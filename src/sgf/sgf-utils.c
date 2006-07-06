/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This file is part of Quarry.                                    *
 *                                                                 *
 * Copyright (C) 2003, 2004, 2005, 2006 Paul Pogonyshev.           *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License as  *
 * published by the Free Software Foundation; either version 2 of  *
 * the License, or (at your option) any later version.             *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details.                    *
 *                                                                 *
 * You should have received a copy of the GNU General Public       *
 * License along with this program; if not, write to the Free      *
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,     *
 * Boston, MA 02110-1301, USA.                                     *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include "sgf.h"
#include "sgf-privates.h"
#include "sgf-undo.h"
#include "board.h"
#include "game-info.h"
#include "utils.h"

#include <assert.h>
#include <math.h>

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif


typedef int (* ValuesComparator) (const void *first_value,
				  const void *second_value);


static void	add_horizontal_coordinates (const SgfGameTree *tree,
					    StringBuffer *buffer);
static void	add_horizontal_line (const SgfGameTree *tree,
				     StringBuffer *buffer, int sensei_style);

static void	do_enter_tree (SgfGameTree *tree, SgfNode *down_to);

static void	find_board_state_data (const SgfGameTree *tree,
				       SgfNode *node,
				       int need_color_to_move,
				       int need_move_coordinates);
static void	find_time_control_data (const SgfGameTree *tree,
					SgfNode *upper_limit,
					SgfNode *lower_limit);
static void	determine_final_color_to_play (const SgfGameTree *tree);

static int	do_set_pointer_property (SgfNode *node, SgfGameTree *tree,
					 SgfType type,
					 ValuesComparator values_are_equal,
					 void *new_value, int side_effect);
static int	strings_are_equal (const void *first_string,
				   const void *second_string);


inline void
sgf_utils_play_node_move (const SgfNode *node, Board *board)
{
  assert (node);

  if (board->game != GAME_AMAZONS) {
    board_play_move (board, node->move_color,
		     node->move_point.x, node->move_point.y);
  }
  else {
    board_play_move (board, node->move_color,
		     node->move_point.x, node->move_point.y,
		     node->data.amazons);
  }
}


void
sgf_utils_format_node_move (const SgfGameTree *tree, const SgfNode *node,
			    StringBuffer *buffer,
			    const char *black_string, const char *white_string,
			    const char *pass_string)
{
  assert (tree);
  assert (node);
  assert (buffer);
  assert (IS_STONE (node->move_color));

  if (node->move_color == BLACK) {
    if (black_string)
      string_buffer_cat_string (buffer, black_string);
  }
  else {
    if (white_string)
      string_buffer_cat_string (buffer, white_string);
  }

  if (tree->game != GAME_AMAZONS) {
    if (tree->game != GAME_GO
	|| !IS_PASS (node->move_point.x, node->move_point.y)) {
      game_format_move (tree->game, tree->board_width, tree->board_height,
			buffer, node->move_point.x, node->move_point.y);
    }
    else if (pass_string)
      string_buffer_cat_string (buffer, pass_string);
  }
  else {
    game_format_move (tree->game, tree->board_width, tree->board_height,
		      buffer, node->move_point.x, node->move_point.y,
		      node->data.amazons);
  }
}


void
sgf_utils_enter_tree (SgfGameTree *tree, Board *board,
		      SgfBoardState *board_state)
{
  assert (tree);
  assert (tree->root);
  assert (tree->current_node);
  assert (board_state);

  tree->board	    = board;
  tree->board_state = board_state;

  do_enter_tree (tree, tree->current_node);
}


/* Go down (forward) in the given tree by `num_nodes' nodes, always
 * following the current variation.  If variation end (a node without
 * children) is reached, the function stops gracefully.  Negative
 * values of `num_nodes' are allowed and will cause the function to go
 * up to the variation end.
 */
void
sgf_utils_go_down_in_tree (SgfGameTree *tree, int num_nodes)
{
  assert (tree);
  assert (tree->current_node);
  assert (tree->board_state);

  if (num_nodes != 0 && tree->current_node->child) {
    GAME_TREE_DO_NOTIFY (tree, SGF_ABOUT_TO_CHANGE_CURRENT_NODE);
    sgf_utils_descend_nodes (tree, num_nodes);
    GAME_TREE_DO_NOTIFY (tree, SGF_CURRENT_NODE_CHANGED);
  }
}


/* Go up (back) in the given tree by `num_nodes' nodes, but stop if
 * tree root is reached.  Negative values are allowed and will cause
 * the function to go up to the root.
 *
 * As a speed optimization for very deep trees, ascending to root node
 * is done with sgf_utils_enter_tree() instead of undoing from the
 * current node.
 */
void
sgf_utils_go_up_in_tree (SgfGameTree *tree, int num_nodes)
{
  assert (tree);
  assert (tree->current_node);
  assert (tree->board_state);

  if (num_nodes != 0 && tree->current_node->parent) {
    GAME_TREE_DO_NOTIFY (tree, SGF_ABOUT_TO_CHANGE_CURRENT_NODE);

    if (num_nodes > 0)
      sgf_utils_ascend_nodes (tree, num_nodes);
    else
      do_enter_tree (tree, tree->root);

    GAME_TREE_DO_NOTIFY (tree, SGF_CURRENT_NODE_CHANGED);
  }
}


int
sgf_utils_count_variations (const SgfGameTree *tree, int of_current_node,
			    int black_variations[BOARD_GRID_SIZE],
			    int white_variations[BOARD_GRID_SIZE],
			    int *other_variations)
{
  SgfNode *node;
  int total_variations = 0;

  assert (tree);
  assert (tree->current_node);

  if (black_variations)
    board_fill_int_grid (tree->board, black_variations, 0);
  if (white_variations)
    board_fill_int_grid (tree->board, white_variations, 0);

  node = tree->current_node;
  if (of_current_node) {
    if (node->parent)
      node = node->parent->child;
  }
  else
    node = node->child;

  while (node) {
    if (IS_STONE (node->move_color)
	&& !IS_PASS (node->move_point.x, node->move_point.y)) {
      if (node->move_color == BLACK) {
	if (black_variations)
	  black_variations[POINT_TO_POSITION (node->move_point)]++;
      }
      else {
	if (white_variations)
	  white_variations[POINT_TO_POSITION (node->move_point)]++;
      }
    }
    else if (other_variations)
      (*other_variations)++;

    total_variations++;
    node = node->next;
  }

  return total_variations;
}


