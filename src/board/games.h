/* This file is automatically generated by `parse-game-list'.
 * Do not modify it, edit `games.list' instead.
 */


#ifndef QUARRY_GAMES_H
#define QUARRY_GAMES_H


typedef enum {
  /* Dummy entry, to fill zero index. */
  GAME_DUMMY,
  GAME_INVALID = GAME_DUMMY,
  FIRST_GAME,

  /* GM[1], Go. */
  GAME_GO = FIRST_GAME,

  /* trademark problems.) */
  GAME_REVERSI,

  /* GM[3], chess. */
  GAME_CHESS,

  /* GM[4], Gomoku + Renju. */
  GAME_GOMOKU,

  /* GM[5], Nine Men's Morris. */
  GAME_NINE_MENS_MORRIS,

  /* GM[6], Backgammon. */
  GAME_BACKGAMMON,

  /* GM[7], Chinese chess. */
  GAME_CHINESE_CHESS,

  /* GM[8], Shogi. */
  GAME_SHOGI,

  /* GM[9], Lines of Action. */
  GAME_LINES_OF_ACTION,

  /* GM[10], Ataxx. */
  GAME_ATAXX,

  /* GM[11], Hex. */
  GAME_HEX,

  /* GM[12], Jungle. */
  GAME_JUNGLE,

  /* GM[13], Neutron. */
  GAME_NEUTRON,

  /* GM[14], Philosopher's Football. */
  GAME_PHILOSOPHERS_FOOTBALL,

  /* GM[15], Quadrature. */
  GAME_QUADRATURE,

  /* GM[16], Trax. */
  GAME_TRAX,

  /* GM[17], Tantrix. */
  GAME_TANTRIX,

  /* GM[18], Amazons. */
  GAME_AMAZONS,

  /* GM[19], Octi. */
  GAME_OCTI,

  /* GM[20], Gess. */
  GAME_GESS,
  LAST_GAME = GAME_GESS
} Game;


#endif /* QUARRY_GAMES_H */
