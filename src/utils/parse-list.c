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


#include "parse-list.h"
#include "utils.h"

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct _FileListItem	FileListItem;
typedef struct _FileList	FileList;

struct _FileListItem {
  FileListItem		 *next;
  char			 *filename;

  FILE			 *file;
  int			  line_number;
};

struct _FileList {
  FileListItem		 *first;
  FileListItem		 *last;

  int			  item_size;
  StringListItemDispose	  item_dispose;
};


#define file_list_new()							\
  ((FileList *) string_list_new_derived (sizeof (FileListItem),		\
					 ((StringListItemDispose)	\
					  file_list_item_dispose)))

#define file_list_init(list)						\
  string_list_init_derived ((list), sizeof (FileListItem),		\
			    ((StringListItemDispose)			\
			     file_list_item_dispose))

#define STATIC_FILE_LIST						\
  STATIC_STRING_LIST_DERIVED (FileListItem, file_list_item_dispose)

static void	    file_list_item_dispose (FileListItem *item);


#define file_list_get_item(list, item_index)				\
  ((FileListItem *) string_list_get_item ((list), (item_index)))

#define file_list_find(list, filename)					\
  ((FileListItem *) string_list_find ((list), (filename)))

#define file_list_find_after_notch(list, filename, notch)		\
  ((FileListItem *)							\
   string_list_find_after_notch ((list), (filename), (notch)))


typedef struct _ConditionStackEntry	ConditionStackEntry;

struct _ConditionStackEntry {
  ConditionStackEntry	     *next;

  const PredefinedCondition  *condition;
  char			      is_true;
  char			      is_in_else_clause;
};


inline static void  print_usage (FILE *where);

static FILE *	    open_file (const char *filename, int for_writing);

static int	    do_parse_lists (FILE *h_file, FILE *c_file,
				    const ListDescription *lists);
static void	    reuse_last_line (char **current_line);

static const PredefinedCondition *
		    get_condition (char **line);

inline static void  push_condition_stack (const PredefinedCondition *condition,
					  int is_negated);
inline static void  pop_condition_stack (void);


const char			  *tab_string = "\t\t\t\t\t\t\t\t\t\t\t\t";


StringBuffer			   h_file_top;
StringBuffer			   h_file_bottom;

StringBuffer			   c_file_top;
StringBuffer			   c_file_bottom;


static AssociationList		   substitutions = STATIC_ASSOCIATION_LIST;
static const PredefinedCondition  *predefined_conditions;

static FileList			   list_files = STATIC_FILE_LIST;

static StringList		   lines = STATIC_STRING_LIST;
static int			   last_line_reusable = 0;

static ConditionStackEntry	  *condition_stack = NULL;


enum {
  OPTION_HELP = UCHAR_MAX + 1
};

static const struct option parse_list_options[] = {
  { "help",		no_argument,		NULL, OPTION_HELP },
  { "define",		required_argument,	NULL, 'D' },
  { "substitute",	required_argument,	NULL, 'D' },
  { NULL,		no_argument,		NULL, 0 }
};

static const char *help_string =
  "\n"
  "Options:\n"
  "  -D, --define SYMBOL=SUBSTITUTION\n"
  "                          define a symbol for `$...$'-style substitutions\n"
  "  --help                  display this help and exit\n";


