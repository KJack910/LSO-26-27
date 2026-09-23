#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif

#include "server_state.h"
#include "game.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION StateMutex;
#define state_mutex_init(m) InitializeCriticalSection(m)
#define state_mutex_lock(m) EnterCriticalSection(m)
#define state_mutex_unlock(m) LeaveCriticalSection(m)
#define strtok_r strtok_s
#else
#include <pthread.h>
typedef pthread_mutex_t StateMutex;
#define state_mutex_init(m) pthread_mutex_init(m, NULL)
#define state_mutex_lock(m) pthread_mutex_lock(m)
#define state_mutex_unlock(m) pthread_mutex_unlock(m)
#endif

#define MAX_PLAYERS 128
#define MAX_SESSIONS 64
#define NAME_LEN 40
#define ID_LEN 40
#define SHIP_COUNT 5

static const int fleet[SHIP_COUNT] = {5, 4, 3, 3, 2};

typedef struct {
    int used;
    unsigned id;
    char name[NAME_LEN];
} Player;

typedef struct {
    int used;
    int pending;
    int accepted;
    int started;
    int ready[2];
    int turn;
    int over;
    unsigned winner;
    char id[ID_LEN];
    unsigned host;
    unsigned guest;
    char host_name[NAME_LEN];
    char guest_name[NAME_LEN];
    GameBoard board[2];
    int ship_count[2];
} Session;

static Player players[MAX_PLAYERS];
static Session sessions[MAX_SESSIONS];
static StateMutex state_lock;
static unsigned next_player = 1;
static unsigned next_session = 1;

static Player *player_find(unsigned id) {
    int i;
    for (i = 0; i < MAX_PLAYERS; ++i) {
        if (players[i].used && players[i].id == id) return &players[i];
    }
    return NULL;
}

static Session *session_find(const char *id) {
    int i;
    for (i = 0; i < MAX_SESSIONS; ++i) {
        if (sessions[i].used && strcmp(sessions[i].id, id) == 0) return &sessions[i];
    }
    return NULL;
}

static int session_slot(const Session *session, unsigned pid) {
    if (pid == session->host) return 0;
    if (session->guest != 0 && pid == session->guest) return 1;
    return -1;
}

static int has_active_session(unsigned pid) {
    int i;
    for (i = 0; i < MAX_SESSIONS; ++i) {
        Session *session = &sessions[i];
        if (session->used && !session->over && session_slot(session, pid) >= 0) return 1;
    }
    return 0;
}

int server_state_init(void) {
    memset(players, 0, sizeof(players));
    memset(sessions, 0, sizeof(sessions));
    next_player = 1;
    next_session = 1;
    state_mutex_init(&state_lock);
    return 0;
}

static void set_error(char *out, size_t size, const char *message) {
    snprintf(out, size, "ERR|%s", message);
}

static unsigned parse_player_id(const char *text) {
    char *end = NULL;
    unsigned long value;
    if (!text || !*text) return 0;
    value = strtoul(text, &end, 10);
    if (!end || *end || value == 0 || value > UINT_MAX) return 0;
    return (unsigned)value;
}

static int parse_integer(const char *text, int *value) {
    char *end = NULL;
    long parsed;

    if (text == NULL || *text == '\0' || value == NULL) return 0;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
        return 0;
    }
    *value = (int)parsed;
    return 1;
}

static void command_hello(char *name, char *out, size_t size) {
    Player *player = NULL;
    int i;
    if (!name || !*name) { set_error(out, size, "bad request"); return; }
    for (i = 0; name[i]; ++i) {
        if (name[i] == '|' || name[i] == '\n' || name[i] == '\r') name[i] = ' ';
    }
    for (i = 0; i < MAX_PLAYERS; ++i) {
        if (players[i].used && strcmp(players[i].name, name) == 0) {
            player = &players[i];
            break;
        }
    }
    if (!player) {
        for (i = 0; i < MAX_PLAYERS; ++i) if (!players[i].used) { player = &players[i]; break; }
        if (!player) { set_error(out, size, "player capacity full"); return; }
        player->used = 1;
        player->id = next_player++;
        snprintf(player->name, sizeof(player->name), "%s", name);
    }
    snprintf(out, size, "OK|%u|%s", player->id, player->name);
}

static void command_list(char *out, size_t size) {
    size_t used = (size_t)snprintf(out, size, "OK");
    int i;
    for (i = 0; i < MAX_SESSIONS && used < size - 100; ++i) {
        Session *session = &sessions[i];
        if (session->used && !session->accepted && !session->over) {
            int written = snprintf(out + used, size - used, "|%s,%s,%u",
                                   session->id, session->host_name, (unsigned)session->pending);
            if (written > 0) used += (size_t)written;
        }
    }
}

