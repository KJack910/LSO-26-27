#include "game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    GameBoard board;
    game_board_init(&board);
    assert(game_place_ship(&board, 0, 0, 3, 'H') == GAME_OK);
    assert(game_place_ship(&board, 0, 2, 2, 'V') == GAME_INVALID); /* overlap */
    assert(game_place_ship(&board, 9, 9, 2, 'H') == GAME_INVALID); /* out of bounds */
    assert(game_place_ship(&board, 4, 4, 2, 'V') == GAME_OK);
    assert(game_receive_shot(&board, 0, 1) == GAME_HIT);
    assert(game_receive_shot(&board, 0, 1) == GAME_INVALID); /* duplicate */
    assert(game_receive_shot(&board, 9, 9) == GAME_MISS);
    assert(game_all_ships_sunk(&board) == 0);
    assert(game_receive_shot(&board, 0, 0) == GAME_HIT);
    assert(game_receive_shot(&board, 0, 2) == GAME_SUNK);
    assert(game_receive_shot(&board, 4, 4) == GAME_HIT);
    assert(game_receive_shot(&board, 5, 4) == GAME_SUNK);
    assert(game_all_ships_sunk(&board) == 1);
    puts("game rules passed");
    return 0;
}
