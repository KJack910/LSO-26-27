#include "server_state.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static char alice_token[SERVER_TOKEN_SIZE];
static char bob_token[SERVER_TOKEN_SIZE];
static unsigned alice_id;
static unsigned bob_id;

static void call(const char *request, char *response, size_t response_size) {
    server_state_process(request, response, response_size);
}

static void expect_prefix(const char *request, const char *prefix) {
    char response[SERVER_RESPONSE_SIZE];
    call(request, response, sizeof(response));
    if (strncmp(response, prefix, strlen(prefix)) != 0) {
        fprintf(stderr, "request=%s response=%s expected prefix=%s\\n", request, response, prefix);
        assert(0);
    }
}

static void expect_exact(const char *request, const char *expected) {
    char response[SERVER_RESPONSE_SIZE];
    call(request, response, sizeof(response));
    assert(strcmp(response, expected) == 0);
}

static void register_player(const char *name, unsigned *id, char *token, size_t token_size) {
    char request[128], response[SERVER_RESPONSE_SIZE], parsed_name[64];
    snprintf(request, sizeof(request), "HELLO|%s", name);
    call(request, response, sizeof(response));
    assert(sscanf(response, "OK|%u|%63[^|]|%32s", id, parsed_name, token) == 3);
    assert(strcmp(parsed_name, name) == 0);
    token[token_size - 1] = '\0';
}

static void setup_started_game(char *session_id, size_t session_id_size) {
    char response[SERVER_RESPONSE_SIZE];
    int player;
    const int fleet[] = {5, 4, 3, 3, 2};
    char command[256];

    register_player("Alice", &alice_id, alice_token, sizeof(alice_token));
    register_player("Bob", &bob_id, bob_token, sizeof(bob_token));
    snprintf(command, sizeof(command), "CREATE|%u|%s", alice_id, alice_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|S", 4) == 0);
    snprintf(session_id, session_id_size, "%s", response + 3);

    snprintf(command, sizeof(command), "JOIN|%u|%s|%s", bob_id, session_id, bob_token);
    expect_exact(command, "OK|request sent");
    snprintf(command, sizeof(command), "DECIDE|%u|%s|1|%s", alice_id, session_id, alice_token);
    expect_exact(command, "OK|accepted");

    for (player = 1; player <= 2; ++player) {
        unsigned id = player == 1 ? alice_id : bob_id;
        const char *token = player == 1 ? alice_token : bob_token;
        int row = player == 1 ? 0 : 5;
        int offset;
        for (offset = 0; offset < 5; ++offset) {
            snprintf(command, sizeof(command), "PLACE|%u|%s|%d|0|H|%d|%s",
                     id, session_id, row + offset, fleet[offset], token);
            expect_exact(command, "OK|placed");
        }
        snprintf(command, sizeof(command), "READY|%u|%s|%s", id, session_id, token);
        expect_prefix(command, "OK|");
    }
}

static void assert_view_phase_and_winner(const char *session_id, unsigned player,
                                         const char *token, const char *phase, const char *winner) {
    char command[256], response[SERVER_RESPONSE_SIZE];
    char *fields[7];
    char *cursor;
    int i;

    snprintf(command, sizeof(command), "VIEW|%u|%s|%s", player, session_id, token);
    call(command, response, sizeof(response));
    fields[0] = response;
    cursor = response;
    for (i = 1; i < 7; ++i) {
        cursor = strchr(cursor, '|');
        assert(cursor != NULL);
        *cursor++ = '\0';
        fields[i] = cursor;
    }
    assert(strcmp(fields[0], "OK") == 0);
    assert(strcmp(fields[1], phase) == 0);
    assert(strcmp(fields[6], winner) == 0);
}

static void test_surrender_assigns_winner(void) {
    char session_id[16], command[256];
    server_state_init();
    setup_started_game(session_id, sizeof(session_id));
    snprintf(command, sizeof(command), "SURRENDER|%u|%s|%s", alice_id, session_id, alice_token);
    expect_exact(command, "OK|SURRENDER|WIN");
    assert_view_phase_and_winner(session_id, bob_id, bob_token, "FINISHED", "Bob");
}

static void test_disconnect_pauses_and_reconnects(void) {
    char session_id[16], command[256];
    time_t base;
    server_state_init();
    setup_started_game(session_id, sizeof(session_id));
    snprintf(command, sizeof(command), "HEARTBEAT|%u|%s|%s", alice_id, session_id, alice_token);
    expect_prefix(command, "OK|");
    snprintf(command, sizeof(command), "HEARTBEAT|%u|%s|%s", bob_id, session_id, bob_token);
    expect_prefix(command, "OK|");
    base = time(NULL);
    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + 1);
    assert_view_phase_and_winner(session_id, bob_id, bob_token, "PAUSED", "-");
    snprintf(command, sizeof(command), "HEARTBEAT|%u|%s|%s", alice_id, session_id, alice_token);
    expect_exact(command, "OK|RECONNECTED");
    assert_view_phase_and_winner(session_id, alice_id, alice_token, "PLAYING", "-");
}

