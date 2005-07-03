/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This file is part of Quarry.                                    *
 *                                                                 *
 * Copyright (C) 2005 Paul Pogonyshev.                             *
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


#ifndef QUARRY_SGF_UNDO_H
#define QUARRY_SGF_UNDO_H


#include "sgf.h"
#include "sgf-undo-operations.h"
#include "quarry.h"


typedef struct _SgfUndoOperationInfo	SgfUndoOperationInfo;

struct _SgfUndoOperationInfo {
  void (* undo) (SgfUndoHistoryEntry *entry, SgfGameTree *tree);
  void (* redo) (SgfUndoHistoryEntry *entry, SgfGameTree *tree);

  void (* free_data) (SgfUndoHistoryEntry *entry, int is_applied,
		      SgfGameTree *tree);
};


struct _SgfUndoHistoryEntry {
  SgfUndoHistoryEntry  *next;
  SgfUndoHistoryEntry  *previous;

  unsigned char		operation_index;
  char			is_last_in_action;
};


/* Undo operations. */
extern const SgfUndoOperationInfo    sgf_undo_operations[];


void		sgf_undo_history_delete_dont_free_data
		  (SgfUndoHistory *history);


#define DECLARE_UNDO_OR_REDO_FUNCTION(name)				\
  void		name (SgfUndoHistoryEntry *entry, SgfGameTree *tree);

#define DECLARE_FREE_DATA_FUNCTION(name)				\
  void		name (SgfUndoHistoryEntry *entry, int is_applied,	\
		      SgfGameTree *tree);


/* Used both for node adding and removing undo/redo. */
DECLARE_UNDO_OR_REDO_FUNCTION (sgf_operation_add_node);
DECLARE_UNDO_OR_REDO_FUNCTION (sgf_operation_delete_node);

DECLARE_FREE_DATA_FUNCTION (sgf_operation_new_node_free_data);

DECLARE_FREE_DATA_FUNCTION (sgf_operation_delete_node_free_data);

DECLARE_UNDO_OR_REDO_FUNCTION (sgf_operation_delete_node_children_undo);
DECLARE_UNDO_OR_REDO_FUNCTION (sgf_operation_delete_node_children_redo);
DECLARE_FREE_DATA_FUNCTION (sgf_operation_delete_node_children_free_data);

DECLARE_UNDO_OR_REDO_FUNCTION (sgf_operation_change_node_move_color_do_change);

/* Used both for node adding and removing undo/redo. */
DECLARE_UNDO_OR_REDO_FUNCTION (sgf_operation_add_property);
DECLARE_UNDO_OR_REDO_FUNCTION (sgf_operation_delete_property);

DECLARE_FREE_DATA_FUNCTION (sgf_operation_new_property_free_data);
DECLARE_FREE_DATA_FUNCTION (sgf_operation_delete_property_free_data);

/* Used as both undo and redo handler. */
DECLARE_UNDO_OR_REDO_FUNCTION (sgf_operation_change_property_do_change);

DECLARE_FREE_DATA_FUNCTION (sgf_operation_change_property_free_data);


#if SGF_REAL_VALUES_ALLOCATED_SEPARATELY

/* Used as both undo and redo handler. */
DECLARE_UNDO_OR_REDO_FUNCTION (sgf_operation_change_real_property_do_change);

#endif


#endif /* QUARRY_SGF_UNDO_H */


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
