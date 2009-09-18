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


#ifndef QUARRY_SGF_PRIVATES_H
#define QUARRY_SGF_PRIVATES_H


#include "sgf.h"
#include "sgf-properties.h"
#include "sgf-parser.h"
#include "sgf-writer.h"
#include "sgf-errors.h"
#include "quarry.h"


#define COLLECTION_DO_NOTIFY(collection)				\
  do {									\
    if ((collection)->notification_callback)				\
      (collection)->notification_callback ((collection),		\
					   (collection)->user_data);	\
  } while (0)

#define GAME_TREE_DO_NOTIFY(tree, notification_code)			\
  do {									\
    if ((tree)->notification_callback)					\
      (tree)->notification_callback ((tree), (notification_code),	\
				     (tree)->user_data);		\
  } while (0)

#define UNDO_HISTORY_DO_NOTIFY(history)					\
  do {									\
    if ((history)->notification_callback)				\
      (history)->notification_callback ((history),			\
					(history)->user_data);		\
  } while (0)


typedef struct _SgfPropertyInfo	SgfPropertyInfo;

struct _SgfPropertyInfo {
#if !SGF_LONG_NAMES
  char		   name[4];
#else
  char		  *name;
#endif

  SgfValueType	   value_type;

  SgfError (* value_parser) (SgfParsingData *data);
  void (* value_writer)	    (SgfWritingData *data, const SgfValue *value);
};


/* Information about each property type. */
extern const SgfPropertyInfo	property_info[];


/* Defined in `sgf-tree.c' and is only used from `sgf-utils.c'. */
void		sgf_property_free_value (SgfValueType value_type,
					 SgfValue *value);

/* Defined in `sgf-utils.c', but also used from `sgf-undo.c'. */
inline void	sgf_utils_do_switch_to_given_node (SgfGameTree *tree,
						   SgfNode *node);
void		sgf_utils_descend_nodes (SgfGameTree *tree, int num_nodes);
void		sgf_utils_ascend_nodes (SgfGameTree *tree, int num_nodes);


#endif /* QUARRY_SGF_PRIVATES_H */


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