static void test_disconnect_timeout_assigns_victory(void) {
    char session_id[16], command[256];
    time_t base;
    server_state_init();
    setup_started_game(session_id, sizeof(session_id));
    snprintf(command, sizeof(command), "HEARTBEAT|%u|%s|%s", alice_id, session_id, alice_token);
    expect_prefix(command, "OK|");
    snprintf(command, sizeof(command), "HEARTBEAT|%u|%s|%s", bob_id, session_id, bob_token);
    expect_prefix(command, "OK|");
    base = time(NULL);
    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + 1);
    assert_view_phase_and_winner(session_id, bob_id, bob_token, "PAUSED", "-");
    snprintf(command, sizeof(command), "HEARTBEAT|%u|%s|%s", bob_id, session_id, bob_token);
    expect_prefix(command, "OK|");
    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + SERVER_DISCONNECT_TIMEOUT_SECONDS + 1);
    assert_view_phase_and_winner(session_id, bob_id, bob_token, "FINISHED", "Bob");
}

static void test_simultaneous_disconnect_reclaims_session(void) {
    char session_id[16], command[256], response[SERVER_RESPONSE_SIZE];
    time_t base;
    server_state_init();
    setup_started_game(session_id, sizeof(session_id));
    base = time(NULL);
    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + 1);
    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + SERVER_DISCONNECT_TIMEOUT_SECONDS + 1);
    snprintf(command, sizeof(command), "CREATE|%u|%s", alice_id, alice_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|S", 4) == 0);
}

