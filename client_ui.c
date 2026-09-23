#include "client_ui.h"

#include "client_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_CAPACITY 128
#define RESPONSE_CAPACITY 2048
#define GRID_SIDE 10
#define GRID_CELLS 100
#define FLEET_COUNT 5
#define NAME_CAPACITY 64

static const int fleet[FLEET_COUNT] = {5, 4, 3, 3, 2};

static int read_input(const char *prompt, char *buffer, size_t capacity) {
    size_t length;
    int character;
    fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(buffer, (int)capacity, stdin)) {
        buffer[0] = '\0';
        return 0;
    }
    length = strcspn(buffer, "\r\n");
    if (buffer[length] == '\0') {
        while ((character = getchar()) != '\n' && character != EOF) {}
    }
    buffer[length] = '\0';
    return 1;
}

static int request_server(const char *host, const char *port, const char *command,
                          char *response, size_t capacity) {
    response[0] = '\0';
    if (client_request(host, port, command, response, capacity) != 0) {
        puts("Connessione al server non riuscita.");
        return 0;
    }
    return 1;
}

static void print_lobby(const char *host, const char *port) {
    char response[RESPONSE_CAPACITY];
    char *entry;
    if (!request_server(host, port, "LIST", response, sizeof(response)) || strncmp(response, "OK", 2)) return;
    puts("\nPARTITE DISPONIBILI");
    entry = strchr(response, '|');
    if (!entry) {
        puts("  Nessuna sessione aperta.");
        return;
    }
    do {
        char *next = strchr(entry + 1, '|');
        char *comma = strchr(entry + 1, ',');
        char *pending = comma ? strchr(comma + 1, ',') : NULL;
        if (next) *next = '\0';
        if (comma && pending) {
            *comma = '\0';
            *pending = '\0';
            printf("  %-6s  Host: %-20s  %s\n", entry + 1, comma + 1,
                   strcmp(pending + 1, "1") == 0 ? "richiesta in attesa" : "in attesa di un giocatore");
        }
        entry = next;
    } while (entry);
}

static void print_grids(const char *own, const char *target) {
    int row, col;
    puts("\n  LA TUA FLOTTA                         BERSAGLI SULLA GRIGLIA AVVERSARIA");
    puts("     0 1 2 3 4 5 6 7 8 9                  0 1 2 3 4 5 6 7 8 9");
    for (row = 0; row < GRID_SIDE; ++row) {
        printf(" %2d  ", row);
        for (col = 0; col < GRID_SIDE; ++col) printf("%c ", own[row * GRID_SIDE + col]);
        printf("             %2d  ", row);
        for (col = 0; col < GRID_SIDE; ++col) printf("%c ", target[row * GRID_SIDE + col]);
        putchar('\n');
    }
    puts("Legenda: S nave, X colpita, o acqua colpita, . acqua inesplorata");
}

static int fetch_view(const char *host, const char *port, const char *pid, const char *sid,
                      char *phase, size_t phase_size, char *role, size_t role_size,
                      char *turn, size_t turn_size, char *own, char *target) {
    char command[INPUT_CAPACITY], response[RESPONSE_CAPACITY];
    char *fields[6], *cursor;
    snprintf(command, sizeof(command), "VIEW|%s|%s", pid, sid);
    if (!request_server(host, port, command, response, sizeof(response)) || strncmp(response, "OK|", 3)) {
        if (response[0]) printf("Sessione non disponibile: %s\n", response);
        return 0;
    }
    fields[0] = response;
    cursor = response;
    for (int i = 1; i < 6; ++i) {
        cursor = strchr(cursor, '|');
        if (!cursor) return 0;
        *cursor++ = '\0';
        fields[i] = cursor;
    }
    if (strlen(fields[4]) != GRID_CELLS || strlen(fields[5]) != GRID_CELLS) return 0;
    snprintf(phase, phase_size, "%s", fields[1]);
    snprintf(role, role_size, "%s", fields[2]);
    snprintf(turn, turn_size, "%s", fields[3]);
    memcpy(own, fields[4], GRID_CELLS + 1);
    memcpy(target, fields[5], GRID_CELLS + 1);
    printf("\nSessione %s | Fase: %s | Ruolo: %s | Turno: %s\n", sid, phase, role, turn);
    print_grids(own, target);
    return 1;
}

