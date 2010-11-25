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

/* UGF parser - much of it gleaned from the old ggo parser code, the
 * rest by looking at files from the IGS mailing list.
 *
 * The parser provides the following features:
 *
 * - UGF version 3 (the only UGF files I have access to)
 *
 * - No limits on any string lengths (be that text property value or
 *   property identifier) except those imposed by memory availability.
 *
 * - Reading of input file in chunks: smaller memory footprint (again,
 *   noticeable on huge files only).
 *
 * - Thread safety; simple progress/cancellation interface (must be
 *   matched with support at higher levels).
 */

/* FIXME: add more comments, especially in tricky/obscure places. */


#include "sgf.h"
#include "file-parser-common.h"
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
#include <stdbool.h>

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif


static int	    parse_ugf_buffer (SgfParsingData *data,
				  SgfCollection **collection,
				  SgfErrorList **error_list,
				  const SgfParserParameters *parameters,
				  int *bytes_parsed,
				  const int *cancellation_flag);

static SgfNode     *init_ugf_tree (SgfParsingData *data, SgfNode *parent);
static SgfError    do_parse_ugf_move (SgfParsingData *data);
static SgfError    ugf_parse_move (SgfParsingData *data);
static SgfNode     *push_ugf_node (SgfParsingData *data, SgfNode *parent);

inline static char *ugf_get_line (SgfParsingData *data, char *existing_text);
inline static void ugf_next_line (SgfParsingData *data);
inline static void ugf_next_token (SgfParsingData *data);
inline static void begin_parsing_ugf_value (SgfParsingData *data);
inline static void ugf_next_token_in_value (SgfParsingData *data);
inline static void ugf_next_character (SgfParsingData *data);
static void        ugf_parse_property (SgfParsingData *data, char *line_contents);
static int         ugf_parse_point (SgfParsingData *data, BoardPoint *point);
static SgfError    ugf_parse_label (SgfParsingData *data, int x, int y, const char *label_string);
static SgfError    ugf_parse_simple_text (SgfParsingData *data);

inline SgfType     get_sgf_property(const char *name);


const SgfParserParameters ugf_parser_defaults = {
  4 * 1024 * 1024, 64 * 1024, 1024 * 1024,
  0
};


/* Read a UGF file and parse the (singular?) game tree it contains.  If the
 * maximum buffer size specified in `parameters' is not enough to keep
 * the whole file in memory, this function sets up data required for
 * refreshing the buffer.
 */
int
ugf_parse_file (const char *filename, SgfCollection **collection,
		SgfErrorList **error_list,
		const SgfParserParameters *parameters,
		int *file_size, int *bytes_parsed,
		const int *cancellation_flag)
{
  int result;
  FILE *file;

  assert (filename);
  assert (collection);
  assert (error_list);
  assert (parameters);

  /* Buffer size parameters should be sane. */
  assert (parameters->max_buffer_size >= 512 * 1024);
  assert (parameters->buffer_size_increment >= 256 * 1024);
  assert (parameters->buffer_refresh_margin >= 1024);
  assert (8 * parameters->buffer_refresh_margin
	  <= parameters->max_buffer_size);

  *collection = NULL;
  *error_list = NULL;
  result = SGF_ERROR_READING_FILE;

  file = fopen (filename, "rb");
  if (file) {
    if (fseek (file, 0, SEEK_END) != -1) {
      SgfParsingData parsing_data;
      int max_buffer_size = ROUND_UP (parameters->max_buffer_size, 4 * 1024);
      int local_file_size;
      int buffer_size;
      char *buffer;

      local_file_size = ftell (file);
      if (local_file_size == 0) {
	fclose (file);
	return SGF_INVALID_FILE;
      }

      if (file_size)
	*file_size = local_file_size;

      rewind (file);

      if (local_file_size <= max_buffer_size)
	buffer_size = local_file_size;
      else
	buffer_size = max_buffer_size;

      buffer = utils_malloc (buffer_size);

      if (fread (buffer, buffer_size, 1, file) == 1) {
	parsing_data.buffer	 = buffer;
	parsing_data.buffer_end	 = buffer + buffer_size;
	parsing_data.buffer_size = buffer_size;

	parsing_data.file_bytes_remaining = local_file_size - buffer_size;

	if (local_file_size <= max_buffer_size)
	  parsing_data.buffer_refresh_point = parsing_data.buffer_end;
	else {
	  parsing_data.buffer_size_increment
	    = ROUND_UP (parameters->buffer_size_increment, 4 * 1024);
	  parsing_data.buffer_refresh_margin
	    = ROUND_UP (parameters->buffer_refresh_margin, 1024);
	  parsing_data.buffer_refresh_point
	    = parsing_data.buffer_end - parsing_data.buffer_refresh_margin;

	  parsing_data.file = file;
	}

	result = parse_ugf_buffer (&parsing_data, collection, error_list,
			       parameters, bytes_parsed, cancellation_flag);
      }

      utils_free (parsing_data.buffer);
    }

    fclose (file);
  }

  return result;
}

/* Get proper SGF property from short name.
 * For aid in translating from other record formats.
*/
inline SgfType
get_sgf_property (const char *name)
{
	char *property_name = name;
	SgfType property_type = 0;
	while (1) {
		property_type = property_tree[property_type][1 + (*property_name - 'A')];
		property_name++;

		if (property_type < SGF_NUM_PROPERTIES) {
			if (strlen(property_name) == 1)
				property_type = SGF_UNKNOWN;
			break;
		}

		property_type -= SGF_NUM_PROPERTIES;
		if (strlen(property_name) == 0) {
			property_type = property_tree[property_type][0];
			break;
		}
	}
	return property_type;
}



/* Parse SGF data in a buffer and creates a game collection from it.
 * In case of errors it returns NULL.
 *
 * Note that the data in buffer is overwritten in process, at least
 * partially.
 */
int
ugf_parse_buffer (char *buffer, int size,
		  SgfCollection **collection, SgfErrorList **error_list,
		  const SgfParserParameters *parameters,
		  int *bytes_parsed, const int *cancellation_flag)
{
  SgfParsingData parsing_data;

  assert (buffer);
  assert (size >= 0);
  /*assert (collection);
  assert (error_list);*/
  assert (parameters);

  parsing_data.buffer = buffer;
  parsing_data.buffer_refresh_point = buffer + size;
  parsing_data.buffer_end = buffer + size;

  parsing_data.file_bytes_remaining = 0;

  return parse_ugf_buffer (&parsing_data, collection, error_list, parameters,
		       bytes_parsed, cancellation_flag);
}


