#include "server_state.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void call(const char *request, char *response, size_t response_size) {
    server_state_process(request, response, response_size);
}

static void expect_prefix(const char *request, const char *prefix) {
    char response[SERVER_RESPONSE_SIZE];
    call(request, response, sizeof(response));
    assert(strncmp(response, prefix, strlen(prefix)) == 0);
}

static void expect_exact(const char *request, const char *expected) {
    char response[SERVER_RESPONSE_SIZE];
    call(request, response, sizeof(response));
    assert(strcmp(response, expected) == 0);
}

static void setup_started_game(char *session_id, size_t session_id_size) {
    char response[SERVER_RESPONSE_SIZE];
    int player;
    const int fleet[] = {5, 4, 3, 3, 2};

    call("HELLO|Alice", response, sizeof(response));
    assert(strcmp(response, "OK|1|Alice") == 0);
    call("HELLO|Bob", response, sizeof(response));
    assert(strcmp(response, "OK|2|Bob") == 0);
    call("CREATE|1", response, sizeof(response));
    assert(strncmp(response, "OK|S", 4) == 0);
    snprintf(session_id, session_id_size, "%s", response + 3);

    {
        char command[128];
        snprintf(command, sizeof(command), "JOIN|2|%s", session_id);
        expect_exact(command, "OK|request sent");
        snprintf(command, sizeof(command), "DECIDE|1|%s|1", session_id);
        expect_exact(command, "OK|accepted");
    }

    for (player = 1; player <= 2; ++player) {
        int row = player == 1 ? 0 : 5;
        int offset;
        for (offset = 0; offset < 5; ++offset) {
            char command[128];
            snprintf(command, sizeof(command), "PLACE|%d|%s|%d|0|H|%d",
                     player, session_id, row + offset, fleet[offset]);
            expect_exact(command, "OK|placed");
        }
        {
            char command[128];
            snprintf(command, sizeof(command), "READY|%d|%s", player, session_id);
            expect_prefix(command, "OK|");
        }
    }
}

static void assert_view_phase_and_winner(const char *session_id, unsigned player,
                                         const char *phase, const char *winner) {
    char command[128];
    char response[SERVER_RESPONSE_SIZE];
    char *fields[7];
    char *cursor;
    int i;

    snprintf(command, sizeof(command), "VIEW|%u|%s", player, session_id);
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
    char session_id[16];
    char command[128];

    server_state_init();
    setup_started_game(session_id, sizeof(session_id));
    snprintf(command, sizeof(command), "SURRENDER|1|%s", session_id);
    expect_exact(command, "OK|SURRENDER|WIN");
    assert_view_phase_and_winner(session_id, 2, "FINISHED", "Bob");
}

static void test_disconnect_pauses_and_reconnects(void) {
    char session_id[16];
    char command[128];
    time_t base;

    server_state_init();
    setup_started_game(session_id, sizeof(session_id));
    snprintf(command, sizeof(command), "HEARTBEAT|1|%s", session_id);
    expect_prefix(command, "OK|");
    snprintf(command, sizeof(command), "HEARTBEAT|2|%s", session_id);
    expect_prefix(command, "OK|");
    base = time(NULL);

    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + 1);
    assert_view_phase_and_winner(session_id, 2, "PAUSED", "-");

    snprintf(command, sizeof(command), "HEARTBEAT|1|%s", session_id);
    expect_exact(command, "OK|RECONNECTED");
    assert_view_phase_and_winner(session_id, 1, "PLAYING", "-");
}

static void test_disconnect_timeout_assigns_victory(void) {
    char session_id[16];
    char command[128];
    time_t base;

    server_state_init();
    setup_started_game(session_id, sizeof(session_id));
    snprintf(command, sizeof(command), "HEARTBEAT|1|%s", session_id);
    expect_prefix(command, "OK|");
    snprintf(command, sizeof(command), "HEARTBEAT|2|%s", session_id);
    expect_prefix(command, "OK|");
    base = time(NULL);

    server_state_maintenance_at(base + SERVER_HEARTBEAT_STALE_SECONDS + 1);
    assert_view_phase_and_winner(session_id, 2, "PAUSED", "-");
    snprintf(command, sizeof(command), "HEARTBEAT|2|%s", session_id);
    expect_prefix(command, "OK|");
    base = time(NULL);
    server_state_maintenance_at(base + SERVER_DISCONNECT_TIMEOUT_SECONDS + 1);
    assert_view_phase_and_winner(session_id, 2, "FINISHED", "Bob");
}

int main(void) {
    test_surrender_assigns_winner();
    test_disconnect_pauses_and_reconnects();
    test_disconnect_timeout_assigns_victory();
    puts("server state disconnect and surrender rules passed");
    return 0;
}