SgfNode *
sgf_utils_find_variation_at_position (SgfGameTree *tree, int x, int y,
				      SgfDirection direction,
				      int after_current)
{
  SgfNode *node;

  assert (tree);
  assert (tree->current_node);
  assert (direction == SGF_NEXT || direction == SGF_PREVIOUS);

  node = tree->current_node;

  if (direction == SGF_NEXT) {
    if (!after_current) {
      if (!node->parent)
	return NULL;

      node = node->parent->child;
    }
    else
      node = node->next;

    while (node) {
      if (IS_STONE (node->move_color)
	  && node->move_point.x == x && node->move_point.y == y)
	break;

      node = node->next;
    }

    return node;
  }
  else {
    SgfNode *result = NULL;
    SgfNode *limit;

    limit = (after_current ? tree->current_node : NULL);
    if (node->parent)
      node = node->parent->child;

    while (node != limit) {
      if (IS_STONE (node->move_color)
	  && node->move_point.x == x && node->move_point.y == y)
	result = node;

      node = node->next;
    }

    return result;
  }
}


void
sgf_utils_switch_to_variation (SgfGameTree *tree, SgfDirection direction)
{
  SgfNode *switch_to;
  SgfNode *parent;

  assert (tree);
  assert (tree->current_node);
  assert (tree->board_state);
  assert (direction == SGF_NEXT || direction == SGF_PREVIOUS);

  parent = tree->current_node->parent;

  if (direction == SGF_NEXT) {
    switch_to = tree->current_node->next;
    if (!switch_to)
      return;
  }
  else {
    if (parent && parent->child != tree->current_node) {
      switch_to = parent->child;
      while (switch_to->next != tree->current_node)
	switch_to = switch_to->next;
    }
    else
      return;
  }

  GAME_TREE_DO_NOTIFY (tree, SGF_ABOUT_TO_CHANGE_CURRENT_NODE);

  sgf_utils_ascend_nodes (tree, 1);

  parent->current_variation = switch_to;
  sgf_utils_descend_nodes (tree, 1);

  GAME_TREE_DO_NOTIFY (tree, SGF_CURRENT_NODE_CHANGED);
}


void
sgf_utils_switch_to_given_variation (SgfGameTree *tree, SgfNode *node)
{
  assert (tree);
  assert (tree->current_node);
  assert (node);
  assert (tree->current_node->parent == node->parent);
  assert (tree->board_state);

  if (tree->current_node != node) {
    GAME_TREE_DO_NOTIFY (tree, SGF_ABOUT_TO_CHANGE_CURRENT_NODE);

    sgf_utils_ascend_nodes (tree, 1);

    node->parent->current_variation = node;
    sgf_utils_descend_nodes (tree, 1);

    GAME_TREE_DO_NOTIFY (tree, SGF_CURRENT_NODE_CHANGED);
  }
}


void
sgf_utils_switch_to_given_node (SgfGameTree *tree, SgfNode *node)
{
  assert (tree);
  assert (tree->current_node);
  assert (node);
  assert (tree->board_state);

  if (tree->current_node != node) {
    GAME_TREE_DO_NOTIFY (tree, SGF_ABOUT_TO_CHANGE_CURRENT_NODE);

    sgf_utils_do_switch_to_given_node (tree, node);

    GAME_TREE_DO_NOTIFY (tree, SGF_CURRENT_NODE_CHANGED);
  }
}


/* Append a variation to the current node.  The new variation may
 * contain a move or it may not.  If `color' is either `BLACK' or
 * `WHITE', arguments after it should specify a move according to
 * tree's game.  If `color' is `EMPTY', then the new variation will
 * not contain a move.
 */
SgfNode *
sgf_utils_append_variation (SgfGameTree *tree, int color, ...)
{
  SgfNode *new_node;

  assert (tree);
  assert (tree->current_node);
  assert (tree->board_state);

  new_node = sgf_node_new (tree, tree->current_node);

  if (color != EMPTY) {
    va_list arguments;

    assert (IS_STONE (color));

    va_start (arguments, color);

    new_node->move_color   = color;
    new_node->move_point.x = va_arg (arguments, int);
    new_node->move_point.y = va_arg (arguments, int);

    if (tree->game == GAME_AMAZONS)
      new_node->data.amazons = va_arg (arguments, BoardAmazonsMoveData);

    va_end (arguments);
  }

  sgf_utils_begin_action (tree);
  sgf_utils_apply_undo_history_entry
    (tree, sgf_new_node_undo_history_entry_new (new_node));
  sgf_utils_end_action (tree);

  return new_node;
}


int
sgf_utils_apply_setup_changes (SgfGameTree *tree,
			       const char grid[BOARD_GRID_SIZE], int side_effect)
{
  BoardPositionList *difference_lists[NUM_ON_GRID_VALUES];
  SgfNode *node;
  SgfUndoHistory *undo_history = NULL;
  int have_any_difference;
  int new_move_color;
  int created_new_node = 0;
  int anything_changed = 0;

  assert (tree);
  assert (tree->current_node);
  assert (tree->board);
  assert (grid);

  node = tree->current_node;
  if (node->move_color == SETUP_NODE) {
    if (node->parent)
      sgf_utils_ascend_nodes (tree, 1);
    else
      board_undo (tree->board, 1);
  }

  grid_diff (tree->board->grid, grid, tree->board_width, tree->board_height,
	     difference_lists);
  assert (!difference_lists[ARROW] || tree->game == GAME_AMAZONS);

  have_any_difference = (difference_lists[EMPTY]
			 || difference_lists[BLACK]
			 || difference_lists[WHITE]
			 || difference_lists[ARROW]);
  new_move_color = (have_any_difference ? SETUP_NODE : EMPTY);

  sgf_utils_begin_action (tree);

  if (IS_STONE (node->move_color)) {
    if (!have_any_difference)
      return 0;

    node	       = sgf_node_new (tree, node);
    node->move_color   = new_move_color;
    created_new_node   = 1;

    /* Don't save property additions in the undo history: just
     * adding/removing the new node is sufficient.
     */
    undo_history       = tree->undo_history;
    tree->undo_history = NULL;
  }
  else if (node->move_color != new_move_color) {
    SgfUndoHistoryEntry *entry
      = (sgf_change_node_inlined_color_undo_history_entry_new
	 (node, SGF_OPERATION_CHANGE_NODE_MOVE_COLOR, new_move_color,
	  side_effect));

    sgf_utils_apply_undo_history_entry (tree, entry);
  }

  anything_changed |= (sgf_utils_set_list_of_point_property
		       (node, tree, SGF_ADD_EMPTY, difference_lists[EMPTY],
			side_effect));
  anything_changed |= (sgf_utils_set_list_of_point_property
		       (node, tree, SGF_ADD_BLACK, difference_lists[BLACK],
			side_effect));
  anything_changed |= (sgf_utils_set_list_of_point_property
		       (node, tree, SGF_ADD_WHITE, difference_lists[WHITE],
			side_effect));
  anything_changed |= (sgf_utils_set_list_of_point_property
		       (node, tree, SGF_ADD_ARROWS, difference_lists[ARROW],
			side_effect));

  if (created_new_node) {
    tree->undo_history = undo_history;
    sgf_utils_apply_undo_history_entry
      (tree, sgf_new_node_undo_history_entry_new (node));
  }
  else {
    if (node->parent)
      sgf_utils_descend_nodes (tree, 1);
    else
      do_enter_tree (tree, node);
  }

  sgf_utils_end_action (tree);

  return anything_changed;
}