static int
parse_ugf_buffer (SgfParsingData *data,
	      SgfCollection **collection, SgfErrorList **error_list,
	      const SgfParserParameters *parameters,
	      int *bytes_parsed, const int *cancellation_flag)
{
  int dummy_bytes_parsed;
  int dummy_cancellation_flag;
  char *line_contents;

  SgfGameTree *tree = NULL;
  SgfNode *current_node = NULL;

  assert (parameters->first_column == 0 || parameters->first_column == 1);

  *collection = sgf_collection_new ();
  *error_list = sgf_error_list_new ();

  memset (data->times_error_reported, 0, sizeof data->times_error_reported);

  data->buffer_pointer = data->buffer;

  data->buffer_offset_in_file = 0;
  if (bytes_parsed) {
    *bytes_parsed = 0;
    data->bytes_parsed = bytes_parsed;
  }
  else
    data->bytes_parsed = &dummy_bytes_parsed;

  data->file_error = 0;
  data->cancelled  = 0;

  if (cancellation_flag)
    data->cancellation_flag = cancellation_flag;
  else {
    dummy_cancellation_flag = 0;
    data->cancellation_flag = &dummy_cancellation_flag;
  }

  data->token = 0;

  data->line				  = 0;
  data->pending_column			  = 0;
  data->first_column			  = parameters->first_column;
  data->ko_property_error_position.line	  = 0;
  data->non_sgf_point_error_position.line = 0;
  data->zero_byte_error_position.line	  = 0;

  data->board = NULL;
  data->error_list = *error_list;

  data->latin1_to_utf8 = iconv_open ("UTF-8", "ISO-8859-1");
  assert (data->latin1_to_utf8 != (iconv_t) (-1));

  int current_section = UGF_SECTION_UNDEF;
  bool in_text = false, in_variation = false;
  line_contents = ugf_get_line(data, (char *)NULL);

	do
	{
		if (strstr(line_contents, "[Header]") == line_contents)
		{
			current_section = UGF_SECTION_HEADER;
			data->tree = sgf_game_tree_new ();
			sgf_collection_add_game_tree (*collection, data->tree);
			tree = data->tree;
		} else if (strstr(line_contents, "[Data]") == line_contents)
		{
			complete_node_and_update_board(data, 0);
			current_section = UGF_SECTION_DATA;
			current_node = data->tree->root;
		} else if (strstr(line_contents, "[Figure]") == line_contents)
		{
			printf("Got a figure.\n");
			current_section = UGF_SECTION_FIGURE;
		} else if (strchr(line_contents, '[') == line_contents)
		{
			current_section = UGF_SECTION_UNDEF;
		} else {
		/*	if (current_section == UGF_SECTION_UNDEF)
				continue; */
			if (current_section == UGF_SECTION_HEADER)
			{
				ugf_parse_property(data, line_contents);
			}
			else if (current_section == UGF_SECTION_DATA)
			{
				current_node = push_ugf_node(data, current_node);
			}
			else if (current_section == UGF_SECTION_FIGURE)
			{
				if (in_text)
				{
					if (strchr(line_contents, '.') == line_contents)
					{
						if (strstr(line_contents, ".EndText") == line_contents)
						{
							in_text = false;
						}
						if (strstr(line_contents, ".#") == line_contents)
						{
							char *x_str = strchr(line_contents, ',') + 1;
							char *y_str = strchr(x_str, ',') + 1;
							char *label_value = strchr(y_str, ',') + 1;
							int x = atoi(x_str) - 1;
							int y = atoi(y_str) - 1;
							data->property_type = get_sgf_property("LB");
							ugf_parse_label(data, x, y, label_value);
						}
					} else {
						/* The following was replaced by a direct setting of the value.
							Ugly as heck, but it prevents having to screw with the buffer_pointer.
						  if (ugf_parse_simple_text(data) != SGF_SUCCESS)
							printf("Failed comment: %s\n", line_contents);
						*/
						SgfProperty **link;
						if (sgf_node_find_property (data->node, SGF_COMMENT, &link))
							return SGF_FATAL_DUPLICATE_PROPERTY;
						*link = sgf_property_new (data->tree, SGF_COMMENT, *link);
						(*link)->value.text = utils_duplicate_string(line_contents);
					}
				}
				else if (strstr(line_contents, ".Text") == line_contents)
				{
					if (strchr(line_contents, ','))
					{
					int node_num = atoi(strchr(line_contents, ',')+1);
					int cn = 0;
					for (current_node = tree->root; cn++ != node_num; current_node = current_node->child)
					{
					}
					data->node = current_node;
					in_text = true;
					} else {
						if (in_variation)
						{
							in_text = true;
						}
					}
				}
			}
		}
		ugf_next_line(data);
	} while ((line_contents = ugf_get_line(data, (char *)NULL)));

	if (tree && tree->root) {
		tree->current_node = tree->root;
/*	return SGF_PARSED; */
	}

	assert(tree);

    /* If we couldn't get any game info, kill it. */
    if (!data->board && data->tree)
      sgf_game_tree_delete (data->tree);

    if (data->tree_char_set_to_utf8 != data->latin1_to_utf8
	&& data->tree_char_set_to_utf8 != NULL)
      iconv_close (data->tree_char_set_to_utf8);

  if (data->board)
    board_delete (data->board);

  iconv_close (data->latin1_to_utf8);

  if (data->cancelled || (*collection)->num_trees == 0) {
    string_list_delete (*error_list);
    *error_list = NULL;

    sgf_collection_delete (*collection);
    *collection = NULL;

    if (data->file_error)
      return SGF_ERROR_READING_FILE;
    else
      return data->cancelled ? SGF_PARSING_CANCELLED : SGF_INVALID_FILE;
  }

  if (string_list_is_empty (*error_list)) {
    string_list_delete (*error_list);
    *error_list = NULL;
  }

  return SGF_PARSED;
}


/* Parse root node.  This function is needed because values of `CA',
 * `GM' and `SZ' properties are crucial for property value validation.
 * The function does nothing but finding these properties (if they are
 * present at all).  Afterwards, root node is parsed as usually.
 */