static int fetch_status(const char *host, const char *port, const char *pid, const char *sid,
                        char *host_name, char *guest_name, int *pending, int *accepted,
                        int *started, int *over) {
    char command[INPUT_CAPACITY], response[RESPONSE_CAPACITY];
    char *fields[7], *cursor;
    snprintf(command, sizeof(command), "STATUS|%s|%s", pid, sid);
    if (!request_server(host, port, command, response, sizeof(response)) || strncmp(response, "OK|", 3)) return 0;
    fields[0] = response;
    cursor = response;
    for (int i = 1; i < 7; ++i) {
        cursor = strchr(cursor, '|');
        if (!cursor) return 0;
        *cursor++ = '\0';
        fields[i] = cursor;
    }
    snprintf(host_name, NAME_CAPACITY, "%s", fields[1]);
    snprintf(guest_name, NAME_CAPACITY, "%s", fields[2]);
    *pending = atoi(fields[3]);
    *accepted = atoi(fields[4]);
    *started = atoi(fields[5]);
    *over = atoi(fields[6]);
    return 1;
}

static int send_simple(const char *host, const char *port, const char *command) {
    char response[RESPONSE_CAPACITY];
    if (!request_server(host, port, command, response, sizeof(response))) return 0;
    puts(response);
    return strncmp(response, "OK|", 3) == 0;
}

static int place_fleet(const char *host, const char *port, const char *pid, const char *sid,
                       int *placed) {
    char input[INPUT_CAPACITY], command[INPUT_CAPACITY];
    while (*placed < FLEET_COUNT) {
        int row, col;
        char orientation;
        printf("\nPosizionamento nave %d/%d (lunghezza %d). Coordinate 0-9.\n",
               *placed + 1, FLEET_COUNT, fleet[*placed]);
        if (!read_input("Riga colonna orientamento H/V (es. 2 1 H): ", input, sizeof(input))) return 0;
        if (sscanf(input, "%d %d %c", &row, &col, &orientation) != 3) {
            puts("Formato non valido.");
            continue;
        }
        snprintf(command, sizeof(command), "PLACE|%s|%s|%d|%d|%c|%d",
                 pid, sid, row, col, orientation, fleet[*placed]);
        if (send_simple(host, port, command)) ++*placed;
        else puts("Riprova con una posizione valida.");
    }
    snprintf(command, sizeof(command), "READY|%s|%s", pid, sid);
    return send_simple(host, port, command);
}

static void session_screen(const char *host, const char *port, const char *pid,
                           const char *player_name, const char *sid, int is_host) {
    int placed = 0;
    char input[INPUT_CAPACITY], command[INPUT_CAPACITY];
    char phase[32], role[16], turn[NAME_CAPACITY], own[GRID_CELLS + 1], target[GRID_CELLS + 1];
    for (;;) {
        char host_name[NAME_CAPACITY] = "", guest_name[NAME_CAPACITY] = "";
        int pending = 0, accepted = 0, started = 0, over = 0;
        if (!fetch_status(host, port, pid, sid, host_name, guest_name,
                          &pending, &accepted, &started, &over)) {
            puts("Non sei più membro della sessione o il server non risponde.");
            return;
        }
        if (!fetch_view(host, port, pid, sid, phase, sizeof(phase), role, sizeof(role),
                        turn, sizeof(turn), own, target)) return;
        is_host = strcmp(role, "HOST") == 0;

        if (strcmp(phase, "REQUEST_PENDING") == 0 && is_host) {
            printf("\n%s chiede di unirsi alla sessione.\n", guest_name);
            if (!read_input("Accettare? (s/n): ", input, sizeof(input))) return;
            snprintf(command, sizeof(command), "DECIDE|%s|%s|%d", pid, sid,
                     input[0] == 's' || input[0] == 'S');
            send_simple(host, port, command);
            continue;
        }
        if (strcmp(phase, "WAITING_REQUEST") == 0) {
            puts("Lobby della sessione: condividi l'ID con l'altro giocatore.");
            if (!read_input("Premi INVIO per aggiornare la lobby: ", input, sizeof(input))) return;
            continue;
        }
        if (strcmp(phase, "WAITING_ACCEPT") == 0) {
            puts("Richiesta inviata. Attendi l'accettazione dell'host.");
            if (!read_input("Premi INVIO per verificare: ", input, sizeof(input))) return;
            continue;
        }
        if (strcmp(phase, "PLACEMENT") == 0 || strcmp(phase, "WAITING_READY") == 0) {
            if (placed < FLEET_COUNT && !place_fleet(host, port, pid, sid, &placed)) continue;
            puts("Flotta pronta: in attesa dell'avversario.");
            if (!read_input("Premi INVIO per aggiornare: ", input, sizeof(input))) return;
            continue;
        }
        if (strcmp(phase, "PLAYING") == 0) {
            if (strcmp(turn, player_name) != 0) {
                puts("Turno dell'avversario.");
                if (!read_input("Premi INVIO per aggiornare (Q per arrenderti): ", input, sizeof(input))) return;
                if (input[0] == 'q' || input[0] == 'Q') {
                    snprintf(command, sizeof(command), "SURRENDER|%s|%s", pid, sid);
                    if (send_simple(host, port, command)) {
                        snprintf(command, sizeof(command), "LEAVE|%s|%s", pid, sid);
                        send_simple(host, port, command);
                        puts("Ti sei arreso: partita persa.");
                        return;
                    }
                }
                continue;
            }
            if (!read_input("Tiro riga colonna (es. 4 7), oppure Q per resa: ", input, sizeof(input))) return;
            if (input[0] == 'q' || input[0] == 'Q') {
                snprintf(command, sizeof(command), "SURRENDER|%s|%s", pid, sid);
                if (send_simple(host, port, command)) {
                    snprintf(command, sizeof(command), "LEAVE|%s|%s", pid, sid);
                    send_simple(host, port, command);
                    puts("Ti sei arreso: partita persa.");
                    return;
                }
                continue;
            }
            {
                int row, col;
                if (sscanf(input, "%d %d", &row, &col) != 2) {
                    puts("Coordinate non valide.");
                    continue;
                }
                snprintf(command, sizeof(command), "SHOT|%s|%s|%d|%d", pid, sid, row, col);
                send_simple(host, port, command);
            }
            continue;
        }
        if (strcmp(phase, "FINISHED") == 0) {
            if (is_host) {
                puts("\n=== PARTITA TERMINATA ===\n  D  Rivincita o attesa di un nuovo avversario\n  E  Esci dalla sessione");
                if (!read_input("Scelta: ", input, sizeof(input))) return;
                if (input[0] == 'd' || input[0] == 'D') {
                    char decision[INPUT_CAPACITY];
                    puts("  1  Rivincita\n  2  Nuovo avversario");
                    if (!read_input("Scelta: ", decision, sizeof(decision))) return;
                    snprintf(command, sizeof(command), "REMATCH|%s|%s|%s", pid, sid,
                             decision[0] == '1' ? "same" : "new");
                    if (send_simple(host, port, command)) placed = 0;
                } else if (input[0] == 'e' || input[0] == 'E') {
                    snprintf(command, sizeof(command), "LEAVE|%s|%s", pid, sid);
                    if (send_simple(host, port, command)) return;
                }
            } else {
                puts("Partita terminata. Attendi la decisione dell'host.");
                if (!read_input("Premi INVIO per aggiornare: ", input, sizeof(input))) return;
                if (over && !accepted) return;
            }
        }
    }
}