int
sgf_utils_apply_markup_changes (SgfGameTree *tree,
				const char markup_grid[BOARD_GRID_SIZE],
				int side_effect)
{
  int num_positions[NUM_SGF_MARKUPS];
  int positions[NUM_SGF_MARKUPS][BOARD_MAX_POSITIONS];
  BoardPositionList *new_markup_lists[NUM_SGF_MARKUPS];
  int k;
  int x;
  int y;
  int anything_changed = 0;

  assert (tree);
  assert (tree->current_node);
  assert (markup_grid);

  for (k = 0; k < NUM_SGF_MARKUPS; k++)
    num_positions[k] = 0;

  for (y = 0; y < tree->board_height; y++) {
    for (x = 0; x < tree->board_width; x++) {
      int pos    = POSITION (x, y);
      int markup = markup_grid[pos];

      if (markup != SGF_MARKUP_NONE) {
	assert (markup < NUM_SGF_MARKUPS);

	positions[markup][num_positions[markup]++] = pos;
      }
    }
  }

  for (k = 0; k < NUM_SGF_MARKUPS; k++) {
    if (num_positions[k] > 0) {
      new_markup_lists[k] = board_position_list_new (positions[k],
						     num_positions[k]);
    }
    else
      new_markup_lists[k] = NULL;
  }

  sgf_utils_begin_action (tree);

  anything_changed |= (sgf_utils_set_list_of_point_property
		       (tree->current_node, tree, SGF_MARK,
			new_markup_lists[SGF_MARKUP_CROSS], side_effect));
  anything_changed |= (sgf_utils_set_list_of_point_property
		       (tree->current_node, tree, SGF_CIRCLE,
			new_markup_lists[SGF_MARKUP_CIRCLE], side_effect));
  anything_changed |= (sgf_utils_set_list_of_point_property
		       (tree->current_node, tree, SGF_SQUARE,
			new_markup_lists[SGF_MARKUP_SQUARE], side_effect));
  anything_changed |= (sgf_utils_set_list_of_point_property
		       (tree->current_node, tree, SGF_TRIANGLE,
			new_markup_lists[SGF_MARKUP_TRIANGLE], side_effect));
  anything_changed |= (sgf_utils_set_list_of_point_property
		       (tree->current_node, tree, SGF_SELECTED,
			new_markup_lists[SGF_MARKUP_SELECTED], side_effect));

  sgf_utils_end_action (tree);

  return anything_changed;
}


int
sgf_utils_set_none_property (SgfNode *node, SgfGameTree *tree, SgfType type,
			     int side_effect)
{
  SgfProperty **link;
  SgfUndoHistoryEntry *entry;

  assert (node);
  assert (tree);
  assert (property_info[type].value_type == SGF_NONE);

  if (sgf_node_find_property (node, type, &link))
    return 0;

  sgf_utils_begin_action (tree);

  entry = sgf_new_property_undo_history_entry_new (tree, node, link, type,
						   side_effect);
  sgf_utils_apply_undo_history_entry (tree, entry);

  sgf_utils_end_action (tree);

  return 1;
}


int
sgf_utils_set_number_property (SgfNode *node, SgfGameTree *tree, SgfType type,
			       int number, int side_effect)
{
  SgfProperty **link;
  SgfUndoHistoryEntry *entry;

  assert (node);
  assert (tree);

  if (type != SGF_TO_PLAY) {
    assert (property_info[type].value_type == SGF_NUMBER
	    || property_info[type].value_type == SGF_DOUBLE
	    || property_info[type].value_type == SGF_COLOR);

    if (sgf_node_find_property (node, type, &link)) {
      if ((*link)->value.number == number)
	return 0;

      entry = sgf_change_property_undo_history_entry_new (node, *link,
							  side_effect);
      ((SgfChangePropertyOperationEntry *) entry)->value.number = number;
    }
    else {
      entry = sgf_new_property_undo_history_entry_new (tree, node, link, type,
						       side_effect);
      ((SgfPropertyOperationEntry *) entry)->property->value.number = number;
    }
  }
  else {
    if (node->to_play_color == number)
      return 0;

    assert (number == EMPTY
	    || (!IS_STONE (node->move_color) && IS_STONE (number)));

    entry = (sgf_change_node_inlined_color_undo_history_entry_new
	     (node, SGF_OPERATION_CHANGE_NODE_TO_PLAY_COLOR, number,
	      side_effect));
  }

  sgf_utils_begin_action (tree);
  sgf_utils_apply_undo_history_entry (tree, entry);
  sgf_utils_end_action (tree);

  return 1;
}


int
sgf_utils_set_real_property (SgfNode *node, SgfGameTree *tree, SgfType type,
			     double value, int side_effect)
{
  SgfProperty **link;
  SgfUndoHistoryEntry *entry;

  assert (node);
  assert (tree);
  assert (property_info[type].value_type == SGF_REAL);

  if (sgf_node_find_property (node, type, &link)) {
#if SGF_REAL_VALUES_ALLOCATED_SEPARATELY

    if (* (*link)->value.real == value)
      return 0;

    entry = sgf_change_real_property_undo_history_entry_new (node, *link,
							     value,
							     side_effect);

#else /* not SGF_REAL_VALUES_ALLOCATED_SEPARATELY */

    if ((*link)->value.real == value)
      return 0;

    entry = sgf_change_property_undo_history_entry_new (node, *link,
							side_effect);
    ((SgfChangePropertyOperationEntry *) entry)->value.real = value;

#endif /* not SGF_REAL_VALUES_ALLOCATED_SEPARATELY */
  }
  else {
    entry = sgf_new_property_undo_history_entry_new (tree, node, link, type,
						     side_effect);

#if SGF_REAL_VALUES_ALLOCATED_SEPARATELY
    ((SgfPropertyOperationEntry *) entry)->property->value.real
      = utils_duplicate_buffer (&value, sizeof (double));
#else
    ((SgfPropertyOperationEntry *) entry)->property->value.real = value;
#endif
  }

  sgf_utils_begin_action (tree);
  sgf_utils_apply_undo_history_entry (tree, entry);
  sgf_utils_end_action (tree);

  return 1;
}


int
sgf_utils_set_text_property (SgfNode *node, SgfGameTree *tree, SgfType type,
			     char *text, int side_effect)
{
  assert (property_info[type].value_type == SGF_SIMPLE_TEXT
	  || property_info[type].value_type == SGF_FAKE_SIMPLE_TEXT
	  || property_info[type].value_type == SGF_TEXT);

  return do_set_pointer_property (node, tree, type, strings_are_equal, text,
				  side_effect);
}