static void command_create(const char *pid_text, char *out, size_t size) {
    unsigned pid = parse_player_id(pid_text);
    Player *player = player_find(pid);
    Session *session = NULL;
    int i;
    unsigned attempts;
    if (!player || has_active_session(pid)) { set_error(out, size, "invalid player or active session"); return; }
    for (i = 0; i < MAX_SESSIONS; ++i) if (!sessions[i].used || sessions[i].over) { session = &sessions[i]; break; }
    if (!session) { set_error(out, size, "session capacity full"); return; }
    memset(session, 0, sizeof(*session));
    session->used = 1;
    session->host = pid;
    snprintf(session->host_name, sizeof(session->host_name), "%s", player->name);
    for (attempts = 0; attempts < 99999; ++attempts) {
        int collision = 0;
        snprintf(session->id, sizeof(session->id), "S%05u", next_session);
        next_session = next_session == 99999 ? 1 : next_session + 1;
        for (i = 0; i < MAX_SESSIONS; ++i) {
            if (&sessions[i] != session && sessions[i].used &&
                strcmp(sessions[i].id, session->id) == 0) {
                collision = 1;
                break;
            }
        }
        if (!collision) break;
    }
    if (attempts == 99999) {
        memset(session, 0, sizeof(*session));
        set_error(out, size, "session id space exhausted");
        return;
    }
    snprintf(out, size, "OK|%s", session->id);
}

static void command_join(const char *pid_text, const char *sid, char *out, size_t size) {
    unsigned pid = parse_player_id(pid_text);
    Player *player = player_find(pid);
    Session *session = sid ? session_find(sid) : NULL;
    if (!player || !session || session->over || session->guest || session->host == pid || has_active_session(pid)) {
        set_error(out, size, "session unavailable or player already active");
        return;
    }
    session->guest = pid;
    session->pending = 1;
    snprintf(session->guest_name, sizeof(session->guest_name), "%s", player->name);
    snprintf(out, size, "OK|request sent");
}

static void command_decide(const char *pid_text, const char *sid, const char *decision,
                           char *out, size_t size) {
    Session *session = sid ? session_find(sid) : NULL;
    if (!session || session->host != parse_player_id(pid_text) || !session->pending) {
        set_error(out, size, "no pending request");
        return;
    }
    session->pending = 0;
    if (decision && strcmp(decision, "1") == 0) {
        session->accepted = 1;
        snprintf(out, size, "OK|accepted");
    } else {
        session->guest = 0;
        session->guest_name[0] = '\0';
        snprintf(out, size, "OK|rejected");
    }
}

static void command_status(const char *pid_text, const char *sid, char *out, size_t size) {
    Session *session = sid ? session_find(sid) : NULL;
    int who = session ? session_slot(session, parse_player_id(pid_text)) : -1;
    if (who < 0) { set_error(out, size, session ? "not a member" : "no session"); return; }
    snprintf(out, size, "OK|%s|%s|%d|%d|%d|%d", session->host_name, session->guest_name,
             session->pending, session->accepted, session->started, session->over);
}

static void command_place(const char *pid_text, const char *sid, const char *row_text,
                          const char *col_text, const char *orientation_text, const char *length_text,
                          char *out, size_t size) {
    Session *session = sid ? session_find(sid) : NULL;
    int who = session ? session_slot(session, parse_player_id(pid_text)) : -1;
    int row, col, length, index;
    if (!session || !session->accepted || session->started || who < 0 || !length_text ||
        session->ship_count[who] >= SHIP_COUNT) {
        set_error(out, size, "placement unavailable"); return;
    }
    if (!parse_integer(row_text, &row) || !parse_integer(col_text, &col) ||
        !parse_integer(length_text, &length) || !orientation_text || orientation_text[1] != '\0') {
        set_error(out, size, "invalid ship placement");
        return;
    }
    index = session->ship_count[who];
    if (length != fleet[index] || !orientation_text ||
        game_place_ship(&session->board[who], row, col, length, orientation_text[0]) != GAME_OK) {
        set_error(out, size, "invalid ship placement"); return;
    }
    ++session->ship_count[who];
    snprintf(out, size, "OK|placed");
}