static int
ugf_root_node (SgfParsingData *data, const char *width_string)
{
  SgfGameTree *tree = data->tree;
  BufferPositionStorage storage;

  data->tree_char_set_to_utf8 = data->latin1_to_utf8;

  STORE_BUFFER_POSITION (data, 1, storage);

  data->in_parse_root = 1;
  data->game	      = GAME_GO;
  data->game_type_expected = 0;
  if (width_string)
  {
	data->board_width = atoi(width_string);
	data->board_height = atoi(width_string);
  } else {
	data->board_width = 0;
	do
	{
		char *w_str = ugf_get_line(data, (char *)NULL);
		if (strstr(w_str, "Size=") == w_str)
		{
			data->board_width = atoi(w_str + 5);
			data->board_height = data->board_width;
		}
		ugf_next_line(data);
	} while (data->board_width == 0);
  }

/*
    else if (property_type == SGF_CHAR_SET && !tree->char_set) {
      tree->char_set = do_parse_simple_text (data, SGF_END);

      if (tree->char_set) {
	char *char_set_uppercased = utils_duplicate_string (tree->char_set);
	char *scan;
*/
	/* We now deal with an UTF-8 string, so uppercasing latin
	 * letters is not a problem.
	 */
/*	for (scan = char_set_uppercased; *scan; scan++) {
	  if ('a' <= *scan && *scan <= 'z')
	    *scan += 'A' - 'a';
	}

	data->tree_char_set_to_utf8
	  = (strcmp (char_set_uppercased, "UTF-8") == 0
	     ? NULL : iconv_open ("UTF-8", char_set_uppercased));
	utils_free (char_set_uppercased);

	if (data->tree_char_set_to_utf8 != (iconv_t) (-1)) {
	  if (data->game && data->board_width)
	    break;
	}
	else {
	  data->tree_char_set_to_utf8 = data->latin1_to_utf8;
	  utils_free (tree->char_set);
	}
      }
    }

    discard_values (data);
  }

  if (data->game)
    data->game_type_expected = 1;
  else {
    data->game = GAME_GO;
    data->game_type_expected = 0;
  }
*/

  if (GAME_IS_SUPPORTED (data->game)) {
    if (data->board_width == 0) {
      data->board_width = game_info[data->game].default_board_size;
      data->board_height = game_info[data->game].default_board_size;
    }

    if (data->board_width > SGF_MAX_BOARD_SIZE)
      data->board_width = SGF_MAX_BOARD_SIZE;
    if (data->board_height > SGF_MAX_BOARD_SIZE)
      data->board_height = SGF_MAX_BOARD_SIZE;

    data->use_board = (BOARD_MIN_WIDTH <= data->board_width
		       && data->board_width <= BOARD_MAX_WIDTH
		       && BOARD_MIN_HEIGHT <= data->board_height
		       && data->board_height <= BOARD_MAX_HEIGHT);
    if (data->use_board) {
      if (data->game == GAME_GO)
	data->do_parse_move = do_parse_ugf_move;
      else
	assert (0);

      if (!data->board) {
	data->board = board_new (data->game,
				 data->board_width, data->board_height);
      }
      else {
	board_set_parameters (data->board, data->game,
			      data->board_width, data->board_height);
      }

      data->board_common_mark = 0;
      data->board_change_mark = 0;
      data->board_markup_mark = 0;
      board_fill_uint_grid (data->board, data->common_marked_positions, 0);
      board_fill_uint_grid (data->board, data->changed_positions, 0);
      board_fill_uint_grid (data->board, data->marked_positions, 0);

      if (data->game == GAME_GO) {
	data->board_territory_mark = 0;
	board_fill_uint_grid (data->board, data->territory_positions, 0);
      }
    }
  }

  RESTORE_BUFFER_POSITION (data, 1, storage);

  data->in_parse_root = 0;
  data->game_info_node = NULL;

  data->has_any_setup_property	   = 0;
  data->first_setup_add_property   = 1;
  data->has_any_markup_property	   = 0;
  data->first_markup_property	   = 1;
  data->has_any_territory_property = 0;
  data->first_territory_property   = 1;

  sgf_game_tree_set_game (tree, data->game);
  tree->board_width = data->board_width;
  tree->board_height = data->board_height;

  data->tree->root = init_ugf_tree (data, NULL);
  data->game_info_node = data->node;
  tree->file_format = 0;
  return 1;
}



static SgfNode *
init_ugf_tree (SgfParsingData *data, SgfNode *parent)
{
  SgfNode *node;

  node = sgf_node_new (data->tree, parent);
  data->node = node;
  return node;
}

/* Add a single node to the game tree from the Data section of a UGF file
 *
 * UGF files are more like stacks than trees - only one path exists in the Data section.
 * Hence the name push_ugf_node(). ECB
*/
SgfNode *
push_ugf_node (SgfParsingData *data, SgfNode *parent)
{
  SgfNode *game_info_node = data->game_info_node;
  int num_undos = 0;

	parent->child = sgf_node_new (data->tree, parent);
	data->node = parent->child;
	ugf_parse_move(data);
	return parent->child;
}


static void
ugf_parse_property (SgfParsingData *data, char * line_contents)
{
	char *name_start = line_contents;
	char *name_end = strstr(name_start, "=");

	SgfType property_type = 0;
	SgfError error;


	const char *property_value = name_end + 1;  /* strndup(data->buffer, data->temp_buffer - data->buffer + 1); */

	if (!property_value || strlen(property_value) < 3)
		return;

	*name_end = '\0';
	printf("UGF Property: %s, Value: %s\n", name_start, property_value);
    /* We have parsed some name.  Now turn it into an SGF property by hand. */

	if (!data->game_info_node)
	{
		ugf_root_node (data, 0);
	}
	if (strcmp(name_start, "Date") == 0)
		property_type = SGF_DATE;
	else if (strcmp(name_start, "Winner") == 0)
	{
		property_type = SGF_RESULT;
		*(strchr(data->buffer_pointer, ',')) = '+';
	}
	else if (strcmp(name_start, "Hdcp") == 0)
	{
		property_type = SGF_HANDICAP;
		char *bp = strchr(data->buffer_pointer, ',');
		*bp = ']';
		data->buffer_pointer = strchr(data->buffer_pointer, '=') + 1;
		error = property_info[property_type].value_parser (data);
		property_type = SGF_KOMI;
		*bp = '=';
		property_value = strchr(property_value, ',') + 1;
	}
	else if (strcmp(name_start, "Rule") == 0)
		property_type = SGF_RULE_SET;
	else if (strcmp(name_start, "Place") == 0)
		property_type = SGF_PLACE;
	else if (strcmp(name_start, "PlayerB") == 0)
	{
		data->property_type = SGF_PLAYER_BLACK;
		char *bp = strchr(property_value, ',');
		
		SgfProperty **link;
		if (sgf_node_find_property (data->node, data->property_type, &link))
			return;
		*link = sgf_property_new (data->tree, data->property_type, *link);
		(*link)->value.text = strndup(property_value, bp - property_value);

		property_type = SGF_BLACK_RANK;
		*bp = '=';
		property_value = bp;
		bp = strchr(data->buffer_pointer, ',');
		*bp = '=';
		data->buffer_pointer = bp - 1;
		bp = strchr(property_value, ',') - 1;
		*bp = '\0';
	}
	else if (strcmp(name_start, "PlayerW") == 0)
	{
		data->property_type = SGF_PLAYER_WHITE;
		char *bp = strchr(property_value, ',');
		
		SgfProperty **link;
		if (sgf_node_find_property (data->node, data->property_type, &link))
			return;
		*link = sgf_property_new (data->tree, data->property_type, *link);
		(*link)->value.text = strndup(property_value, bp - property_value);

		property_type = SGF_WHITE_RANK;
		*bp = '=';
		property_value = bp;
		bp = strchr(data->buffer_pointer, ',');
		*bp = '=';
		data->buffer_pointer = bp - 1;
		bp = strchr(property_value, ',') - 1;
		*bp = '\0';
	}
	else if (strcmp(name_start, "Commentator") == 0)
		property_type = SGF_ANNOTATOR;
	else if (strcmp(name_start, "Title") == 0)
	{
		property_type = SGF_GAME_NAME;
		*(strchr(data->buffer_pointer, ',')) = '=';
		*(strrchr(property_value, ',')) = '\0';
		data->buffer_pointer = strchr(data->buffer_pointer, '=') + 1;
	}
	else return;

	printf("Property Value: %s\n", property_value);

	data->property_type = property_type;
	data->buffer_pointer = strchr(data->buffer_pointer, '=') + 1;
	data->temp_buffer = data->buffer_pointer + strlen(property_value) - 1;
	*data->temp_buffer = ']';

	error = property_info[property_type].value_parser (data);
	if (error > SGF_LAST_FATAL_ERROR) {
		if (SGF_FIRST_GAME_INFO_PROPERTY <= property_type
				&& property_type <= SGF_LAST_GAME_INFO_PROPERTY)
			data->game_info_node = data->node;

		if (error != SGF_SUCCESS) {
			add_error (data, error);
			discard_single_value (data);
		}
	}
	else {
		if (error != SGF_FAIL)
			add_error (data, error);
	}
/*  discard_values (data); */
}