int
sgf_utils_set_list_of_point_property (SgfNode *node, SgfGameTree *tree,
				      SgfType type,
				      BoardPositionList *position_list,
				      int side_effect)
{
  assert (property_info[type].value_type == SGF_LIST_OF_POINT
	  || property_info[type].value_type == SGF_ELIST_OF_POINT);

  return do_set_pointer_property (node, tree, type,
				  ((ValuesComparator)
				   board_position_lists_are_equal),
				  position_list, side_effect);
}


int
sgf_utils_set_list_of_label_property (SgfNode *node, SgfGameTree *tree,
				      SgfType type,
				      SgfLabelList *label_list,
				      int side_effect)
{
  assert (property_info[type].value_type == SGF_LIST_OF_LABEL);

  return do_set_pointer_property (node, tree, type,
				  (ValuesComparator) sgf_label_lists_are_equal,
				  label_list, side_effect);
}


/* Add a text property to a given node.  If such a property already
 * exists, don't overwrite its value, but instead append new value to
 * the old one, putting `separator' in between.  If this property
 * doesn't yet exist, the `separator' is not used.  If `separator' is
 * NULL, then "\n\n" is used.
 *
 * Return non-zero if the value is appended (zero if added).
 */
int
sgf_utils_append_text_property (SgfNode *node, SgfGameTree *tree, SgfType type,
				char *text, const char *separator,
				int side_effect)
{
  const char *existing_text;

  assert (node);
  assert (property_info[type].value_type == SGF_SIMPLE_TEXT
	  || property_info[type].value_type == SGF_FAKE_SIMPLE_TEXT
	  || property_info[type].value_type == SGF_TEXT);

  existing_text = sgf_node_get_text_property_value (node, type);

  if (existing_text) {
    char *text_to_free = text;

    text = utils_cat_strings (utils_duplicate_string (existing_text),
			      (separator ? separator : "\n\n"), text, NULL);
    utils_free (text_to_free);
  }

  return do_set_pointer_property (node, tree, type, strings_are_equal, text,
				  side_effect);
}


int
sgf_utils_set_score_result (SgfNode *node, SgfGameTree *tree, double score,
			    int side_effect)
{
  char *result;

  if (score < -0.000005 || 0.000005 < score) {
    char color_who_won = (score > 0.0 ? 'B' : 'W');

    if (tree->game == GAME_GO
	&& fabs (score - floor (score + 0.000005)) >= 0.000005)
      result = utils_cprintf ("%c+%.f", color_who_won, fabs (score));
    else
      result = utils_cprintf ("%c+%d", color_who_won, (int) fabs (score));
  }
  else
    result = utils_duplicate_string ("Draw");

  return do_set_pointer_property (node, tree, SGF_RESULT,
				  strings_are_equal, result, side_effect);
}


int
sgf_utils_set_time_left (SgfNode *node, SgfGameTree *tree,
			 int color, double time_left, int moves_left,
			 int side_effect)
{
  assert (node);
  assert (tree);
  assert (IS_STONE (color));

  sgf_utils_begin_action (tree);

  sgf_utils_set_real_property (node, tree,
			       (color == BLACK
				? SGF_TIME_LEFT_FOR_BLACK
				: SGF_TIME_LEFT_FOR_WHITE),
			       time_left, side_effect);

  if (moves_left) {
    sgf_utils_set_number_property (node, tree,
				   (color == BLACK
				    ? SGF_MOVES_LEFT_FOR_BLACK
				    : SGF_MOVES_LEFT_FOR_WHITE),
				   moves_left, side_effect);
  }

  sgf_utils_end_action (tree);

  if (tree->board_state && tree->current_node == node) {
    tree->board_state->time_left[COLOR_INDEX (color)]  = time_left;
    tree->board_state->moves_left[COLOR_INDEX (color)] = moves_left;
  }

  return 1;
}


void
sgf_utils_apply_custom_undo_entry
  (SgfGameTree *tree, const SgfCustomUndoHistoryEntryData *entry_data,
   void *user_data, SgfNode *node_to_switch_to)
{
  SgfUndoHistoryEntry *entry;

  assert (tree);
  assert (entry_data);

  entry = sgf_custom_undo_history_entry_new (entry_data, user_data,
					     node_to_switch_to);

  sgf_utils_begin_action (tree);
  sgf_utils_apply_undo_history_entry (tree, entry);
  sgf_utils_end_action (tree);
}


int
sgf_utils_get_node_move_number (const SgfNode *node, const SgfGameTree *tree)
{
  int num_moves;

  assert (node);
  assert (IS_STONE (node->move_color));
  assert (tree);

  for (num_moves = 0; node && node != tree->current_node;
       node = node->parent) {
    int move_number;

    if (sgf_node_get_number_property_value (node, SGF_MOVE_NUMBER,
					    &move_number))
      return move_number + num_moves;

    if (IS_STONE (node->move_color))
      num_moves++;
  }

  if (!node)
    return num_moves;
  else
    return tree->board->move_number + num_moves;
}


int
sgf_utils_get_sequential_move_number (const SgfGameTree *tree)
{
  int move_number;

  assert (tree);
  assert (tree->current_node);
  assert (tree->board);

  if (sgf_node_find_property (tree->current_node, SGF_MOVE_NUMBER, NULL)) {
    if (tree->current_node->parent)
      move_number = board_get_move_number (tree->board, 1);
    else
      move_number = 0;

    if (IS_STONE (tree->current_node->move_color))
      move_number++;
  }
  else
    move_number = tree->board->move_number;

  return move_number;
}


int
sgf_utils_determine_player_to_move_by_rules (const SgfGameTree *tree)
{
  int color_to_play_copy;
  int sgf_color_to_play_copy;
  int color_to_play_by_rules;

  assert (tree);
  assert (tree->current_node);

  color_to_play_copy	 = tree->board_state->color_to_play;
  sgf_color_to_play_copy = tree->board_state->sgf_color_to_play;

  find_board_state_data (tree, tree->current_node, 1, 0);
  determine_final_color_to_play (tree);

  color_to_play_by_rules = tree->board_state->color_to_play;

  tree->board_state->color_to_play     = color_to_play_copy;
  tree->board_state->sgf_color_to_play = sgf_color_to_play_copy;

  return color_to_play_by_rules;
}


void
sgf_utils_find_board_state_data (const SgfGameTree *tree,
				 int need_color_to_move,
				 int need_move_coordinates)
{
  assert (tree);
  assert (tree->current_node);

  find_board_state_data (tree, tree->current_node,
			 need_color_to_move, need_move_coordinates);
}


void
sgf_utils_find_time_control_data (const SgfGameTree *tree)
{
  assert (tree);
  assert (tree->current_node);

  find_time_control_data (tree, tree->root, tree->current_node);
}