int
parse_list_main (int argc, char *argv[],
		 const ListDescriptionSet *list_sets, int num_sets,
		 const PredefinedCondition *conditions)
{
  int option;
  int result = 255;

  utils_remember_program_name (argv[0]);

  while ((option = getopt_long (argc, argv, "D:", parse_list_options, NULL))
	 != -1) {
    switch (option) {
    case OPTION_HELP:
      print_usage (stdout);
      printf (help_string);

      result = 0;
      goto exit_parse_list_main;

    case 'D':
      {
	const char *delimiter = strchr (optarg, '=');

	if (delimiter) {
	  if (memchr (optarg, '$', delimiter - optarg)) {
	    fprintf (stderr,
		     ("%s: fatal: "
		      "substitution name cannot contain dollar signs\n"),
		     short_program_name);
	    goto exit_parse_list_main;
	  }

	  string_list_add_from_buffer (&substitutions,
				       optarg, delimiter - optarg);
	  substitutions.last->association
	    = utils_duplicate_string (delimiter + 1);
	}
	else {
	  string_list_add (&substitutions, optarg);
	  substitutions.last->association = utils_duplicate_string ("");
	}
      }

      break;

    default:
      fprintf (stderr, "Try `%s --help' for more information.\n",
	       full_program_name);
      goto exit_parse_list_main;
    }
  }

  if (argc - optind == 3) {
    char *list_file_name = argv[optind];
    char *h_file_name	 = argv[optind + 1];
    char *c_file_name	 = argv[optind + 2];
    FILE *list_file	 = open_file (list_file_name, 0);

    if (list_file) {
      char *line;
      const ListDescription *lists = NULL;
      int k;

      string_list_prepend (&list_files, list_file_name);
      list_files.first->file = list_file;
      list_files.first->line_number = 0;

      do
	line = read_line ();
      while (line && (! *line || *line == '#'));

      if (looking_at ("@mode", &line)) {
	const char *mode = parse_thing (IDENTIFIER, &line, "mode name");

	if (mode) {
	  for (k = 0; k < num_sets; k++) {
	    if (strcmp (mode, list_sets[k].mode_name) == 0) {
	      lists = list_sets[k].lists;
	      break;
	    }
	  }

	  if (!lists)
	    print_error ("fatal: unknown mode `%s'", mode);
	}
      }
      else
	print_error ("fatal: `@mode' expected");

      if (lists) {
	FILE *h_file = open_file (h_file_name, 1);

	if (h_file) {
	  FILE *c_file = open_file (c_file_name, 1);

	  if (c_file) {
	    static const char *preamble =
	      "/* This file is automatically generated by `%s'.\n"
	      " * Do not modify it, edit `%s' instead.\n"
	      " */\n";

	    int n;
	    int h_file_name_length = strlen (h_file_name);

	    fprintf (h_file, preamble, short_program_name, list_file_name);
	    fprintf (c_file, preamble, short_program_name, list_file_name);

	    for (k = h_file_name_length; k >= 1; k--) {
	      if (h_file_name[k - 1] == DIRECTORY_SEPARATOR)
		break;
	    }

	    for (n = 0; k < h_file_name_length; k++) {
	      if (isalnum (h_file_name[k]))
		h_file_name[n++] = toupper (h_file_name[k]);
	      else if (h_file_name[k] == '.'
		       || h_file_name[k] == '_'
		       || h_file_name[k] == '-')
		h_file_name[n++] = '_';
	    }

	    h_file_name[n] = 0;
	    if (n > 4 && strcmp (h_file_name + n - 4, "_NEW") == 0)
	      h_file_name[n - 4] = 0;

	    fprintf (h_file, "\n\n#ifndef QUARRY_%s\n#define QUARRY_%s\n",
		     h_file_name, h_file_name);

	    predefined_conditions = conditions;
	    result = do_parse_lists (h_file, c_file, lists);

	    fprintf (h_file, "\n\n#endif /* QUARRY_%s */\n", h_file_name);

	    fclose (c_file);
	  }

	  fclose (h_file);
	}
      }

      string_list_empty (&list_files);

      while (condition_stack)
	pop_condition_stack ();
    }
  }
  else {
    print_usage (stderr);
    fprintf (stderr, "Try `%s --help' for more information.\n",
	     full_program_name);
  }

 exit_parse_list_main:
  string_list_empty (&substitutions);
  utils_free_program_name_strings ();

  return result;
}


static void
print_usage (FILE *where)
{
  fprintf (where, "Usage: %s [OPTION]... LIST_FILE H_FILE C_FILE\n",
	   full_program_name);
}


static void
file_list_item_dispose (FileListItem *item)
{
  fclose (item->file);
}