static void command_ready(const char *pid_text, const char *sid, char *out, size_t size) {
    Session *session = sid ? session_find(sid) : NULL;
    int who = session ? session_slot(session, parse_player_id(pid_text)) : -1;
    if (!session || who < 0 || !session->accepted || session->ship_count[who] != SHIP_COUNT) {
        set_error(out, size, "place all five ships first"); return;
    }
    session->ready[who] = 1;
    if (session->ready[0] && session->ready[1]) session->started = 1;
    snprintf(out, size, "OK|%s", session->started ? "game started" : "waiting for opponent");
}

static void command_shot(const char *pid_text, const char *sid, const char *row_text,
                         const char *col_text, char *out, size_t size) {
    Session *session = sid ? session_find(sid) : NULL;
    int who = session ? session_slot(session, parse_player_id(pid_text)) : -1;
    GameResult result;
    if (!session || !session->started || session->over || who != session->turn) {
        set_error(out, size, "not your turn or game inactive"); return;
    }
    {
        int row, col;
        if (!parse_integer(row_text, &row) || !parse_integer(col_text, &col)) {
            set_error(out, size, "invalid shot coordinates");
            return;
        }
        result = game_receive_shot(&session->board[1 - who], row, col);
    }
    if (result == GAME_INVALID) { set_error(out, size, "invalid or repeated shot"); return; }
    session->turn = 1 - who;
    if (game_all_ships_sunk(&session->board[1 - who])) {
        session->over = 1;
        session->winner = who == 0 ? session->host : session->guest;
        snprintf(out, size, "OK|%s|WIN", result == GAME_MISS ? "MISS" : result == GAME_SUNK ? "SUNK" : "HIT");
    } else {
        snprintf(out, size, "OK|%s", result == GAME_MISS ? "MISS" : result == GAME_SUNK ? "SUNK" : "HIT");
    }
}

static void command_surrender(const char *pid_text, const char *sid, char *out, size_t size) {
    Session *session = sid ? session_find(sid) : NULL;
    int who = session ? session_slot(session, parse_player_id(pid_text)) : -1;
    if (!session || who < 0 || !session->started || session->over) {
        set_error(out, size, "surrender unavailable");
        return;
    }
    session->winner = who == 0 ? session->guest : session->host;
    session->over = 1;
    snprintf(out, size, "OK|SURRENDER|WIN");
}

static void reset_game(Session *session) {
    game_board_init(&session->board[0]);
    game_board_init(&session->board[1]);
    memset(session->ship_count, 0, sizeof(session->ship_count));
    memset(session->ready, 0, sizeof(session->ready));
    session->started = 0;
    session->over = 0;
    session->turn = 0;
    session->winner = 0;
}

static void command_rematch(const char *pid_text, const char *sid, const char *choice,
                            char *out, size_t size) {
    Session *session = sid ? session_find(sid) : NULL;
    if (!session || parse_player_id(pid_text) != session->host || !session->over || !choice) {
        set_error(out, size, "host decision unavailable; use same or new after game"); return;
    }
    if (strcmp(choice, "same") == 0) {
        reset_game(session);
        session->accepted = 1;
        snprintf(out, size, "OK|rematch started");
    } else if (strcmp(choice, "new") == 0) {
        reset_game(session);
        session->guest = 0;
        session->guest_name[0] = '\0';
        session->pending = 0;
        session->accepted = 0;
        snprintf(out, size, "OK|session open for new player");
    } else {
        set_error(out, size, "host decision unavailable; use same or new after game");
    }
}

static void reset_game(Session *session);

static void command_leave(const char *pid_text, const char *sid, char *out, size_t size) {
    Session *session = sid ? session_find(sid) : NULL;
    unsigned pid = parse_player_id(pid_text);
    int who = session ? session_slot(session, pid) : -1;
    if (!session || who < 0) { set_error(out, size, "not a member"); return; }
    if (!session->over) { set_error(out, size, "cannot leave an active session"); return; }
    if (who == 0 && session->guest) {
        session->host = session->guest;
        snprintf(session->host_name, sizeof(session->host_name), "%s", session->guest_name);
        session->guest = 0;
        session->guest_name[0] = '\0';
        session->accepted = 0;
        session->pending = 0;
        reset_game(session);
    } else if (who == 1) {
        session->guest = 0;
        session->guest_name[0] = '\0';
        session->accepted = 0;
        session->pending = 0;
        reset_game(session);
    } else {
        session->used = 0;
    }
    snprintf(out, size, "OK|left; host transfer applied if needed");
}