void
sgf_utils_delete_current_node (SgfGameTree *tree)
{
  SgfUndoHistoryEntry *entry;

  assert (tree);
  assert (tree->current_node);
  assert (tree->current_node->parent);
  assert (tree->board_state);

  entry = sgf_delete_node_undo_history_entry_new (tree->current_node);

  sgf_utils_begin_action (tree);
  sgf_utils_apply_undo_history_entry (tree, entry);
  sgf_utils_end_action (tree);
}


void
sgf_utils_delete_current_node_children (SgfGameTree *tree)
{
  SgfUndoHistoryEntry *entry;

  assert (tree);
  assert (tree->current_node);
  assert (tree->board_state);

  /* Is there anything to delete at all? */
  if (!tree->current_node->child)
    return;

  entry = sgf_delete_node_children_undo_history_entry_new (tree->current_node);

  sgf_utils_begin_action (tree);
  sgf_utils_apply_undo_history_entry (tree, entry);
  sgf_utils_end_action (tree);
}


void
sgf_utils_swap_current_node_with (SgfGameTree *tree, SgfNode *swap_with)
{
  SgfUndoHistoryEntry *entry;

  assert (tree);
  assert (tree->current_node);
  assert (swap_with);
  assert (tree->current_node != swap_with);
  assert (tree->current_node->parent == swap_with->parent);

  entry = sgf_swap_nodes_undo_history_entry_new (tree->current_node,
						 swap_with);
  sgf_utils_begin_action (tree);
  sgf_utils_apply_undo_history_entry (tree, entry);
  sgf_utils_end_action (tree);
}


int
sgf_utils_delete_property (SgfNode *node, SgfGameTree *tree, SgfType type,
			   int side_effect)
{
  SgfProperty **link;

  assert (node);
  assert (tree);

  if (sgf_node_find_property (node, type, &link)) {
    SgfUndoHistoryEntry *entry
      = sgf_delete_property_undo_history_entry_new (node, *link, side_effect);

    sgf_utils_begin_action (tree);
    sgf_utils_apply_undo_history_entry (tree, entry);
    sgf_utils_end_action (tree);

    return 1;
  }

  return 0;
}


void
sgf_utils_set_node_is_collapsed (SgfGameTree *tree, SgfNode *node,
				 int is_collapsed)
{
  assert (tree);
  assert (node);

  if ((is_collapsed && !node->is_collapsed)
      || (!is_collapsed && node->is_collapsed)) {
    GAME_TREE_DO_NOTIFY (tree, SGF_ABOUT_TO_MODIFY_MAP);

    node->is_collapsed = (is_collapsed ? 1 : 0);
    sgf_game_tree_invalidate_map (tree, node);

    GAME_TREE_DO_NOTIFY (tree, SGF_MAP_MODIFIED);
  }
}


void
sgf_utils_get_markup (const SgfGameTree *tree, char markup[BOARD_GRID_SIZE])
{
  SgfNode *node;
  const BoardPositionList *position_list;
  int k;

  assert (tree);
  assert (tree->current_node);
  assert (markup);

  node = tree->current_node;

  board_fill_grid (tree->board, markup, SGF_MARKUP_NONE);

  position_list = sgf_node_get_list_of_point_property_value (node, SGF_MARK);
  if (position_list) {
    for (k = 0; k < position_list->num_positions; k++)
      markup[position_list->positions[k]] = SGF_MARKUP_CROSS;
  }

  position_list = sgf_node_get_list_of_point_property_value (node, SGF_CIRCLE);
  if (position_list) {
    for (k = 0; k < position_list->num_positions; k++)
      markup[position_list->positions[k]] = SGF_MARKUP_CIRCLE;
  }

  position_list = sgf_node_get_list_of_point_property_value (node, SGF_SQUARE);
  if (position_list) {
    for (k = 0; k < position_list->num_positions; k++)
      markup[position_list->positions[k]] = SGF_MARKUP_SQUARE;
  }

  position_list = sgf_node_get_list_of_point_property_value (node,
							     SGF_TRIANGLE);
  if (position_list) {
    for (k = 0; k < position_list->num_positions; k++)
      markup[position_list->positions[k]] = SGF_MARKUP_TRIANGLE;
  }

  position_list = sgf_node_get_list_of_point_property_value (node,
							     SGF_SELECTED);
  if (position_list) {
    for (k = 0; k < position_list->num_positions; k++)
      markup[position_list->positions[k]] = SGF_MARKUP_SELECTED;
  }
}


void
sgf_utils_mark_territory_on_grid (const SgfGameTree *tree,
				  char grid[BOARD_GRID_SIZE],
				  char black_territory_mark,
				  char white_territory_mark)
{
  assert (tree);
  assert (tree->board);
  assert (tree->current_node);
  assert (grid);

  if (tree->game == GAME_GO) {
    const BoardPositionList *territory_list;
    int k;

    territory_list
      = sgf_node_get_list_of_point_property_value (tree->current_node,
						   SGF_BLACK_TERRITORY);
    if (territory_list) {
      for (k = 0; k < territory_list->num_positions; k++) {
	if (tree->board->grid[territory_list->positions[k]] != BLACK)
	  grid[territory_list->positions[k]] = black_territory_mark;
      }
    }

    territory_list
      = sgf_node_get_list_of_point_property_value (tree->current_node,
						   SGF_WHITE_TERRITORY);
    if (territory_list) {
      for (k = 0; k < territory_list->num_positions; k++) {
	if (tree->board->grid[territory_list->positions[k]] != WHITE)
	  grid[territory_list->positions[k]] = white_territory_mark;
      }
    }
  }
}


/* Set handicap in the `tree's current node (normally root).  If fixed
 * handicap is requested, the function also adds `AB' property with
 * required number of handicap stones.
 */
void
sgf_utils_set_handicap (SgfGameTree *tree, int handicap, int is_fixed)
{
  assert (tree);
  assert (tree->game == GAME_GO);
  assert (tree->current_node);
  assert (handicap == 0 || handicap > 1);
  assert (!IS_STONE (tree->current_node->move_color));

  if (is_fixed) {
    if (handicap != 0) {
      BoardPositionList *handicap_stones
	= go_get_fixed_handicap_stones (tree->board_width, tree->board_height,
					handicap);

      tree->current_node->move_color = SETUP_NODE;
      sgf_node_add_list_of_point_property (tree->current_node, tree,
					   SGF_ADD_BLACK, handicap_stones, 0);
    }
  }
  else
    assert (handicap < tree->board_width * tree->board_height);

  sgf_node_add_text_property (tree->current_node, tree, SGF_HANDICAP,
			      utils_cprintf ("%d", handicap), 1);
}


void
sgf_utils_add_free_handicap_stones (SgfGameTree *tree,
				    BoardPositionList *handicap_stones)
{
  int handicap;

  assert (tree);
  assert (tree->current_node);
  assert (handicap_stones);

  handicap = sgf_node_get_handicap (tree->current_node);
  assert (0 < handicap_stones->num_positions
	  && handicap_stones->num_positions <= handicap);

  tree->current_node->move_color = SETUP_NODE;
  sgf_node_add_list_of_point_property (tree->current_node, tree,
				       SGF_ADD_BLACK, handicap_stones, 0);
}