static FILE *
open_file (const char *filename, int for_writing)
{
  FILE *file = fopen (filename, for_writing ? "w" : "r");

  if (!file) {
    fprintf (stderr, "%s: can't open file `%s' for %s\n",
	     short_program_name, filename,
	     for_writing ? "writing" : "reading");
  }

  return file;
}


static int
do_parse_lists (FILE *h_file, FILE *c_file, const ListDescription *lists)
{
  int k;
  int result = 1;
  int had_c_includes = 0;
  int had_h_includes = 0;
  int in_list = 0;
  int equal_to_last = 0;
  int h_file_line_length = 0;
  int num_c_file_array_elements = 0;
  int pending_linefeeds = 0;
  const char *h_file_enum_name = NULL;
  const char *c_file_array_name = NULL;
  char *last_identifier = NULL;
  char *pending_h_comment = NULL;
  char *pending_c_comment = NULL;
  char *pending_eol_comment = NULL;
  StringBuffer c_file_arrays[NUM_LIST_SORT_ORDERS];
  StringBuffer *list_c_file_array = NULL;
  StringBuffer h_file_enums;

  string_buffer_init (&h_file_top, 0x2000, 0x1000);
  string_buffer_init (&h_file_bottom, 0x2000, 0x1000);
  string_buffer_init (&h_file_enums, 0x2000, 0x1000);

  string_buffer_init (&c_file_top, 0x2000, 0x1000);
  string_buffer_init (&c_file_bottom, 0x2000, 0x1000);
  for (k = 0; k < NUM_LIST_SORT_ORDERS; k++)
    string_buffer_init (&c_file_arrays[k], 0x2000, 0x1000);

  while (1) {
    char *line = read_line ();

    if (!line) {
      while (lists->name && lists->multiple_lists_allowed)
	lists++;

      if (!condition_stack) {
	if (!lists->name) {
	  result = 0;

	  if (lists->list_finalizer) {
	    if (lists->list_finalizer (NULL))
	      result = 1;
	  }
	}
	else {
	  fprintf (stderr,
		   "%s: unexpected end of file: list of type `%s' expected\n",
		   short_program_name, lists->name);
	}
      }
      else {
	fprintf (stderr,
		 "%s: unexpected end of file: condition `%s' unterminated\n",
		 short_program_name, condition_stack->condition->identifier);
      }

      break;
    }

    if (! *line)
      continue;

    if (line[0] == '#') {
      if (line[1] == '>') {
	line = line + 2;
	while (isspace (*line))
	  line++;

	utils_free (pending_h_comment);
	pending_h_comment = utils_duplicate_string (line);

	utils_free (pending_c_comment);
	pending_c_comment = utils_duplicate_string (line);
      }

      continue;
    }

    if (in_list) {
      if (line[0] != '}') {
	char first_char = line[0];
	const char *identifier = NULL;

	if (first_char != '=' && first_char != '+') {
	  if (lists->line_parser1) {
	    if (lists->line_parser1 (&line))
	      break;

	    if (!line)
	      continue;

	    while (*line && isspace (*line))
	      line++;
	  }
	}
	else {
	  if (!h_file_enum_name) {
	    print_error ("`+' and `=' directives are not allowed "
			 "in lists that don't generate enumerations");
	    break;
	  }

	  do
	    line++;
	  while (isspace (*line));
	}

	if ((!pending_eol_comment || ! *pending_eol_comment)
	    && last_identifier
	    && h_file_enum_name)
	  string_buffer_cat_string (&h_file_enums, ",\n");

	if (pending_eol_comment) {
	  if (*pending_eol_comment && h_file_enum_name) {
	    string_buffer_cprintf (&h_file_enums, ",%s/* %s */\n",
				   TABBING (7, h_file_line_length + 1),
				   pending_eol_comment);
	  }

	  utils_free (pending_eol_comment);
	  pending_eol_comment = NULL;
	}

	if (pending_h_comment) {
	  if (*pending_h_comment && h_file_enum_name) {
	    if (last_identifier)
	      string_buffer_add_character (&h_file_enums, '\n');
	    string_buffer_cat_strings (&h_file_enums,
				       "  /* ", pending_h_comment, " */\n",
				       NULL);
	  }

	  utils_free (pending_h_comment);
	  pending_h_comment = NULL;
	}

	if (h_file_enum_name) {
	  identifier = parse_thing (IDENTIFIER, &line, "identifier");
	  if (!identifier)
	    break;

	  string_buffer_cat_strings (&h_file_enums, "  ", identifier, NULL);
	  h_file_line_length = 2 + strlen (identifier);

	  if (first_char == '=' || equal_to_last) {
	    string_buffer_cat_strings (&h_file_enums,
				       " = ", last_identifier, NULL);
	    h_file_line_length += 3 + strlen (last_identifier);
	  }

	  utils_free (last_identifier);
	  last_identifier = utils_duplicate_string (identifier);
	}

	if (first_char != '+') {
	  if (first_char != '=') {
	    if (c_file_array_name && *lists->c_file_array_type) {
	      if (num_c_file_array_elements > 0) {
		string_buffer_add_character (list_c_file_array, ',');
		string_buffer_add_characters (list_c_file_array, '\n',
					      1 + pending_linefeeds);
	      }

	      if (pending_c_comment) {
		if (*pending_c_comment) {
		  if (num_c_file_array_elements > 0)
		    string_buffer_add_character (list_c_file_array, '\n');

		  string_buffer_cat_strings (list_c_file_array,
					     "  /* ", pending_c_comment,
					     " */\n", NULL);
		}

		utils_free (pending_c_comment);
		pending_c_comment = NULL;
	      }
	    }

	    if (c_file_array_name)
	      num_c_file_array_elements++;

	    pending_linefeeds = 0;
	    if (lists->line_parser2 (list_c_file_array, &line, identifier,
				     &pending_eol_comment, &pending_linefeeds))
	      break;

	    if (*line) {
	      print_error ("unexpected characters at the end of line");
	      break;
	    }

	    if (pending_linefeeds < 0) {
	      pending_linefeeds = 0;
	      if (! *line) {
		while (1) {
		  line = read_line ();

		  if (line && ! *line)
		    pending_linefeeds++;
		  else {
		    reuse_last_line (&line);
		    break;
		  }
		}
	      }
	    }
	  }

	  equal_to_last = 0;
	}
	else {
	  if (equal_to_last) {
	    print_error ("second inserted identifier in a row; "
			 "did you mean `='?");
	    break;
	  }

	  equal_to_last = 1;
	}
      }
      else {
	if (!last_identifier && num_c_file_array_elements == 0) {
	  print_error ("empty list `%s'", lists->name);
	  break;
	}

	if (pending_eol_comment) {
	  if (*pending_eol_comment && h_file_enum_name) {
	    string_buffer_cprintf (&h_file_enums, "%s/* %s */",
				   TABBING (7, h_file_line_length),
				   pending_eol_comment);
	  }

	  utils_free (pending_eol_comment);
	  pending_eol_comment = NULL;
	}

	if (lists->list_finalizer) {
	  if (lists->list_finalizer (list_c_file_array))
	    break;
	}

	if (h_file_enum_name) {
	  if (strcmp (h_file_enum_name, "unnamed") != 0) {
	    string_buffer_cat_strings (&h_file_enums,
				       "\n} ", h_file_enum_name, ";\n", NULL);
	  }
	  else
	    string_buffer_cat_string (&h_file_enums, "\n};\n");
	}

	if (c_file_array_name && *lists->c_file_array_type)
	  string_buffer_cat_string (list_c_file_array, "\n};\n");

	if (!lists->multiple_lists_allowed)
	  lists++;

	in_list = 0;
      }
    }
    else {
      if (looking_at ("@include", &line) || looking_at ("@c_include", &line)) {
	if (! *line) {
	  print_error ("filename expected");
	  break;
	}

	if (!had_c_includes) {
	  fputs ("\n\n", c_file);
	  had_c_includes = 1;
	}

	fprintf (c_file, "#include %s\n", line);
      }
      else if (looking_at ("@h_include", &line)) {
	if (! *line) {
	  print_error ("filename expected");
	  break;
	}

	if (had_h_includes != 1) {
	  fputs ((had_h_includes == 0 ? "\n\n" : "\n"), h_file);
	  had_h_includes = 1;
	}

	fprintf (h_file, "#include %s\n", line);
      }
      else if (looking_at ("@define_condition", &line)) {
	const PredefinedCondition *condition = get_condition (&line);

	if (!condition)
	  break;

	if (had_h_includes != 2) {
	  fputs ((had_h_includes == 0 ? "\n\n" : "\n"), h_file);
	  had_h_includes = 2;
	}

	fprintf (h_file, "#define %s%s%d\n",
		 condition->identifier,
		 TABBING (4, 8 + strlen (condition->identifier)),
		 condition->value);
      }
      else {
	const char *identifier = parse_thing (IDENTIFIER, &line, "list name");

	if (!identifier)
	  break;

	if (!lists->name) {
	  print_error ("unexpected list beginning");
	  break;
	}

	if (lists->multiple_lists_allowed
	    && strcmp (identifier, lists->name) != 0
	    && (lists + 1)->name)
	  lists++;

	if (strcmp (identifier, lists->name) == 0) {
	  if (looking_at ("-", &line)) {
	    if (lists->enumeration_required) {
	      print_error ("enumeration name expected");
	      break;
	    }

	    h_file_enum_name = NULL;
	  }
	  else {
	    h_file_enum_name = parse_thing (IDENTIFIER, &line,
					    "enumeration name");
	    if (!h_file_enum_name)
	      break;
	  }

	  if (!lists->c_file_array_type) {
	    if (!looking_at ("-", &line)) {
	      print_error ("unexpected array name");
	      break;
	    }

	    c_file_array_name = NULL;
	  }
	  else {
	    if (looking_at ("-", &line)) {
	      print_error ("array name expected");
	      break;
	    }

	    c_file_array_name = parse_thing (IDENTIFIER, &line,
					     "array name");
	    if (!c_file_array_name)
	      break;
	  }

	  if (*line != '{') {
	    print_error ("list opening brace expected");
	    break;
	  }

	  if (*(line + 1)) {
	    print_error ("unexpected characters at the end of line");
	    break;
	  }

	  if (pending_h_comment) {
	    if (*pending_h_comment && h_file_enum_name) {
	      string_buffer_cat_strings (&h_file_enums,
					 "/* ", pending_h_comment, " */\n",
					 NULL);
	    }

	    utils_free (pending_h_comment);
	    pending_h_comment = NULL;
	  }

	  assert (0 <= lists->sort_order
		  && lists->sort_order <= NUM_LIST_SORT_ORDERS);
	  list_c_file_array = &c_file_arrays[lists->sort_order];

	  if (h_file_enum_name) {
	    if (strcmp (h_file_enum_name, "unnamed") != 0)
	      string_buffer_cat_string (&h_file_enums, "\n\ntypedef enum {\n");
	    else
	      string_buffer_cat_string (&h_file_enums, "\n\nenum {\n");
	  }

	  if (c_file_array_name && *lists->c_file_array_type) {
	    string_buffer_cat_strings (list_c_file_array,
				       "\n\n", lists->c_file_array_type,
				       c_file_array_name, "[] = {\n", NULL);
	  }

	  if (lists->list_initializer) {
	    if (lists->list_initializer (list_c_file_array,
					 h_file_enum_name, c_file_array_name))
	      break;
	  }

	  in_list		    = 1;
	  equal_to_last		    = 0;
	  num_c_file_array_elements = 0;
	  pending_linefeeds	    = 1;

	  utils_free (last_identifier);
	  last_identifier = NULL;
	}
	else {
	  print_error ("list name `%s' expected, got `%s'",
		       lists->name, identifier);
	  break;
	}
      }
    }
  }

  if (h_file_top.length > 0)
    fwrite (h_file_top.string, h_file_top.length, 1, h_file);

  if (h_file_enums.length > 0)
    fwrite (h_file_enums.string, h_file_enums.length, 1, h_file);

  if (h_file_bottom.length > 0)
    fwrite (h_file_bottom.string, h_file_bottom.length, 1, h_file);

  string_buffer_dispose (&h_file_top);
  string_buffer_dispose (&h_file_bottom);
  string_buffer_dispose (&h_file_enums);

  if (c_file_top.length > 0)
    fwrite (c_file_top.string, c_file_top.length, 1, c_file);

  for (k = 0; k < NUM_LIST_SORT_ORDERS; k++) {
    fwrite (c_file_arrays[k].string, c_file_arrays[k].length, 1, c_file);
    string_buffer_dispose (&c_file_arrays[k]);
  }

  if (c_file_bottom.length > 0)
    fwrite (c_file_bottom.string, c_file_bottom.length, 1, c_file);

  string_buffer_dispose (&c_file_top);
  string_buffer_dispose (&c_file_bottom);

  utils_free (last_identifier);
  utils_free (pending_h_comment);
  utils_free (pending_c_comment);
  utils_free (pending_eol_comment);

  string_list_empty (&lines);

  return result;
}