/* Parse a value of an unknown property.  No assumptions about format
 * of the property are made besides that "\]" is considered an escaped
 * bracket and doesn't terminate value string.  Unknown properties are
 * always allowed to have a list of values.
 *
 * FIXME: Add charsets support.
 */
/*static void
parse_unknown_property_values (SgfParsingData *data,
			       StringList *property_value_list)
{
  do {
    data->temp_buffer = data->buffer;

    next_character (data);
    while (data->token != ']' && data->token != SGF_END) {
      *data->temp_buffer++ = data->token;
      if (data->token == '\\') {
	next_character (data);
	*data->temp_buffer++ = data->token;
      }

      next_character (data);
    }

    if (data->token == SGF_END)
      return;

    string_list_add_from_buffer (property_value_list,
				 data->buffer,
				 data->temp_buffer - data->buffer);
    next_token (data);
  } while (data->token == '[');
}
*/


/* Parse value of "none" type.  Basically value validation only. */
SgfError
ugf_parse_none (SgfParsingData *data)
{
  SgfProperty **link;

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  if (data->property_type == SGF_KO)
    data->ko_property_error_position = data->property_name_error_position;

  *link = sgf_property_new (data->tree, data->property_type, *link);

  next_character (data);
  if (data->token == ',') {
    next_token (data);
    return SGF_SUCCESS;
  }

  return SGF_ERROR_NON_EMPTY_VALUE;
}


/* Parse a number.  This function is used instead of atoi() or
 * strtol() because we prefer not to create a string for the latter.
 * This function operates directly on buffer.  In addition, with
 * library functions, it would have been difficult to check for
 * errors.
 */
/*static int
do_parse_number (SgfParsingData *data, int *number)
{
  int negative = 0;
  unsigned value = 0;

  if (data->token == '-' || data->token == '+') {
    if (data->token == '-')
      negative = 1;

    next_token_in_value (data);
  }

  if ('0' <= data->token && data->token <= '9') {
    do {
      if (value < (UINT_MAX / 10U))
	value = value * 10 + (data->token - '0');
      else
	value = UINT_MAX;

      next_token_in_value (data);
    } while ('0' <= data->token && data->token <= '9');

    if (!negative) {
      if (value <= INT_MAX) {
	*number = value;
	return 1;
      }

      *number = INT_MAX;
    }
    else {
      if (value <= - (unsigned) INT_MIN) {
	*number = -value;
	return 1;
      }

      *number = INT_MIN;
    }

    return 2;
  }

  return 0;
}
*/

/* Parse a number, but discard it as illegal if negative, or
 * non-positive (for `MN' property.)  For `PM' property, give a
 * warning if the print mode specified is not described in SGF
 * specification.
 */
SgfError
ugf_parse_constrained_number (SgfParsingData *data)
{
  SgfProperty **link;
  BufferPositionStorage storage;
  int number;

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  begin_parsing_ugf_value (data);
  if (data->token == ',')
    return SGF_FATAL_EMPTY_VALUE;

  STORE_BUFFER_POSITION (data, 0, storage);

  if (do_parse_number (data, &number)
      && number >= (data->property_type != SGF_MOVE_NUMBER ? 0 : 1)) {
    if (data->property_type == SGF_PRINT_MODE && number >= NUM_SGF_PRINT_MODES)
      add_error (data, SGF_WARNING_UNKNOWN_PRINT_MODE, number);

    *link = sgf_property_new (data->tree, data->property_type, *link);
    (*link)->value.number = number;

    return end_parsing_value (data);
  }

  RESTORE_BUFFER_POSITION (data, 0, storage);
  return SGF_FATAL_INVALID_VALUE;
}

/*
static int
do_parse_real (SgfParsingData *data, double *real)
{
  double value = 0.0;
  int negative = 0;

  if (data->token == '-' || data->token == '+') {
    if (data->token == '-')
      negative = 1;

    next_token_in_value (data);
  }

  if (('0' <= data->token && data->token <= '9') || data->token == '.') {
    SgfErrorPosition integer_part_error_position;

    if ('0' <= data->token && data->token <= '9') {
      do {
	value = value * 10 + (double) (data->token - '0');
	next_token_in_value (data);
      } while ('0' <= data->token && data->token <= '9');

      integer_part_error_position.line = 0;
    }
    else
      STORE_ERROR_POSITION (data, integer_part_error_position);

    if (data->token == '.') {
      next_token_in_value (data);
      if ('0' <= data->token && data->token <= '9') {
	double decimal_multiplier = 0.1;

	do {
	  value += decimal_multiplier * (double) (data->token - '0');
	  decimal_multiplier /= 10;
	  next_token_in_value (data);
	} while ('0' <= data->token && data->token <= '9');
      }
      else {
	if (integer_part_error_position.line)
	  return 0;
      }
    }

    if (integer_part_error_position.line) {
      insert_error (data, SGF_WARNING_INTEGER_PART_SUPPLIED,
		    &integer_part_error_position);
    }

    if (!negative) {
*/      /* OMG, wrote this with floats, but with doubles, I doubt it
       * makes any sense at all ;).  No point in deleting anyway,
       * right?
       */
/*      if (value <= 10e100) {
	*real = value;
	return 1;
      }

      *real = 10e100;
    }
    else {
      if (value >= -10e100) {
	*real = -value;
	return 1;
      }

      *real = -10e100;
    }

    add_error (data, SGF_ERROR_TOO_LARGE_ABSOLUTE_VALUE);
    return 2;
  }

  return 0;
}
*/

SgfError
ugf_parse_real (SgfParsingData *data)
{
  SgfProperty **link;
  BufferPositionStorage storage;
  double real;

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  begin_parsing_ugf_value (data);
  if (data->token == ',')
    return SGF_FATAL_EMPTY_VALUE;

  STORE_BUFFER_POSITION (data, 0, storage);

  if (do_parse_real (data, &real)) {
    *link = sgf_property_new (data->tree, data->property_type, *link);

#if SGF_REAL_VALUES_ALLOCATED_SEPARATELY
    (*link)->value.real = utils_duplicate_buffer (&real, sizeof (double));
#else
    (*link)->value.real = real;
#endif

    return end_parsing_value (data);
  }

  RESTORE_BUFFER_POSITION (data, 0, storage);
  return SGF_FATAL_INVALID_VALUE;
}


/* Parse a "double" value.  Allowed values are [1] (normal) and [2]
 * (emphasized).  Anything else is considered an error.
 */
SgfError
ugf_parse_double (SgfParsingData *data)
{
  SgfProperty **link;

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  begin_parsing_ugf_value (data);

  *link = sgf_property_new (data->tree, data->property_type, *link);
  (*link)->value.emphasized = (data->token == '2');

  if (data->token == '1' || data->token == '2') {
    next_token_in_value (data);
    return end_parsing_value (data);
  }

  return SGF_ERROR_INVALID_DOUBLE_VALUE;
}


/*static int
do_parse_color (SgfParsingData *data)
{
  if (data->token == 'B')
    return BLACK;

  if (data->token == 'W')
    return WHITE;

  if (data->token == 'b' || data->token == 'w') {
    add_error (data, SGF_WARNING_LOWER_CASE_COLOR,
	       data->token, data->token - ('b' - 'B'));

    return data->token == 'b' ? BLACK : WHITE;
  }

  return EMPTY;
}
*/

/* Parse color value.  [W] stands for white and [B] - for black.  No
 * other values are allowed except that [w] and [b] are upcased and
 * warned about.
 */
