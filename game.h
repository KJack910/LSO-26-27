#ifndef BATTLESHIP_GAME_H
#define BATTLESHIP_GAME_H
#define GAME_SIZE 10
#define GAME_MAX_SHIPS 5
typedef struct {
    char cells[GAME_SIZE][GAME_SIZE];
    /* 0 = unshot, 1 = miss, 2 = hit. */
    char shots[GAME_SIZE][GAME_SIZE];
    unsigned char ship_ids[GAME_SIZE][GAME_SIZE];
    int ship_segments[GAME_MAX_SHIPS];
    int ship_count;
    int ships_left;
} GameBoard;
typedef enum { GAME_OK=0, GAME_INVALID=-1, GAME_HIT=1, GAME_MISS=2, GAME_SUNK=3 } GameResult;
void game_board_init(GameBoard *board);
GameResult game_place_ship(GameBoard *board, int row, int col, int length, char orientation);
GameResult game_receive_shot(GameBoard *board, int row, int col);
int game_all_ships_sunk(const GameBoard *board);
#endif