static void
reuse_last_line (char **current_line)
{
  static char empty_line[] = "";

  assert (!last_line_reusable);
  last_line_reusable = 1;

  *current_line = empty_line;
}


void
print_error (const char *format_string, ...)
{
  va_list arguments;

  if (!string_list_is_empty (&list_files)) {
    fprintf (stderr, "%s:%d: ",
	     list_files.first->filename, list_files.first->line_number);
  }

  va_start (arguments, format_string);
  vfprintf (stderr, format_string, arguments);
  va_end (arguments);

  fputc ('\n', stderr);
}


char *
read_line (void)
{
  char *line;

  if (!last_line_reusable) {
    int length;
    char *beginning;
    char *end;

  read_next_line:
    line = NULL;
    while (!line && !string_list_is_empty (&list_files)) {
      line = utils_fgets (list_files.first->file, &length);
      if (!line)
	string_list_delete_first_item (&list_files);
    }

    if (!line)
      return NULL;

    list_files.first->line_number++;

    for (beginning = line; *beginning; beginning++) {
      if (!isspace (*beginning))
	break;
    }

    for (end = line + length; end > beginning; end--) {
      if (!isspace (*(end - 1)))
	break;
    }

    *end = 0;
    if (*beginning) {
      char *scan;

      for (scan = beginning; scan < end; scan++) {
	if (*scan == '$') {
	  if (* ++scan == '$') {
	    char *copy_scan;

	    for (copy_scan = scan; copy_scan < end; copy_scan++)
	      *copy_scan = *(copy_scan + 1);

	    end--;
	  }
	  else {
	    char *second_delimiter_scan;

	    for (second_delimiter_scan = scan + 1;
		 second_delimiter_scan < end; second_delimiter_scan++) {
	      if (*second_delimiter_scan == '$')
		break;
	    }

	    if (second_delimiter_scan < end) {
	      const char *substitution;
	      int substitution_length;
	      char *new_line;

	      *second_delimiter_scan = 0;

	      substitution = association_list_find_association (&substitutions,
								scan);
	      if (!substitution) {
		print_error ("undefined substitution symbol `%s'", scan);
		goto error;
	      }

	      substitution_length = strlen (substitution);
	      new_line
		= utils_cat_as_strings (NULL,
					beginning, (scan - 1) - beginning,
					substitution, substitution_length,
					second_delimiter_scan + 1,
					end - (second_delimiter_scan + 1),
					NULL);
	      scan	= new_line + (((scan - 1) - beginning)
				      + substitution_length);
	      beginning = new_line;
	      end	= scan + (end - (second_delimiter_scan + 1));

	      utils_free (line);
	      line = new_line;
	    }
	    else {
	      print_error ("warning: possible unterminated substitution");
	      break;
	    }
	  }
	}
      }

      if (looking_at ("@include_list", &beginning)) {
	if (*beginning) {
	  FILE *new_list_file = fopen (beginning, "r");

	  if (new_list_file) {
	    string_list_prepend_from_buffer (&list_files,
					     beginning, end - beginning);
	    list_files.first->file = new_list_file;
	    list_files.first->line_number = 0;
	  }
	  else {
	    print_error ("can't open file %s for reading", beginning);
	    string_list_empty (&list_files);
	  }
	}
	else {
	  print_error ("name of file to include is missing");
	  string_list_empty (&list_files);
	}

	utils_free (line);
	goto read_next_line;
      }
      else if (looking_at ("@if", &beginning)) {
	const PredefinedCondition *condition = get_condition (&beginning);

	if (!condition)
	  goto error;

	push_condition_stack (condition, 0);

	utils_free (line);
	goto read_next_line;
      }
      else if (looking_at ("@ifnot", &beginning)) {
	const PredefinedCondition *condition = get_condition (&beginning);

	if (!condition)
	  goto error;

	push_condition_stack (condition, 1);

	utils_free (line);
	goto read_next_line;
      }
      else if (looking_at ("@else", &beginning)) {
	if (!condition_stack || condition_stack->is_in_else_clause) {
	  print_error ("unexpected `@else'");
	  goto error;
	}

	condition_stack->is_in_else_clause = 1;

	utils_free (line);
	goto read_next_line;
      }
      else if (looking_at ("@endif", &beginning)) {
	if (!condition_stack) {
	  print_error ("unexpected `@endif'");
	  goto error;
	}

	pop_condition_stack ();

	utils_free (line);
	goto read_next_line;
      }
    }

    if (condition_stack
	&& !(condition_stack->is_true ^ condition_stack->is_in_else_clause)) {
      /* In conditioned block and with false condition: skip line. */
      utils_free (line);
      goto read_next_line;
    }

    string_list_add_from_buffer (&lines, beginning, end - beginning);
    utils_free (line);
  }

  last_line_reusable = 0;
  return lines.last->text;

 error:
  string_list_empty (&list_files);
  utils_free (line);

  return NULL;
}