SgfError
ugf_parse_color (SgfParsingData *data)
{
  SgfProperty **link;
  int color;

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  begin_parsing_ugf_value (data);
  if (data->token == ',')
    return SGF_FATAL_EMPTY_VALUE;

  color = do_parse_color (data);

  if (color != EMPTY) {
    *link = sgf_property_new (data->tree, data->property_type, *link);
    (*link)->value.color = color;

    next_token_in_value (data);
    return end_parsing_value (data);
  }

  return SGF_FATAL_INVALID_VALUE;
}


/* Parse a simple text value.  All leading and trailing whitespace
 * characters are removed.  Other newlines, if encountered, are
 * converted into spaces.  Escaped newlines are removed completely.
 * Return either a heap copy of parsed values or NULL in case of error
 * (empty value).
 *
 * The `extra_stop_character' parameter should be set to either
 * SGF_END or color (':') depending on desired value terminator in
 * addition to ']'.
 */
static char *
ugf_do_parse_simple_text (SgfParsingData *data, char extra_stop_character)
{
  data->temp_buffer = data->buffer;

  /* Skip leading whitespace. */
  while (1) {
    next_token (data);
    if (data->token == '\\') {
      next_character (data);
      if (data->token == ' ' || data->token == '\n')
	continue;

      *data->temp_buffer++ = data->token;
      next_character (data);
    }

    break;
  }

  if (data->temp_buffer != data->buffer
      || (data->token != ']' && data->token != extra_stop_character
	  && data->token != SGF_END)) {
    do {
      if (data->token != '\\' && data->token != '\n')
	*data->temp_buffer++ = data->token;
      else if (data->token == '\\') {
	next_character (data);
	if (data->token != '\n')
	  *data->temp_buffer++ = data->token;
      }
      else
	*data->temp_buffer++ = ' ';

      next_character (data);
    } while (data->token != ']' && data->token != extra_stop_character
	     && data->token != SGF_END);

    /* Delete trailing whitespace. */
    while (*(data->temp_buffer - 1) == ' ')
      data->temp_buffer--;

    return convert_text_to_utf8 (data, NULL);
  }

  return NULL;
}

/*
static char *
do_parse_text (SgfParsingData *data, char *existing_text)
{
  next_character (data);

  data->temp_buffer = data->buffer;
  while (data->token != '\n' && data->token != SGF_END) {
    if (data->token != '\\')
      *data->temp_buffer++ = data->token;
    else {
      next_character (data);
      if (data->token != '\n')
	*data->temp_buffer++ = data->token;
    }

    next_character (data);
  }

  while (1) {
    if (data->temp_buffer == data->buffer) {
      add_error (data, SGF_WARNING_EMPTY_VALUE);
      next_token (data);
      return existing_text;
    }

    if ((*(data->temp_buffer - 1) != ' ' && *(data->temp_buffer - 1) != '\n'))
      break;

    data->temp_buffer--;
  }

  next_token (data);

  if (existing_text)
    existing_text = utils_cat_as_string (existing_text, "\n\n", 2);

  return convert_text_to_utf8 (data, existing_text);
}
*/

static char *
ugf_get_line (SgfParsingData *data, char *existing_text)
{
  char *newline = strchr(data->buffer_pointer, '\n');
  char *winline = strchr(data->buffer_pointer, '\r');
  if (winline < newline) newline = winline;

  return (newline != 0) ? strndup(data->buffer_pointer, newline - data->buffer_pointer): existing_text;
}


/* Parse a simple text value, that is, a line of text. */
static SgfError
ugf_parse_simple_text (SgfParsingData *data)
{
  SgfProperty **link;
  char *text;

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  text = ugf_do_parse_simple_text (data, '\n');
  if (text) {
    next_token (data);

    *link = sgf_property_new (data->tree, data->property_type, *link);
    (*link)->value.text = text;

    return SGF_SUCCESS;
  }

  return SGF_WARNING_PROPERTY_WITH_EMPTY_VALUE;
}


SgfError
ugf_parse_text (SgfParsingData *data)
{
  int property_found;
  SgfProperty **link;
  char *text;

  property_found = sgf_node_find_property (data->node, data->property_type,
					   &link);
  if (property_found) {
    add_error (data, SGF_WARNING_PROPERTIES_MERGED);
    text = do_parse_text (data, (*link)->value.text);
  }
  else
    text = do_parse_text (data, NULL);

  if (data->token == '[') {
    add_error (data, SGF_WARNING_VALUES_MERGED);

    do
      text = do_parse_text (data, text);
    while (data->token == '[');
  }

  if (!property_found) {
    if (!text) {
      /* Not really a success, but simpler this way. */
      return SGF_SUCCESS;
    }

    *link = sgf_property_new (data->tree, data->property_type, *link);
  }

  (*link)->value.text = text;
  return SGF_SUCCESS;
}


static int
ugf_parse_point (SgfParsingData *data, BoardPoint *point)
{
	if (('a' <= data->token && data->token <= 'z')
			|| ('A' <= data->token && data->token <= 'Z')) {
		char x = data->token;

		ugf_next_token_in_value (data);

		if (('a' <= data->token && data->token <= 'z')
				|| ('A' <= data->token && data->token <= 'Z')) {
			char y = data->token;

			ugf_next_token (data);

			point->x = (x >= 'A' ? x - 'A' : x - 'a');
			point->y = (y >= 'A' ? y - 'A' : y - 'a');

			if (data->token == 'B')
				data->property_type = SGF_BLACK;
			else if (data->token == 'W')
				data->property_type = SGF_WHITE;
			else
				data->property_type = SGF_NONE;

			return (point->x < data->board_width && point->y < data->board_height ? 0 : 1);

		}
		if ('1' <= data->token && data->token <= '9') {
			int y;

			do_parse_number (data, &y);

			point->x = (x >= 'a' ? x - 'a' : x - 'A');
			if (data->game == GAME_GO) {
				if (point->x == 'I' - 'A')
					return 2;

				if (point->x > 'I' - 'A')
					point->x--;
			}

			if (point->x < data->board_width && y <= data->board->height) {
				point->y = (data->game != GAME_REVERSI
						? data->board->height - y : y - 1);
				if (!data->times_error_reported[SGF_WARNING_NON_SGF_POINT_NOTATION]
						&& !data->non_sgf_point_error_position.line) {
					STORE_ERROR_POSITION (data, data->non_sgf_point_error_position);
					data->non_sgf_point_x = x;
					data->non_sgf_point_y = y;
				}

				return 0;
			}
			point->x = -1;
			return 1;
		}
	}
	return 2;
}


static SgfError
do_parse_ugf_move (SgfParsingData *data)
{
  BoardPoint *move_point = &data->node->move_point;

  if (data->token != '\n') {
    BufferPositionStorage storage;

    STORE_BUFFER_POSITION (data, 0, storage);

    switch (ugf_parse_point(data, move_point)) {
    case 0:
      return SGF_SUCCESS;

    case 1:
      if (move_point->x != 19 || move_point->y != 19
	  || data->board_width > 19 || data->board_height > 19)
	return SGF_FATAL_MOVE_OUT_OF_BOARD;

      break;

    default:
      RESTORE_BUFFER_POSITION (data, 0, storage);
      return SGF_FATAL_INVALID_VALUE;
    }

  }

  move_point->x = PASS_X;
  move_point->y = PASS_Y;

  return SGF_SUCCESS;
}


