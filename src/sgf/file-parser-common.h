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

#ifndef PARSER-COMMON
#define PARSER-COMMON

#include "sgf.h"
#include "sgf-parser.h"
#include "sgf-errors.h"
#include "sgf-privates.h"
#include "board.h"
#include "game-info.h"
#include "utils.h"

#include <assert.h>
#include <iconv.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif


#define SGF_END			0

/* SGF specification remains silent about escaping in non-text
 * properties, but `SGFC' allows escaping in them.  Function
 * next_token_in_value() provides transparent escaping.  In order to
 * avoid premature end of value parsing, escaped ']' is replaced with
 * the below character, which is never returned by next_character().
 */
#define ESCAPED_BRACKET		'\r'


typedef struct _BufferPositionStorage	BufferPositionStorage;

struct _BufferPositionStorage {
  char		token;
  int		line;
  int		column;
  int		pending_column;
};



#define STORE_BUFFER_POSITION(data, index, storage)			\
  do {									\
    (data)->stored_buffer_pointers[index] = (data)->buffer_pointer;	\
    (storage).token			  = (data)->token;		\
    (storage).line			  = (data)->line;		\
    (storage).column			  = (data)->column;		\
    (storage).pending_column		  = (data)->pending_column;	\
  } while (0)

#define RESTORE_BUFFER_POSITION(data, index, storage)			\
  do {									\
    (data)->buffer_pointer = (data)->stored_buffer_pointers[index];	\
    (data)->token	   = (storage).token;				\
    (data)->line	   = (storage).line;				\
    (data)->column	   = (storage).column;				\
    (data)->pending_column = (storage).pending_column;			\
  } while (0)


#define STORE_ERROR_POSITION(data, storage)				\
  do {									\
    (storage).line   = (data)->line;					\
    (storage).column = (data)->column;					\
    (storage).notch  = (data)->error_list->last;			\
  } while (0)


#endif
/* file-parser-common.h */