static const PredefinedCondition *
get_condition (char **line)
{
  const PredefinedCondition *condition;
  const char *condition_identifier = parse_thing (IDENTIFIER, line,
						  "condition identifier");

  if (!condition_identifier)
    return NULL;

  for (condition = predefined_conditions; condition->identifier; condition++) {
    if (strcmp (condition->identifier, condition_identifier) == 0)
      return condition;
  }

  print_error ("unknown condition identifier `%s'", condition_identifier);
  return NULL;
}


const char *
parse_thing (Thing thing, char **line, const char *type)
{
  const char *value;

  while (*line && ! **line)
    *line = read_line ();

  if (! *line) {
    print_error ("%s expected", type);
    return NULL;
  }

  value = *line;

  if (thing != STRING && thing != STRING_OR_NULL
      && (thing != STRING_OR_IDENTIFIER || **line != '"')) {
    do {
      int expected_character;

      switch (thing) {
      case IDENTIFIER:
      case STRING_OR_IDENTIFIER:
	expected_character = (isalpha (**line) || **line == '_'
			      || (*line != value && isdigit (**line)));
	break;

      case PROPERTY_IDENTIFIER:
	expected_character = isupper (**line);
	break;

      case FIELD_NAME:
	expected_character = (isalpha (**line)
			      || **line == '_' || ** line == '['
			      || (*line != value && (isdigit (**line)
						     || **line == '.'
						     || **line == ']')));
	break;

      case INTEGER_NUMBER:
	expected_character = isdigit (**line);
	break;

      case FLOATING_POINT_NUMBER:
	expected_character = isdigit (**line) || **line == '.';
	break;

      case TIME_VALUE:
	expected_character = (isdigit (**line)
			      || **line == ':' || **line == '.');
	break;

      default:
	assert (0);
      }

      if (!expected_character) {
	print_error ("unexpected character '%c' in %s", **line, type);
	return NULL;
      }

      (*line)++;
    } while (**line && !isspace (**line));
  }
  else {
    if (thing == STRING_OR_NULL) {
      char *possible_null = *line;

      if (looking_at ("NULL", line)) {
	*(possible_null + 4) = 0;
	return possible_null;
      }
    }

    if (**line != '"') {
      print_error ("string%s expected",
		   (thing == STRING ? "" : (thing == STRING_OR_NULL
					    ? " or NULL" : "or identifier")));
      return NULL;
    }

    while (1) {
      (*line)++;

      if (! **line || (**line == '\\' && ! *(*line + 1))) {
	print_error ("unterminated string");
	return NULL;
      }

      if (**line == '"') {
	(*line)++;
	break;
      }

      if (**line == '\\')
	(*line)++;
    }
  }

  if (**line) {
    **line = 0;
    do
      (*line)++;
    while (isspace (**line));
  }

  return value;
}