SgfError
ugf_parse_move (SgfParsingData *data)
{
  SgfError error;

  begin_parsing_ugf_value (data);

  if (data->node->move_color == EMPTY) {
	  error = data->do_parse_move (data);
	  if (error == SGF_SUCCESS) {
		  data->node->move_color = (data->property_type == SGF_BLACK
				  ? BLACK : WHITE);

		  /*return end_parsing_value (data);*/
		  return SGF_SUCCESS;
	  }
  }
  else {
    SgfNode *current_node = data->node;
    SgfNode *node = sgf_node_new (data->tree, NULL);

    data->node = node;
    error = data->do_parse_move (data);
    data->node = current_node;

    if (error == SGF_SUCCESS) {
	    int num_undos;

	    add_error (data, SGF_ERROR_ANOTHER_MOVE);

	    num_undos = complete_node_and_update_board (data, 0);

	    node->move_color = (data->property_type == SGF_BLACK ? BLACK : WHITE);
	    node->parent = data->node;
	    data->node->child = node;
	    node = node->child;
	    data->node = node;
	    /*
	       if (end_parsing_value (data) == SGF_ERROR_ILLEGAL_TRAILING_CHARACTERS)
	       add_error (data, SGF_ERROR_ILLEGAL_TRAILING_CHARACTERS);

	       if (data->token == '[') {
	       add_error (data, SGF_ERROR_MULTIPLE_VALUES);
	       discard_values (data);
	       }

	       STORE_ERROR_POSITION (data, data->node_error_position);
	       parse_ugf_node_sequence (data, node);
	     */

	    if (data->node == node) {
		    num_undos += complete_node_and_update_board (data,
				    (data->token != ';'
				     && data->token != '('));
		    node = data->node;
		    board_undo (data->board, num_undos);

		    return SGF_SUCCESS;
	    }
	    else
		    sgf_node_delete (node, data->tree);
    }
  }
  data->non_sgf_point_error_position.line = 0;
  return error;
}

static SgfError
ugf_parse_label (SgfParsingData *data, int x, int y, const char *label_string)
{
	SgfProperty **link;
	char *labels[BOARD_GRID_SIZE];
	int num_labels = 0;

	data->board_common_mark++;

	if (sgf_node_find_property (data->node, get_sgf_property("LB"), &link)) {
		int k;
		SgfLabelList *label_list = (*link)->value.label_list;

		num_labels = label_list->num_labels;
		for (k = 0; k < num_labels; k++) {
			int pos = POINT_TO_POSITION (label_list->labels[k].point);

			labels[pos] = label_list->labels[k].text;
			label_list->labels[k].text = NULL;

			data->common_marked_positions[pos] = data->board_common_mark;
		}

		sgf_property_delete_at_link (link, data->tree);
		add_error (data, SGF_WARNING_PROPERTIES_MERGED);
	}

	BufferPositionStorage storage;
	BoardPoint point;
	int pos;

	point.x = x;
	point.y = y;
	pos = POINT_TO_POSITION (point);
	if (data->common_marked_positions[pos] != data->board_common_mark) {
		labels[pos] = utils_duplicate_string(label_string);
		if (labels[pos]) {
			data->common_marked_positions[pos] = data->board_common_mark;
			num_labels++;
		}
	} else {
		add_error (data, SGF_ERROR_DUPLICATE_LABEL, point.x, point.y);
		data->non_sgf_point_error_position.line = 0;
	}


	if (num_labels > 0) {
		SgfLabelList *label_list = sgf_label_list_new_empty (num_labels);
		int k;
		int x;
		int y;

		*link = sgf_property_new (data->tree, get_sgf_property("LB"), *link);
		(*link)->value.label_list = label_list;

		for (y = 0, k = 0; k < num_labels; y++) {
			for (x = 0; x < data->board_width; x++) {
				if (data->common_marked_positions[POSITION (x, y)]
						== data->board_common_mark) {
					label_list->labels[k].point.x = x;
					label_list->labels[k].point.y = y;
					label_list->labels[k].text = labels[POSITION (x, y)];
					k++;
				}
			}
		}
	}

	return SGF_SUCCESS;
}

/* Parse a value of `AP' property (composed simpletext ":"
 * simpletext).  The value is stored in SgfGameTree structure.
 */
SgfError
ugf_parse_application (SgfParsingData *data)
{
	char *text;

	if (data->tree->application_name)
		return SGF_FATAL_DUPLICATE_PROPERTY;

	/* Parse the first part of value. */
	text = do_parse_simple_text (data, ':');
	if (text) {
		data->tree->application_name = text;

		if (data->token == ':') {
			/* Parse the second part of value. */
			data->tree->application_version = do_parse_simple_text (data, SGF_END);
		}
		else if (data->token == ']')
			add_error (data, SGF_WARNING_COMPOSED_SIMPLE_TEXT_EXPECTED);

		next_token (data);
		return SGF_SUCCESS;
	}

	return SGF_WARNING_PROPERTY_WITH_EMPTY_VALUE;
}


SgfError
ugf_parse_figure (SgfParsingData *data)
{
  SgfProperty **link;
  BufferPositionStorage storage;
  int figure_flags = SGF_FIGURE_USE_DEFAULTS;
  int flags_parsed;
  char *diagram_name = NULL;

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  *link = sgf_property_new (data->tree, data->property_type, *link);

  begin_parsing_ugf_value (data);
  if (data->token == ']') {
    (*link)->value.figure = NULL;
    return SGF_SUCCESS;
  }

  STORE_BUFFER_POSITION (data, 0, storage);

  flags_parsed = do_parse_number (data, &figure_flags);
  if (flags_parsed) {
    if (figure_flags & ~SGF_FIGURE_FLAGS_MASK)
      add_error (data, SGF_WARNING_UNKNOWN_FLAGS);

    if (is_composed_value (data, 1))
      diagram_name = do_parse_simple_text (data, SGF_END);
    else
      add_error (data, SGF_WARNING_COMPOSED_SIMPLE_TEXT_EXPECTED);
  }
  else {
    add_error (data, SGF_ERROR_FIGURE_FLAGS_EXPECTED);

    RESTORE_BUFFER_POSITION (data, 0, storage);
    diagram_name = do_parse_simple_text (data, SGF_END);
  }

  (*link)->value.figure = sgf_figure_description_new (figure_flags,
						      diagram_name);

  return end_parsing_value (data);
}


SgfError
ugf_parse_file_format (SgfParsingData *data)
{
  BufferPositionStorage storage;
  int file_format;

  if (data->tree->file_format)
    return SGF_FATAL_DUPLICATE_PROPERTY;

  begin_parsing_ugf_value (data);
  if (data->token == ']')
    return SGF_FATAL_EMPTY_VALUE;

  STORE_BUFFER_POSITION (data, 0, storage);

  if (do_parse_number (data, &file_format)
      && 1 <= file_format && file_format <= 1000) {
    if (file_format > 4)
      add_error (data, SGF_CRITICAL_FUTURE_FILE_FORMAT, file_format);

    data->tree->file_format = file_format;
    return end_parsing_value (data);
  }

  RESTORE_BUFFER_POSITION (data, 0, storage);
  return SGF_FATAL_INVALID_VALUE;
}


