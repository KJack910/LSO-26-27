#include "game.h"
#include <string.h>
void game_board_init(GameBoard *board) {
    if (board != NULL) memset(board, 0, sizeof(*board));
}

GameResult game_place_ship(GameBoard *board, int row, int col, int length, char orientation) {
    int offset;
    unsigned char ship_id;

    if (board == NULL || length < 1 || length > GAME_SIZE ||
        (orientation != 'H' && orientation != 'V') ||
        row < 0 || col < 0 || row >= GAME_SIZE || col >= GAME_SIZE ||
        board->ship_count >= GAME_MAX_SHIPS) {
        return GAME_INVALID;
    }
    if ((orientation == 'H' && col + length > GAME_SIZE) ||
        (orientation == 'V' && row + length > GAME_SIZE)) {
        return GAME_INVALID;
    }

    for (offset = 0; offset < length; ++offset) {
        int current_row = row + (orientation == 'V' ? offset : 0);
        int current_col = col + (orientation == 'H' ? offset : 0);
        if (board->cells[current_row][current_col]) return GAME_INVALID;
    }

    ship_id = (unsigned char)(board->ship_count + 1);
    for (offset = 0; offset < length; ++offset) {
        int current_row = row + (orientation == 'V' ? offset : 0);
        int current_col = col + (orientation == 'H' ? offset : 0);
        board->cells[current_row][current_col] = 1;
        board->ship_ids[current_row][current_col] = ship_id;
    }
    board->ship_segments[board->ship_count] = length;
    ++board->ship_count;
    board->ships_left += length;
    return GAME_OK;
}
GameResult game_receive_shot(GameBoard *board, int row, int col) {
    unsigned char ship_id;

    if (board == NULL || row < 0 || col < 0 || row >= GAME_SIZE || col >= GAME_SIZE ||
        board->shots[row][col] != 0) {
        return GAME_INVALID;
    }
    if (!board->cells[row][col]) {
        board->shots[row][col] = 1;
        return GAME_MISS;
    }

    ship_id = board->ship_ids[row][col];
    if (ship_id == 0 || ship_id > board->ship_count) return GAME_INVALID;
    board->shots[row][col] = 2;
    board->cells[row][col] = 0;
    --board->ship_segments[ship_id - 1];
    --board->ships_left;
    return board->ship_segments[ship_id - 1] == 0 ? GAME_SUNK : GAME_HIT;
}
int game_all_ships_sunk(const GameBoard *board) {
    return board != NULL && board->ship_count > 0 && board->ships_left == 0;
}