char *
parse_multiline_string (char **line, const char *type,
			const char *line_separator, int null_allowed)
{
  const char *string_chunk;
  char *string;

  string_chunk = parse_thing (null_allowed ? STRING_OR_NULL : STRING,
			      line, type);
  if (!string_chunk)
    return NULL;

  string = utils_duplicate_string (string_chunk);
  if (*string != '"')
    return string;

  while (! **line) {
    *line = read_line ();

    if (! *line) {
      utils_free (string);
      return NULL;
    }

    if (**line == '"') {
      string_chunk = parse_thing (STRING, line, type);
      if (!string_chunk) {
	utils_free (string);
	return NULL;
      }

      string = utils_cat_strings (string, line_separator, string_chunk, NULL);
    }
    else {
      reuse_last_line (line);
      break;
    }
  }

  return string;
}


int
parse_color (char **line, QuarryColor *color, const char *type)
{
  int num_digits;
  int red;
  int green;
  int blue;

  while (*line && ! **line)
    *line = read_line ();

  if (! *line || * (*line)++ != '#') {
    print_error ("%s expected", type);
    return 0;
  }

  for (num_digits = 0; isxdigit (**line) && num_digits <= 6; num_digits++)
    (*line)++;

  if ((num_digits != 6 && num_digits != 3) || (**line && !isspace (**line))) {
    print_error ("%s expected", type);
    return 0;
  }

  if (num_digits == 6)
    sscanf ((*line) - 6, "%2x%2x%2x", &red, &green, &blue);
  else {
    sscanf ((*line) - 3, "%1x%1x%1x", &red, &green, &blue);
    red   *= 0x11;
    green *= 0x11;
    blue  *= 0x11;
  }

  while (isspace (**line))
    (*line)++;

  color->red   = red;
  color->green = green;
  color->blue  = blue;

  return 1;
}


int
looking_at (const char *what, char **line)
{
  int length = strlen (what);

  while (*line && ! **line)
    *line = read_line ();

  if (*line && strncmp (*line, what, length) == 0
      && (!(*line) [length] || isspace ((*line) [length]))) {
    *line += length;
    while (isspace (**line))
      (*line)++;

    return 1;
  }

  return 0;
}


inline static void
push_condition_stack (const PredefinedCondition *condition, int is_negated)
{
  ConditionStackEntry *entry = utils_malloc (sizeof (ConditionStackEntry));

  entry->condition	   = condition;
  entry->is_true	   = (condition->value != 0) ^ (is_negated != 0);
  entry->is_in_else_clause = 0;

  entry->next		   = condition_stack;
  condition_stack	   = entry;
}


inline static void
pop_condition_stack (void)
{
  ConditionStackEntry *entry = condition_stack;

  condition_stack = entry->next;
  utils_free (entry);
}


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