/* Normalize text for an SGF property.
 *
 * For simple text: convert '\t' into appropriate number of spaces,
 * replace '\n', '\v' and '\f' characters with one space, remove all
 * '\r' characters and trim both leading and trailing whitespace.
 *
 * For other (multiline) text: convert '\t' into appropriate number of
 * spaces,. convert '\v' and '\f' into a few linefeeds, remove all
 * '\r' characters and, finally, trim trailing whitespace.
 */
char *
sgf_utils_normalize_text (const char *text, int is_simple_text)
{
  const char *normalized_up_to;
  int current_column = 0;
  StringBuffer buffer;
  char *scan;

  assert (text);

  if (is_simple_text) {
    while (*text == ' ' || *text == '\t'
	   || *text == '\n' || *text == '\r'
	   || *text == '\v' || *text == '\f') {
      if (*text != '\r') {
	current_column++;
	if (*text == '\t')
	  current_column = ROUND_UP (current_column, 8);
      }

      text++;
    }
  }

  if (! *text)
    return NULL;

  string_buffer_init (&buffer, 0x1000, 0x1000);

  for (normalized_up_to = text; *text; text++) {
    if (IS_UTF8_STARTER (*text)) {
      if (*text != '\n' || is_simple_text) {
	current_column++;

	if (*text == '\t' || *text == '\v' || *text == '\f'
	    || *text == '\n' || *text == '\r') {
	  string_buffer_cat_as_string (&buffer, normalized_up_to,
				       text - normalized_up_to);
	  normalized_up_to = text + 1;

	  if (*text == '\t') {
	    int tab_to_column = ROUND_UP (current_column, 8);

	    string_buffer_add_characters (&buffer, ' ',
					  tab_to_column - current_column);
	    current_column = tab_to_column;
	  }
	  else if (*text != '\r') {
	    if (is_simple_text) {
	      /* Convert all whitespace into a single space
	       * character.
	       */
	      string_buffer_add_character (&buffer, ' ');
	    }
	    else {
	      /* This is hardly important.  Insert a few empty
	       * lines.
	       */
	      string_buffer_add_characters (&buffer, '\n', 4);
	      current_column = 0;
	    }
	  }
	  else {
	    /* Remove. */
	    current_column--;
	  }
	}
      }
      else
	current_column = 0;
    }
  }

  string_buffer_cat_as_string (&buffer,
			       normalized_up_to, text - normalized_up_to);

  /* Trim end whitespace. */
  for (scan = buffer.string + buffer.length; ; scan--) {
    if (scan == buffer.string) {
      string_buffer_dispose (&buffer);
      return NULL;
    }

    if (*(scan - 1) != ' ' && *(scan - 1) != '\n')
      break;
  }

  *scan = 0;
  return utils_realloc (buffer.string, (scan + 1) - buffer.string);
}


char *
sgf_utils_export_position_as_ascii (const SgfGameTree *tree)
{
  StringBuffer result;
  BoardPoint hoshi_points[9];
  int num_hoshi_points = 0;
  int x;
  int y;

  assert (tree);
  assert (tree->board);
  assert (tree->board_state);

  if (tree->game == GAME_GO) {
    num_hoshi_points = go_get_hoshi_points (tree->board_width,
					    tree->board_height,
					    hoshi_points);
  }

  string_buffer_init (&result, 0x400, 0x200);

  add_horizontal_coordinates (tree, &result);
  add_horizontal_line (tree, &result, 0);

  for (y = 0; y < tree->board_height; y++) {
    int vertical_coordinate
      = (game_info[tree->game].reversed_vertical_coordinates
	 ? tree->board_height - y : y + 1);

    string_buffer_printf (&result, "%*d |",
			  tree->board_height < 10 ? 1 : 2,
			  vertical_coordinate);

    if (tree->board_state->last_move_x != 0
	|| tree->board_state->last_move_y != y)
      string_buffer_add_character (&result, ' ');
    else
      string_buffer_add_character (&result, '(');

    for (x = 0; x < tree->board_width; x++) {
      char character = '.';

      switch (tree->board->grid[POSITION (x, y)]) {
      case EMPTY:
	{
	  int k;

	  for (k = 0; k < num_hoshi_points; k++) {
	    if (x == hoshi_points[k].x && y == hoshi_points[k].y) {
	      character = '+';
	      break;
	    }
	  }
	}

	break;

      case BLACK:
	character = 'X';
	break;

      case WHITE:
	character = 'O';
	break;

      case SPECIAL_ON_GRID_VALUE:
	character = '=';
	break;

      default:
	assert (0);
      }

      string_buffer_add_character (&result, character);

      if ((tree->board_state->last_move_x != x
	   && tree->board_state->last_move_x != x + 1)
	  || tree->board_state->last_move_y != y)
	string_buffer_add_character (&result, ' ');
      else {
	if (tree->board_state->last_move_x == x)
	  string_buffer_add_character (&result, ')');
	else
	  string_buffer_add_character (&result, '(');
      }
    }

    string_buffer_printf (&result, "| %-*d\n",
			  tree->board_height < 10 ? 1 : 2,
			  vertical_coordinate);
  }

  add_horizontal_line (tree, &result, 0);
  add_horizontal_coordinates (tree, &result);

  return string_buffer_steal_string (&result);
}


char *
sgf_utils_export_position_as_senseis_library_diagram (const SgfGameTree *tree)
{
  StringBuffer result;
  const SgfBoardState *board_state;
  const BoardPositionList* circle_mark;
  const BoardPositionList* square_mark;
  const SgfLabelList *labels;
  int x;
  int y;

  assert (tree);
  assert (tree->game == GAME_GO);
  assert (tree->board);
  assert (tree->board_state);
  assert (tree->current_node);

  board_state = tree->board_state;
  circle_mark = sgf_node_get_list_of_point_property_value (tree->current_node,
							   SGF_CIRCLE);
  square_mark = sgf_node_get_list_of_point_property_value (tree->current_node,
							   SGF_SQUARE);
  labels      = sgf_node_get_list_of_label_property_value (tree->current_node,
							   SGF_LABEL);

  string_buffer_init (&result, 0x400, 0x200);

  string_buffer_add_characters (&result, '$', 2);

  if (board_state->last_move_node) {
    string_buffer_add_character
      (&result, board_state->last_move_node->move_color == BLACK ? 'B' : 'W');
  }

  string_buffer_cat_string (&result, " \n");

  add_horizontal_line (tree, &result, 1);

  for (y = 0; y < tree->board_height; y++) {
    string_buffer_cat_string (&result, "$$ |");

    for (x = 0; x < tree->board_width; x++) {
      static const char *characters = ".CSXB#OW@1";

      int pos = POSITION (x, y);
      const char *character;

      string_buffer_add_character (&result, ' ');

      if (tree->board->grid[pos] == EMPTY) {
	if (labels) {
	  BoardPoint point = { x, y };
	  const char *label = sgf_label_list_get_label (labels, point);

	  if (label
	      && (('a' <= label[0] && label[0] <= 'z')
		  || ('A' <= label[0] && label[0] <= 'Z'))
	      && !label[1]) {
	    char label_character = label[0];

	    if ('A' <= label_character && label_character <= 'Z')
	      label_character -= 'A' - 'a';

	    string_buffer_add_character (&result, label_character);
	    continue;
	  }
	}

	character = characters;
      }
      else {
	switch (tree->board->grid[pos]) {
	case BLACK:
	  character = characters + 3;
	  break;

	case WHITE:
	  character = characters + 6;
	  break;

	default:
	  assert (0);
	}
      }

      if (circle_mark
	  && board_position_list_find_position (circle_mark, pos) != -1)
	character += 1;
      else if (square_mark
	       && board_position_list_find_position (square_mark, pos) != -1)
	character += 2;
      else if (IS_STONE (tree->board->grid[pos])
	       && tree->board_state->last_move_x == x
	       && tree->board_state->last_move_y == y)
	character = characters + 9;

      string_buffer_add_character (&result, *character);
    }

    string_buffer_cat_string (&result, " |\n");
  }

  add_horizontal_line (tree, &result, 1);

  return string_buffer_steal_string (&result);
}