static void test_lobby_and_finished_session_cleanup(void) {
    char command[256], response[SERVER_RESPONSE_SIZE], session_id[16];
    time_t base;
    server_state_init();
    register_player("LobbyHost", &alice_id, alice_token, sizeof(alice_token));
    snprintf(command, sizeof(command), "CREATE|%u|%s", alice_id, alice_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|S", 4) == 0);
    snprintf(session_id, sizeof(session_id), "%s", response + 3);
    base = time(NULL);
    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + 1);
    snprintf(command, sizeof(command), "CREATE|%u|%s", alice_id, alice_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|S", 4) == 0);

    server_state_init();
    setup_started_game(session_id, sizeof(session_id));
    snprintf(command, sizeof(command), "SURRENDER|%u|%s|%s", alice_id, session_id, alice_token);
    expect_exact(command, "OK|SURRENDER|WIN");
    base = time(NULL);
    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + 1);
    snprintf(command, sizeof(command), "CREATE|%u|%s", alice_id, alice_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|S", 4) == 0);
}

static void test_quit_releases_player_slots(void) {
    int i;
    char name[32], command[256], response[SERVER_RESPONSE_SIZE], token[SERVER_TOKEN_SIZE];
    unsigned id;
    server_state_init();
    for (i = 0; i < 128; ++i) {
        snprintf(name, sizeof(name), "Player%d", i);
        register_player(name, &id, token, sizeof(token));
        snprintf(command, sizeof(command), "QUIT|%u|%s", id, token);
        call(command, response, sizeof(response));
        assert(strcmp(response, "OK|quit allowed") == 0);
    }
    register_player("AfterQuit", &id, token, sizeof(token));
    assert(id > 128);
}

static void test_duplicate_names_and_token_authentication(void) {
    char first[SERVER_TOKEN_SIZE], second[SERVER_TOKEN_SIZE], command[256], response[SERVER_RESPONSE_SIZE];
    unsigned first_id, second_id;
    server_state_init();
    register_player("SameName", &first_id, first, sizeof(first));
    register_player("SameName", &second_id, second, sizeof(second));
    assert(first_id != second_id);
    snprintf(command, sizeof(command), "CREATE|%u|%s", first_id, second);
    call(command, response, sizeof(response));
    assert(strncmp(response, "ERR|", 4) == 0);
    snprintf(command, sizeof(command), "CREATE|%u|%s", first_id, first);
    expect_prefix(command, "OK|S");
}

static void test_resume_after_timeout_and_target_privacy(void) {
    char session_id[16], command[256], response[SERVER_RESPONSE_SIZE];
    time_t base;
    char *fields[7], *cursor;
    int i;
    server_state_init();
    setup_started_game(session_id, sizeof(session_id));
    snprintf(command, sizeof(command), "SHOT|%u|%s|5|0|%s", alice_id, session_id, alice_token);
    expect_prefix(command, "OK|HIT");
    snprintf(command, sizeof(command), "VIEW|%u|%s|%s", alice_id, session_id, alice_token);
    call(command, response, sizeof(response));
    fields[0] = response;
    cursor = response;
    for (i = 1; i < 7; ++i) {
        cursor = strchr(cursor, '|');
        assert(cursor != NULL);
        *cursor++ = '\0';
        fields[i] = cursor;
    }
    assert(fields[5][50] == 'X');
    snprintf(command, sizeof(command), "HEARTBEAT|%u|%s|%s", alice_id, session_id, alice_token);
    expect_prefix(command, "OK|");
    snprintf(command, sizeof(command), "HEARTBEAT|%u|%s|%s", bob_id, session_id, bob_token);
    expect_prefix(command, "OK|");
    base = time(NULL);
    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + 1);
    snprintf(command, sizeof(command), "HEARTBEAT|%u|%s|%s", bob_id, session_id, bob_token);
    expect_prefix(command, "OK|");
    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + SERVER_DISCONNECT_TIMEOUT_SECONDS + 1);

    snprintf(command, sizeof(command), "RESUME|%u|%s", alice_id, alice_token);
    call(command, response, sizeof(response));
    assert(strcmp(response, "ERR|no active session") == 0);
    assert_view_phase_and_winner(session_id, bob_id, bob_token, "FINISHED", "Bob");
    snprintf(command, sizeof(command), "VIEW|%u|%s|%s", bob_id, session_id, bob_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|FINISHED|HOST|", strlen("OK|FINISHED|HOST|")) == 0);
}