SgfError
ugf_parse_handicap (SgfParsingData *data)
{
  SgfProperty **link;
  BufferPositionStorage storage;
  int handicap;

  if (data->game != GAME_GO) {
    add_error (data, SGF_ERROR_WRONG_GAME,
	       game_info[GAME_GO].name, game_info[data->game].name);
    return SGF_FAIL;
  }

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  STORE_BUFFER_POSITION (data, 0, storage);

  begin_parsing_ugf_value (data);
  if (data->token == ']')
    return SGF_FATAL_EMPTY_VALUE;

  *link = sgf_property_new (data->tree, data->property_type, *link);

  if (do_parse_number (data, &handicap) && data->token == ']') {
    if (handicap > data->board_width * data->board_height) {
      handicap = data->board_width * data->board_height;
      add_error (data, SGF_WARNING_HANDICAP_REDUCED, handicap);
    }
    else if (handicap == 1 || handicap < 0) {
      handicap = 0;
      add_error (data, SGF_ERROR_INVALID_HANDICAP);
    }

    (*link)->value.text = utils_cprintf ("%d", handicap);
    return end_parsing_value (data);
  }

  return invalid_game_info_property (data, *link, &storage);
}


SgfError
ugf_parse_komi (SgfParsingData *data)
{
  SgfProperty **link;
  BufferPositionStorage storage;
  double komi;

  if (data->game != GAME_GO) {
    add_error (data, SGF_ERROR_WRONG_GAME,
	       game_info[GAME_GO].name, game_info[data->game].name);
    return SGF_FAIL;
  }

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  STORE_BUFFER_POSITION (data, 0, storage);

  begin_parsing_ugf_value (data);
  if (data->token == ']')
    return SGF_FATAL_EMPTY_VALUE;

  *link = sgf_property_new (data->tree, data->property_type, *link);

  if (do_parse_real (data, &komi) && data->token == ']') {
    (*link)->value.text = utils_cprintf ("%.f", komi);
    return end_parsing_value (data);
  }

  return invalid_game_info_property (data, *link, &storage);
}


SgfError
ugf_parse_markup_property (SgfParsingData *data)
{
  int markup;

  if (data->first_markup_property) {
    for (markup = 0; markup < NUM_SGF_MARKUPS; markup++)
      data->has_markup_properties[markup] = 0;

    data->first_markup_property = 0;
    data->board_markup_mark += NUM_SGF_MARKUPS;
  }

  if (data->property_type == SGF_MARK)
    markup = SGF_MARKUP_CROSS;
  else if (data->property_type == SGF_CIRCLE)
    markup = SGF_MARKUP_CIRCLE;
  else if (data->property_type == SGF_SQUARE)
    markup = SGF_MARKUP_SQUARE;
  else if (data->property_type == SGF_TRIANGLE)
    markup = SGF_MARKUP_TRIANGLE;
  else if (data->property_type == SGF_SELECTED)
    markup = SGF_MARKUP_SELECTED;
  else
    assert (0);

  if (data->has_markup_properties[markup])
    add_error (data, SGF_WARNING_PROPERTIES_MERGED);

  if (do_parse_list_of_point (data, data->marked_positions,
			      data->board_markup_mark, markup,
			      SGF_ERROR_DUPLICATE_MARKUP, NULL)) {
    data->has_any_markup_property = 1;
    data->has_markup_properties[markup] = 1;
  }

  return SGF_SUCCESS;
}


SgfError
ugf_parse_result (SgfParsingData *data)
{
  SgfProperty **link;
  BufferPositionStorage storage;

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  STORE_BUFFER_POSITION (data, 0, storage);

  begin_parsing_ugf_value (data);
  if (data->token == ']')
    return SGF_FATAL_EMPTY_VALUE;

  *link = sgf_property_new (data->tree, data->property_type, *link);

  if (data->token == 'B' || data->token == 'W') {
    char color = data->token;

    next_token_in_value (data);
    if (data->token == '+') {
      next_token_in_value (data);
      if (('0' <= data->token && data->token <= '9') || data->token == '.') {
	double score;

	if (do_parse_real (data, &score) && data->token == ']') {
	  (*link)->value.text = utils_cprintf ("%c+%.f", color, score);
	  return end_parsing_value (data);
	}
      }
      else {
	static const char *non_score_results[6]
	  = { "F", "Forfeit", "R", "Resign", "T", "Time" };
	int result_index = looking_at (data, non_score_results, 6);

	if (result_index != -1) {
	  /* Use full-word reasons internally. */
	  (*link)->value.text
	    = utils_cprintf ("%c+%s",
			     color, non_score_results[result_index | 1]);

	  return end_parsing_value (data);
	}
      }
    }
  }
  else {
    static const char *no_winner_results[4] = { "0", "?", "Draw", "Void" };
    int result_index = looking_at (data, no_winner_results, 4);

    if (result_index != -1) {
      /* Prefer "Draw" to "0". */
      (*link)->value.text
	= utils_duplicate_string (no_winner_results[result_index != 0
						    ? result_index : 2]);

      return end_parsing_value (data);
    }
  }

  return invalid_game_info_property (data, *link, &storage);
}


SgfError
ugf_parse_setup_property (SgfParsingData *data)
{
  int color;

  if (data->first_setup_add_property) {
    for (color = 0; color < NUM_ON_GRID_VALUES; color++)
      data->has_setup_add_properties[color] = 0;

    data->first_setup_add_property = 0;
    data->board_change_mark += NUM_ON_GRID_VALUES;
  }

  if (data->property_type == SGF_ADD_BLACK)
    color = BLACK;
  else if (data->property_type == SGF_ADD_WHITE)
    color = WHITE;
  else if (data->property_type == SGF_ADD_EMPTY)
    color = EMPTY;
  else if (data->property_type == SGF_ADD_ARROWS) {
    if (data->game != GAME_AMAZONS) {
      add_error (data, SGF_ERROR_WRONG_GAME,
		 game_info[GAME_AMAZONS].name, game_info[data->game].name);
      return SGF_FAIL;
    }

    color = ARROW;
  }
  else
    assert (0);

  if (data->has_setup_add_properties[color])
    add_error (data, SGF_WARNING_PROPERTIES_MERGED);

  if (do_parse_list_of_point (data, data->changed_positions,
			      data->board_change_mark, color,
			      SGF_ERROR_DUPLICATE_SETUP, data->board->grid)) {
    data->has_any_setup_property = 1;
    data->has_setup_add_properties[color] = 1;
  }

  return SGF_SUCCESS;
}


SgfError
ugf_parse_style (SgfParsingData *data)
{
  BufferPositionStorage storage;

  if (data->tree->style_is_set)
    return SGF_FATAL_DUPLICATE_PROPERTY;

  begin_parsing_ugf_value (data);
  if (data->token == ']')
    return SGF_FATAL_EMPTY_VALUE;

  STORE_BUFFER_POSITION (data, 0, storage);

  if (do_parse_number (data, &data->tree->style)) {
    data->tree->style_is_set = 1;

    if (data->tree->style & ~SGF_STYLE_FLAGS_MASK)
      add_error (data, SGF_WARNING_UNKNOWN_FLAGS);

    return end_parsing_value (data);
  }

  RESTORE_BUFFER_POSITION (data, 0, storage);
  return SGF_FATAL_INVALID_VALUE;
}