static void
add_horizontal_coordinates (const SgfGameTree *tree, StringBuffer *buffer)
{
  int x;

  string_buffer_add_characters (buffer, ' ', tree->board_height < 10 ? 3 : 4);

  for (x = 0; x < tree->board_width; x++) {
    char coordinate_character
      = game_info[tree->game].horizontal_coordinates[x];

    string_buffer_add_character (buffer, ' ');
    string_buffer_add_character (buffer, coordinate_character);
  }

  string_buffer_add_character (buffer, '\n');
}


static void
add_horizontal_line (const SgfGameTree *tree, StringBuffer *buffer,
		     int sensei_style)
{
  if (sensei_style)
    string_buffer_cat_string (buffer, "$$ ");
  else {
    string_buffer_add_characters (buffer, ' ',
				  tree->board_height < 10 ? 2 : 3);
  }

  string_buffer_add_character (buffer, '+');
  string_buffer_add_characters (buffer, '-', 2 * tree->board_width + 1);
  string_buffer_add_character (buffer, '+');
  string_buffer_add_character (buffer, '\n');
}




static void
do_enter_tree (SgfGameTree *tree, SgfNode *down_to)
{
  static SgfNode root_predecessor;
  SgfBoardState *const board_state = tree->board_state;
  SgfNode *node;
  int num_nodes = 1;

  board_set_parameters (tree->board, tree->game,
			tree->board_width, tree->board_height);

  board_state->sgf_color_to_play	= EMPTY;
  board_state->last_move_x		= NULL_X;
  board_state->last_move_y		= NULL_Y;
  board_state->game_info_node		= NULL;
  board_state->last_move_node		= NULL;
  board_state->last_main_variation_node = NULL;

  board_state->time_left[BLACK_INDEX]	= -1.0;
  board_state->time_left[WHITE_INDEX]	= -1.0;
  board_state->moves_left[BLACK_INDEX]	= -1;
  board_state->moves_left[WHITE_INDEX]	= -1;

  for (node = tree->root; node != down_to; node = node->current_variation) {
    assert (node);
    num_nodes++;
  }

  /* Use a fake SGF node so that sgf_utils_descend_nodes() has
   * something to descend from.
   */
  root_predecessor.child	     = tree->root;
  root_predecessor.current_variation = tree->root;
  tree->current_node		     = &root_predecessor;
  tree->current_node_depth	     = -1;

  sgf_utils_descend_nodes (tree, num_nodes);
}


inline void
sgf_utils_do_switch_to_given_node (SgfGameTree *tree, SgfNode *node)
{
  SgfNode *path_scan;

  for (path_scan = node; path_scan->parent; path_scan = path_scan->parent)
    path_scan->parent->current_variation = path_scan;

  do_enter_tree (tree, node);
}


/* Descend in an SGF tree from current node by one or more nodes,
 * always following node's current variation.  Nodes' move or setup
 * properties, whichever are present, are played on tree's associated
 * board and `board_state' is updated as needed.
 */
void
sgf_utils_descend_nodes (SgfGameTree *tree, int num_nodes)
{
  SgfBoardState *const board_state = tree->board_state;
  SgfNode *node = tree->current_node;

  tree->current_node_depth += num_nodes;

  do {
    if (node->current_variation) {
      if (node->current_variation != node->child
	  && !board_state->last_main_variation_node)
	board_state->last_main_variation_node = node;
    }
    else {
      if (!node->child) {
	tree->current_node_depth -= num_nodes;
	break;
      }

      node->current_variation = node->child;
    }

    node = node->current_variation;

    if (!board_state->game_info_node && sgf_node_is_game_info_node (node)) {
      board_state->game_info_node	= node;
      board_state->game_info_node_depth = (tree->current_node_depth
					   - (num_nodes - 1));
    }

    if (IS_STONE (node->move_color)) {
      sgf_utils_play_node_move (node, tree->board);

      board_state->sgf_color_to_play = OTHER_COLOR (node->move_color);
      board_state->last_move_x	     = node->move_point.x;
      board_state->last_move_node    = node;
    }
    else {
      if (node->move_color == SETUP_NODE) {
	const BoardPositionList *position_lists[NUM_ON_GRID_VALUES];

	position_lists[BLACK]
	  = sgf_node_get_list_of_point_property_value (node, SGF_ADD_BLACK);
	position_lists[WHITE]
	  = sgf_node_get_list_of_point_property_value (node, SGF_ADD_WHITE);
	position_lists[EMPTY]
	  = sgf_node_get_list_of_point_property_value (node, SGF_ADD_EMPTY);

	if (tree->game == GAME_AMAZONS) {
	  position_lists[ARROW]
	    = sgf_node_get_list_of_point_property_value (node, SGF_ADD_ARROWS);
	}
	else
	  position_lists[ARROW] = NULL;

	board_apply_changes (tree->board, position_lists);

	board_state->last_move_x = NULL_X;
      }
      else
	board_add_dummy_move_entry (tree->board);

      if (node->to_play_color != EMPTY)
	board_state->sgf_color_to_play = node->to_play_color;
    }

    if (node->move_color != SETUP_NODE) {
      sgf_node_get_number_property_value (node, SGF_MOVE_NUMBER,
					  (int *) &tree->board->move_number);
    }
  } while (--num_nodes != 0);

  if (node != tree->current_node) {
    if (board_state->last_move_x != NULL_X)
      board_state->last_move_y = board_state->last_move_node->move_point.y;
    else
      board_state->last_move_y = NULL_Y;

    find_time_control_data (tree, tree->current_node, node);

    tree->current_node = node;
    determine_final_color_to_play (tree);
  }
}