int client_run(const char *host, const char *port) {
    char name[INPUT_CAPACITY], command[RESPONSE_CAPACITY], response[RESPONSE_CAPACITY];
    char player_id[32], choice[INPUT_CAPACITY], session_id[16];
    if (!read_input("Nome giocatore: ", name, sizeof(name)) || !name[0] || strchr(name, '|')) {
        puts("Nome non valido.");
        return 1;
    }
    snprintf(command, sizeof(command), "HELLO|%s", name);
    if (!request_server(host, port, command, response, sizeof(response)) || strncmp(response, "OK|", 3)) {
        fprintf(stderr, "Registrazione non riuscita: %s\n", response);
        return 1;
    }
    {
        char *separator = strchr(response + 3, '|');
        if (!separator) return 1;
        *separator = '\0';
        snprintf(player_id, sizeof(player_id), "%s", response + 3);
        snprintf(name, sizeof(name), "%s", separator + 1);
    }
    printf("\nBenvenuto, %s. ID giocatore: %s\n", name, player_id);
    for (;;) {
        print_lobby(host, port);
        puts("\nMENU PRINCIPALE\n  1  Crea una partita\n  2  Aggiorna la lobby\n  3  Unisciti a una sessione\n  0  Esci");
        if (!read_input("Scelta: ", choice, sizeof(choice))) return 0;
        if (choice[0] == '0') {
            snprintf(command, sizeof(command), "QUIT|%s", player_id);
            if (send_simple(host, port, command)) return 0;
        } else if (choice[0] == '1') {
            snprintf(command, sizeof(command), "CREATE|%s", player_id);
            if (request_server(host, port, command, response, sizeof(response)) && !strncmp(response, "OK|", 3)) {
                snprintf(session_id, sizeof(session_id), "%s", response + 3);
                printf("Sessione creata: %s. Accesso diretto alla lobby.\n", session_id);
                session_screen(host, port, player_id, name, session_id, 1);
            } else puts(response);
        } else if (choice[0] == '2') {
            continue;
        } else if (choice[0] == '3') {
            if (!read_input("ID sessione (S seguito da 5 cifre): ", session_id, sizeof(session_id))) return 0;
            snprintf(command, sizeof(command), "JOIN|%s|%s", player_id, session_id);
            if (request_server(host, port, command, response, sizeof(response)) && !strncmp(response, "OK|", 3)) {
                puts("Richiesta inviata. Accesso alla lobby della sessione.");
                session_screen(host, port, player_id, name, session_id, 0);
            } else puts(response);
        } else puts("Scelta non riconosciuta.");
    }
}