SgfError
ugf_parse_territory (SgfParsingData *data)
{
  int color_index;

  if (data->game != GAME_GO) {
    add_error (data, SGF_ERROR_WRONG_GAME,
	       game_info[GAME_GO].name, game_info[data->game].name);
    return SGF_FAIL;
  }

  if (data->first_territory_property) {
    data->has_territory_properties[BLACK_INDEX] = 0;
    data->has_territory_properties[WHITE_INDEX] = 0;

    data->first_territory_property = 0;
    data->board_territory_mark += NUM_COLORS;
  }

  if (data->property_type == SGF_BLACK_TERRITORY)
    color_index = BLACK_INDEX;
  else if (data->property_type == SGF_WHITE_TERRITORY)
    color_index = WHITE_INDEX;
  else
    assert (0);

  if (data->has_territory_properties[color_index])
    add_error (data, SGF_WARNING_PROPERTIES_MERGED);

  if (do_parse_list_of_point (data, data->territory_positions,
			      data->board_territory_mark, color_index,
			      SGF_ERROR_DUPLICATE_TERRITORY, NULL)) {
    data->has_any_territory_property = 1;
    data->has_territory_properties[color_index] = 1;
  }

  return SGF_SUCCESS;
}


SgfError
ugf_parse_time_limit (SgfParsingData *data)
{
  SgfProperty **link;
  BufferPositionStorage storage;
  double time_limit;

  if (sgf_node_find_property (data->node, data->property_type, &link))
    return SGF_FATAL_DUPLICATE_PROPERTY;

  STORE_BUFFER_POSITION (data, 0, storage);

  begin_parsing_ugf_value (data);
  if (data->token == ']')
    return SGF_FATAL_EMPTY_VALUE;

  if (do_parse_real (data, &time_limit) && data->token == ']') {
    if (time_limit < 0.0) {
      RESTORE_BUFFER_POSITION (data, 0, storage);
      next_character (data);

      return SGF_FATAL_NEGATIVE_TIME_LIMIT;
    }

    *link = sgf_property_new (data->tree, data->property_type, *link);
    (*link)->value.text = utils_cprintf ("%.f", time_limit);

    return end_parsing_value (data);
  }

  *link = sgf_property_new (data->tree, data->property_type, *link);
  return invalid_game_info_property (data, *link, &storage);
}


SgfError
ugf_parse_to_play (SgfParsingData *data)
{
  if (data->node->to_play_color != EMPTY)
    return SGF_FATAL_DUPLICATE_PROPERTY;

  begin_parsing_ugf_value (data);
  if (data->token == '\n')
    return SGF_FATAL_EMPTY_VALUE;

  data->node->to_play_color = do_parse_color (data);

  if (data->node->to_play_color != EMPTY) {
    /* `PL' is a setup property. */
    data->has_any_setup_property = 1;

    next_token_in_value (data);
    return end_parsing_value (data);
  }

  return SGF_FATAL_INVALID_VALUE;
}


/* FIXME: write this function. */
SgfError
ugf_parse_letters (SgfParsingData *data)
{
  begin_parsing_ugf_value (data);
  while (data->token != '\n') next_token (data);
  return end_parsing_value (data);
}


/* FIXME: write this function. */
SgfError
ugf_parse_simple_markup (SgfParsingData *data)
{
  begin_parsing_ugf_value (data);
  while (data->token != '\n') next_token (data);
  return end_parsing_value (data);
}

inline static void
begin_parsing_ugf_value (SgfParsingData *data)
{
  data->whitespace_error_position.line = 0;
  if (data->token == '\n' || data->token == ' ' || data->token == ',')
	  next_token_in_value (data);
}



inline static void
ugf_next_line (SgfParsingData *data)
{
	do
		ugf_next_character (data);
	while (data->token != '\n');
}

/* Read characters from the input buffer skipping any whitespace
 * encountered.  This is just a wrapper around next_character().
 */
inline static void
ugf_next_token (SgfParsingData *data)
{
  do
    ugf_next_character (data);
  while (data->token == ' ' || data->token == ',' || data->token == '\n' || data->token == '\r');
}


/* Similar to next_token() except that keeps track of the first
 * whitespace encountered (if any).  This information is used by
 * end_parsing_value() for reporting errors.
 *
 * This function also handles the escape character ('\') transparently
 * for its callers (SGF specification remains silent about escaping in
 * non-text properties, but this is the way `SGFC' behaves).
 */
static void
ugf_next_token_in_value (SgfParsingData *data)
{
  ugf_next_character (data);
  if (data->token == '\\') {
    ugf_next_character (data);
    if (data->token == '[')
      data->token = ESCAPED_BRACKET;
  }

  data->whitespace_passed = 1;

  if (data->token == ' ' || data->token == '\n' || data->token == ',') {
    if (!data->whitespace_error_position.line) {
      STORE_ERROR_POSITION (data, data->whitespace_error_position);
      data->whitespace_passed = 0;
    }

    do {
      ugf_next_character (data);
      if (data->token == '\\') {
	ugf_next_character (data);
	if (data->token == '[')
	  data->token = ESCAPED_BRACKET;
      }
    } while (data->token == ' ' || data->token == '\n' || data->token == ',');
  }
}


/* Read a character from the input buffer.  All linebreak combinations
 * allowed by SGF (LF, CR, CR LF, LF CR) are replaced with a single
 * '\n'.  All other whitespace characters ('\t', '\v' and '\f') are
 * converted to spaces.  The function also keeps track of the current
 * line and column in the buffer.
 */
static void
ugf_next_character (SgfParsingData *data)
{
  if (data->buffer_pointer < data->buffer_end) {
    char token = *data->buffer_pointer++;

    /* Update current line and column based on the _previous_
     * character.  This way newlines appear at ends of lines.
     */
    data->column = data->pending_column;
    if (data->column == 0)
      data->line++;

    if (token != '\n' && token != '\r') {
      if (token == 0) {
	if (!data->zero_byte_error_position.line) {
	  /* To avoid spoiling add_error() calls, we add warning about
	   * zero byte after the complete buffer is parsed.
	   */
	  STORE_ERROR_POSITION (data, data->zero_byte_error_position);
	}

	while (data->buffer_pointer < data->buffer_end
	       && *data->buffer_pointer == 0) {
	  data->buffer_pointer++;
	  data->pending_column++;
	}

	ugf_next_character (data);
	return;
      }

      data->token = token;
      data->pending_column++;

      /* SGF specification tells to handle '\t', '\v' and '\f' as a
       * space.  Also, '\t' updates column in a non-standard way.
       */
      if (token == '\t') {
	data->pending_column = ROUND_UP (data->pending_column, 8);
	data->token = ' ';
      }

      if (token == '\v' || token == '\f')
	data->token = ' ';
    }
    else {
      if (data->buffer_pointer == data->buffer_end
	  && data->buffer_refresh_point < data->buffer_end)
	expand_buffer (data);

      if (data->buffer_pointer < data->buffer_end
	  && token + *data->buffer_pointer == '\n' + '\r') {
	/* Two character linebreak, e.g. for Windows systems. */
	data->buffer_pointer++;
      }

      data->token = '\n';
      data->pending_column = 0;
    }
  }
  else {
    if (data->buffer_refresh_point == data->buffer_end)
      data->token = SGF_END;
    else {
      expand_buffer (data);
      ugf_next_character (data);
    }
  }
}


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