void
sgf_utils_ascend_nodes (SgfGameTree *tree, int num_nodes)
{
  SgfBoardState *const board_state = tree->board_state;
  SgfNode *node = tree->current_node;
  int k;

  for (k = 0; k < num_nodes && node->parent; k++) {
    node = node->parent;

    if (board_state->last_main_variation_node == node)
      board_state->last_main_variation_node = NULL;
  }

  if (node->parent) {
    board_undo (tree->board, num_nodes);

    find_board_state_data (tree, node, 1, 1);
    find_time_control_data (tree, NULL, node);

    tree->current_node	      = node;
    tree->current_node_depth -= num_nodes;

    if (board_state->game_info_node
	&& board_state->game_info_node_depth > tree->current_node_depth)
      board_state->game_info_node = NULL;

    determine_final_color_to_play (tree);
  }
  else
    do_enter_tree (tree, tree->root);
}


static void
find_board_state_data (const SgfGameTree *tree, SgfNode *node,
		       int need_color_to_move, int need_move_coordinates)
{
  SgfBoardState *const board_state = tree->board_state;

  if (!need_color_to_move && !need_color_to_move)
    return;

  board_state->last_move_node = NULL;
  if (need_color_to_move)
    board_state->sgf_color_to_play = EMPTY;

  while (1) {
    if (IS_STONE (node->move_color)) {
      if (board_state->sgf_color_to_play == EMPTY)
	board_state->sgf_color_to_play = OTHER_COLOR (node->move_color);

      board_state->last_move_node = node;
      if (need_move_coordinates) {
	board_state->last_move_x = node->move_point.x;
	board_state->last_move_y = node->move_point.y;
      }

      break;
    }
    else {
      if (board_state->sgf_color_to_play == EMPTY) {
	/* `PL' is a setup property, so we check only here. */
	board_state->sgf_color_to_play = node->to_play_color;
      }

      if (node->move_color == SETUP_NODE) {
	if (need_move_coordinates) {
	  board_state->last_move_x = NULL_X;
	  board_state->last_move_y = NULL_Y;
	  need_move_coordinates	   = 0;
	}
      }
    }

    node = node->parent;
    if (!node) {
      if (need_move_coordinates) {
	board_state->last_move_x = NULL_X;
	board_state->last_move_y = NULL_Y;
      }

      break;
    }
  }
}


static void
find_time_control_data (const SgfGameTree *tree,
			SgfNode *upper_limit, SgfNode *lower_limit)
{
  SgfBoardState *const board_state = tree->board_state;
  SgfNode *node;

  char have_time_left_for_black  = 0;
  char have_time_left_for_white  = 0;
  char have_moves_left_for_black = 0;
  char have_moves_left_for_white = 0;

  if (!upper_limit) {
    have_time_left_for_black  = (board_state->time_left[BLACK_INDEX] == -1.0);
    have_time_left_for_white  = (board_state->time_left[WHITE_INDEX] == -1.0);
    have_moves_left_for_black = (board_state->moves_left[BLACK_INDEX] == -1);
    have_moves_left_for_white = (board_state->moves_left[WHITE_INDEX] == -1);
  }
  else {
    /* Weird fix for do_enter_tree()'s `root_predecessor'. */
    if (upper_limit->child)
      upper_limit = upper_limit->child->parent;
  }

  node = lower_limit;
  while (!(have_time_left_for_black && have_time_left_for_white
	   && have_moves_left_for_black && have_moves_left_for_white)) {
    if (!have_time_left_for_black) {
      have_time_left_for_black = (sgf_node_get_real_property_value
				  (node, SGF_TIME_LEFT_FOR_BLACK,
				   &board_state->time_left[BLACK_INDEX]));
    }

    if (!have_time_left_for_white) {
      have_time_left_for_white = (sgf_node_get_real_property_value
				  (node, SGF_TIME_LEFT_FOR_WHITE,
				   &board_state->time_left[WHITE_INDEX]));
    }

    if (!have_moves_left_for_black) {
      have_moves_left_for_black = (sgf_node_get_number_property_value
				   (node, SGF_MOVES_LEFT_FOR_BLACK,
				    &board_state->moves_left[BLACK_INDEX]));
    }

    if (!have_moves_left_for_white) {
      have_moves_left_for_white = (sgf_node_get_number_property_value
				   (node, SGF_MOVES_LEFT_FOR_WHITE,
				    &board_state->moves_left[WHITE_INDEX]));
    }

    node = node->parent;
    if (node == upper_limit)
      break;
  }
}


static void
determine_final_color_to_play (const SgfGameTree *tree)
{
  const SgfNode *game_info_node = tree->board_state->game_info_node;

  if (tree->board_state->sgf_color_to_play == EMPTY
      && tree->game == GAME_GO
      && game_info_node) {
    int handicap = sgf_node_get_handicap (game_info_node);

    if (handicap > 0
	&& sgf_node_get_list_of_point_property_value (game_info_node,
						      SGF_ADD_BLACK))
      tree->board_state->sgf_color_to_play = WHITE;
    else if (handicap == 0)
      tree->board_state->sgf_color_to_play = BLACK;
  }

  tree->board_state->color_to_play
    = board_adjust_color_to_play (tree->board, RULE_SET_DEFAULT,
				  tree->board_state->sgf_color_to_play);
}




static int
do_set_pointer_property (SgfNode *node, SgfGameTree *tree, SgfType type,
			 ValuesComparator values_are_equal, void *new_value,
			 int side_effect)
{
  SgfProperty **link;
  SgfUndoHistoryEntry *entry;

  assert (node);
  assert (tree);

  if (sgf_node_find_property (node, type, &link)) {
    if (new_value) {
      if (values_are_equal ((*link)->value.memory_block, new_value)) {
	SgfValue value;

	value.memory_block = new_value;
	sgf_property_free_value (property_info[type].value_type, &value);
	return 0;
      }

      entry = sgf_change_property_undo_history_entry_new (node, *link,
							  side_effect);

      ((SgfChangePropertyOperationEntry *) entry)->value.memory_block
	= new_value;
    }
    else
      entry = sgf_delete_property_undo_history_entry_new (node, *link,
							  side_effect);
  }
  else {
    if (!new_value)
      return 0;

    entry = sgf_new_property_undo_history_entry_new (tree, node, link, type,
						     side_effect);
    ((SgfPropertyOperationEntry *) entry)->property->value.memory_block
      = new_value;
  }

  sgf_utils_begin_action (tree);
  sgf_utils_apply_undo_history_entry (tree, entry);
  sgf_utils_end_action (tree);

  return 1;
}


static int
strings_are_equal (const void *first_string, const void *second_string)
{
  return (strcmp ((const char *) first_string, (const char *) second_string)
	  == 0);
}



/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