static void command_quit(const char *pid_text, char *out, size_t size) {
    unsigned pid = parse_player_id(pid_text);
    if (!player_find(pid)) { set_error(out, size, "invalid player"); return; }
    if (has_active_session(pid)) { set_error(out, size, "cannot quit while a session is active"); return; }
    snprintf(out, size, "OK|quit allowed");
}

static const char *session_phase(const Session *session, int who) {
    if (session->over) return "FINISHED";
    if (session->started) return "PLAYING";
    if (!session->guest) return "WAITING_REQUEST";
    if (session->pending) return who == 1 ? "WAITING_ACCEPT" : "REQUEST_PENDING";
    if (!session->accepted) return "WAITING_ACCEPT";
    if (session->ready[0] || session->ready[1]) return "WAITING_READY";
    return "PLACEMENT";
}

static void encode_grid(const GameBoard *board, int target, char grid[GAME_SIZE * GAME_SIZE + 1]) {
    int row, col, offset = 0;
    for (row = 0; row < GAME_SIZE; ++row) {
        for (col = 0; col < GAME_SIZE; ++col) {
            unsigned char shot = (unsigned char)board->shots[row][col];
            if (shot == 2) grid[offset++] = 'X';
            else if (shot == 1) grid[offset++] = 'o';
            else if (!target && board->cells[row][col]) grid[offset++] = 'S';
            else grid[offset++] = '.';
        }
    }
    grid[offset] = '\0';
}

static void command_view(const char *pid_text, const char *sid, char *out, size_t size) {
    Session *session = sid ? session_find(sid) : NULL;
    int who = session ? session_slot(session, parse_player_id(pid_text)) : -1;
    char own[GAME_SIZE * GAME_SIZE + 1];
    char target[GAME_SIZE * GAME_SIZE + 1];
    const char *turn_name = "-";
    if (!session || who < 0) { set_error(out, size, session ? "not a member" : "no session"); return; }
    encode_grid(&session->board[who], 0, own);
    encode_grid(&session->board[1 - who], 1, target);
    if (session->started && !session->over) turn_name = session->turn == 0 ? session->host_name : session->guest_name;
    snprintf(out, size, "OK|%s|%s|%s|%s|%s", session_phase(session, who), who == 0 ? "HOST" : "GUEST",
             turn_name, own, target);
}

void server_state_process(const char *request, char *response, size_t response_size) {
    char line[1024];
    char *save = NULL;
    char *fields[8] = {0};
    char *token;
    int count = 0;
    size_t size = response_size;
    if (!request || !response || size == 0) return;
    snprintf(line, sizeof(line), "%s", request);
    token = strtok_r(line, "|", &save);
    while (token && count < 8) {
        fields[count++] = token;
        token = strtok_r(NULL, "|", &save);
    }
    set_error(response, size, "bad request");
    state_mutex_lock(&state_lock);
    if (!fields[0]) { /* leave default error */ }
    else if (strcmp(fields[0], "HELLO") == 0 && count == 2) command_hello(fields[1], response, size);
    else if (strcmp(fields[0], "LIST") == 0 && count == 1) command_list(response, size);
    else if (strcmp(fields[0], "CREATE") == 0 && count == 2) command_create(fields[1], response, size);
    else if (strcmp(fields[0], "JOIN") == 0 && count == 3) command_join(fields[1], fields[2], response, size);
    else if (strcmp(fields[0], "DECIDE") == 0 && count == 4) command_decide(fields[1], fields[2], fields[3], response, size);
    else if (strcmp(fields[0], "STATUS") == 0 && count == 3) command_status(fields[1], fields[2], response, size);
    else if (strcmp(fields[0], "PLACE") == 0 && count == 7) command_place(fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], response, size);
    else if (strcmp(fields[0], "READY") == 0 && count == 3) command_ready(fields[1], fields[2], response, size);
    else if (strcmp(fields[0], "SHOT") == 0 && count == 5) command_shot(fields[1], fields[2], fields[3], fields[4], response, size);
    else if (strcmp(fields[0], "SURRENDER") == 0 && count == 3) command_surrender(fields[1], fields[2], response, size);
    else if (strcmp(fields[0], "REMATCH") == 0 && count == 4) command_rematch(fields[1], fields[2], fields[3], response, size);
    else if (strcmp(fields[0], "LEAVE") == 0 && count == 3) command_leave(fields[1], fields[2], response, size);
    else if (strcmp(fields[0], "QUIT") == 0 && count == 2) command_quit(fields[1], response, size);
    else if (strcmp(fields[0], "VIEW") == 0 && count == 3) command_view(fields[1], fields[2], response, size);
    state_mutex_unlock(&state_lock);
}