static void test_accepted_pregame_host_disconnect(void) {
    char session_id[16], command[256], response[SERVER_RESPONSE_SIZE];
    time_t base;
    server_state_init();
    register_player("Host", &alice_id, alice_token, sizeof(alice_token));
    register_player("Guest", &bob_id, bob_token, sizeof(bob_token));
    snprintf(command, sizeof(command), "CREATE|%u|%s", alice_id, alice_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|S", 4) == 0);
    snprintf(session_id, sizeof(session_id), "%s", response + 3);
    snprintf(command, sizeof(command), "JOIN|%u|%s|%s", bob_id, session_id, bob_token);
    expect_exact(command, "OK|request sent");
    snprintf(command, sizeof(command), "DECIDE|%u|%s|1|%s", alice_id, session_id, alice_token);
    expect_exact(command, "OK|accepted");
    base = time(NULL);
    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + 1);
    snprintf(command, sizeof(command), "HEARTBEAT|%u|%s|%s", bob_id, session_id, bob_token);
    expect_prefix(command, "OK|");
    snprintf(command, sizeof(command), "VIEW|%u|%s|%s", bob_id, session_id, bob_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|PAUSED|GUEST|", strlen("OK|PAUSED|GUEST|")) == 0);
    server_state_maintenance_at(base + SERVER_DISCONNECT_TIMEOUT_SECONDS + 1);
    assert_view_phase_and_winner(session_id, bob_id, bob_token, "FINISHED", "Guest");
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|FINISHED|HOST|", strlen("OK|FINISHED|HOST|")) == 0);
}

static void test_p03_rematch_end_to_end(void) {
    char session_id[16], command[256], response[SERVER_RESPONSE_SIZE];
    const int fleet_lens[] = {5, 4, 3, 3, 2};
    server_state_init();
    setup_started_game(session_id, sizeof(session_id));

    /* Alice surrenders -> Bob wins */
    snprintf(command, sizeof(command), "SURRENDER|%u|%s|%s", alice_id, session_id, alice_token);
    expect_exact(command, "OK|SURRENDER|WIN");
    assert_view_phase_and_winner(session_id, alice_id, alice_token, "FINISHED", "Bob");
    assert_view_phase_and_winner(session_id, bob_id, bob_token, "FINISHED", "Bob");

    /* Host (Alice) initiates rematch */
    snprintf(command, sizeof(command), "REMATCH|%u|%s|same|%s", alice_id, session_id, alice_token);
    expect_exact(command, "OK|rematch started");

    /* Phase transitions back to PLACEMENT for both */
    assert_view_phase_and_winner(session_id, alice_id, alice_token, "PLACEMENT", "-");
    assert_view_phase_and_winner(session_id, bob_id, bob_token, "PLACEMENT", "-");

    /* Both players place all 5 ships again */
    for (int p = 1; p <= 2; ++p) {
        unsigned id = (p == 1) ? alice_id : bob_id;
        const char *token = (p == 1) ? alice_token : bob_token;
        int row = (p == 1) ? 0 : 5;
        for (int offset = 0; offset < 5; ++offset) {
            snprintf(command, sizeof(command), "PLACE|%u|%s|%d|0|H|%d|%s",
                     id, session_id, row + offset, fleet_lens[offset], token);
            expect_exact(command, "OK|placed");
        }
        snprintf(command, sizeof(command), "READY|%u|%s|%s", id, session_id, token);
        call(command, response, sizeof(response));
        if (p == 1) {
            assert(strcmp(response, "OK|waiting for opponent") == 0);
        } else {
            assert(strcmp(response, "OK|game started") == 0);
        }
    }

    /* Game restarted successfully in PLAYING phase */
    assert_view_phase_and_winner(session_id, alice_id, alice_token, "PLAYING", "-");
    assert_view_phase_and_winner(session_id, bob_id, bob_token, "PLAYING", "-");
}

static void test_p04_exit_and_result_both_participants(void) {
    char session_id[16], command[256], response[SERVER_RESPONSE_SIZE];
    server_state_init();
    setup_started_game(session_id, sizeof(session_id));

    /* Alice surrenders -> Bob wins */
    snprintf(command, sizeof(command), "SURRENDER|%u|%s|%s", alice_id, session_id, alice_token);
    expect_exact(command, "OK|SURRENDER|WIN");

    /* 1. Both participants receive final status with over=1 */
    snprintf(command, sizeof(command), "STATUS|%u|%s|%s", alice_id, session_id, alice_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|Alice|Bob|0|1|1|1", strlen("OK|Alice|Bob|0|1|1|1")) == 0);

    snprintf(command, sizeof(command), "STATUS|%u|%s|%s", bob_id, session_id, bob_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|Alice|Bob|0|1|1|1", strlen("OK|Alice|Bob|0|1|1|1")) == 0);

    /* 2. Both participants receive the winner name in VIEW */
    assert_view_phase_and_winner(session_id, alice_id, alice_token, "FINISHED", "Bob");
    assert_view_phase_and_winner(session_id, bob_id, bob_token, "FINISHED", "Bob");

    /* 3. Guest exits via LEAVE -> host remains in lobby waiting for a new request */
    snprintf(command, sizeof(command), "LEAVE|%u|%s|%s", bob_id, session_id, bob_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|left", 7) == 0);

    snprintf(command, sizeof(command), "VIEW|%u|%s|%s", alice_id, session_id, alice_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|WAITING_REQUEST|HOST|", strlen("OK|WAITING_REQUEST|HOST|")) == 0);

    /* 4. Host exits via LEAVE -> session is cleared */
    snprintf(command, sizeof(command), "LEAVE|%u|%s|%s", alice_id, session_id, alice_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "OK|left", 7) == 0);

    snprintf(command, sizeof(command), "VIEW|%u|%s|%s", alice_id, session_id, alice_token);
    call(command, response, sizeof(response));
    assert(strncmp(response, "ERR|no session", 14) == 0);
}

int main(void) {
    test_surrender_assigns_winner();
    test_disconnect_pauses_and_reconnects();
    test_disconnect_timeout_assigns_victory();
    test_simultaneous_disconnect_reclaims_session();
    test_lobby_and_finished_session_cleanup();
    test_quit_releases_player_slots();
    test_duplicate_names_and_token_authentication();
    test_resume_after_timeout_and_target_privacy();
    test_accepted_pregame_host_disconnect();
    test_p03_rematch_end_to_end();
    test_p04_exit_and_result_both_participants();
    puts("server state regression rules passed");
    return 0;
}
