/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This file is part of Quarry.                                    *
 *                                                                 *
 * Copyright (C) 2003, 2004 Paul Pogonyshev.                       *
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
 * Software Foundation, Inc., 59 Temple Place - Suite 330,         *
 * Boston, MA 02111-1307, USA.                                     *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#ifndef QUARRY_GTK_GOBAN_H
#define QUARRY_GTK_GOBAN_H


#include "gtk-tile-set-interface.h"
#include "board.h"
#include "quarry.h"

#include <gtk/gtk.h>


#define GTK_TYPE_GOBAN		(gtk_goban_get_type())
#define GTK_GOBAN(obj)		(GTK_CHECK_CAST((obj), GTK_TYPE_GOBAN,	\
						GtkGoban))
#define GTK_GOBAN_CLASS(klass)						\
  (GTK_CHECK_CLASS_CAST((klass), GTK_TYPE_GOBAN, GtkGobanClass))

#define GTK_IS_GOBAN(obj)	(GTK_CHECK_TYPE((obj), GTK_TYPE_GOBAN))
#define GTK_IS_GOBAN_CLASS(klass)					\
  (GTK_CHECK_CLASS_TYPE((klass), GTK_TYPE_GOBAN))

#define GTK_GOBAN_GET_CLASS(obj)					\
  (GTK_CHECK_GET_CLASS((obj), GTK_TYPE_GOBAN, GtkGobanClass))


typedef struct _GtkGobanPointerData	GtkGobanPointerData;
typedef struct _GtkGobanClickData	GtkGobanClickData;

struct _GtkGobanPointerData {
  gint		   x;
  gint		   y;
  GdkModifierType  modifiers;
  gint		   button;
  gint		   press_x;
  gint		   press_y;
};

struct _GtkGobanClickData {
  gint		   x;
  gint		   y;
  gint		   feedback_tile;
  GdkModifierType  modifiers;
  gint		   button;
};

typedef enum {
  GOBAN_FEEDBACK_NONE,
  GOBAN_FEEDBACK_GHOST,
  GOBAN_FEEDBACK_BLACK_GHOST = GOBAN_FEEDBACK_GHOST + BLACK_INDEX,
  GOBAN_FEEDBACK_WHITE_GHOST = GOBAN_FEEDBACK_GHOST + WHITE_INDEX,
  GOBAN_FEEDBACK_THICK_GHOST,
  GOBAN_FEEDBACK_THICK_BLACK_GHOST = GOBAN_FEEDBACK_THICK_GHOST + BLACK_INDEX,
  GOBAN_FEEDBACK_THICK_WHITE_GHOST = GOBAN_FEEDBACK_THICK_GHOST + WHITE_INDEX,
  GOBAN_FEEDBACK_PRESS_DEFAULT,
  GOBAN_FEEDBACK_MOVE_DEFAULT,
  GOBAN_FEEDBACK_BLACK_MOVE_DEFAULT = GOBAN_FEEDBACK_MOVE_DEFAULT + BLACK_INDEX,
  GOBAN_FEEDBACK_WHITE_MOVE_DEFAULT = GOBAN_FEEDBACK_MOVE_DEFAULT + WHITE_INDEX,
  GOBAN_FEEDBACK_SPECIAL,
  NUM_GOBAN_FEEDBACKS
} GtkGobanPointerFeedback;

/* Note that there _must not_ be zero enumeration element.  These
 * constants are used in `gtk-goban-window.c' in `callback_action'
 * field of GtkItemFactoryEntry and zero has special meaning in that
 * context.
 */
typedef enum {
  GOBAN_NAVIGATE_BACK = 1,
  GOBAN_NAVIGATE_BACK_FAST,
  GOBAN_NAVIGATE_FORWARD,
  GOBAN_NAVIGATE_FORWARD_FAST,
  GOBAN_NAVIGATE_PREVIOUS_VARIATION,
  GOBAN_NAVIGATE_NEXT_VARIATION,
  GOBAN_NAVIGATE_ROOT,
  GOBAN_NAVIGATE_VARIATION_END
} GtkGobanNavigationCommand;

/* Goban markup flags controlling appearance of stones in marked
 * positions.  Tiles are enumerated in `gui-utils/tile-set.h'.
 */
#if NUM_TILES >= 1 << 4
#error Too many tiles, need to adjust GOBAN_MARKUP_* flags.
#endif

enum {
  GOBAN_MARKUP_GHOSTIFY = 1 << 4,
  GOBAN_MARKUP_GHOSTIFY_SLIGHTLY = 1 << 5,

  GOBAN_MARKUP_FLAGS_MASK = (GOBAN_MARKUP_GHOSTIFY
			     | GOBAN_MARKUP_GHOSTIFY_SLIGHTLY),
  GOBAN_MARKUP_TILE_MASK = ~GOBAN_MARKUP_FLAGS_MASK
};


#define NUM_OVERLAYS		4
#define FEEDBACK_OVERLAY	(NUM_OVERLAYS - 1)


typedef struct _GtkGoban		GtkGoban;
typedef struct _GtkGobanClass		GtkGobanClass;

struct _GtkGoban {
  GtkWidget		 widget;

  Game			 game;
  int			 width;
  int			 height;

  char			 grid[BOARD_GRID_SIZE];
  char			 goban_markup[BOARD_GRID_SIZE];
  char			 sgf_markup[BOARD_GRID_SIZE];

  int			 overlay_pos[NUM_OVERLAYS];
  char			 overlay_contents[NUM_OVERLAYS];

  int			 last_move_x;
  int			 last_move_y;

  gint			 cell_size;
  gint			 small_cell_size;

  PangoFontDescription  *font_description;
  gint			 font_size;
  gint			 character_height;
  gint			 digit_width;

  gint			 left_margin;
  gint			 right_margin;
  gint			 top_margin;
  gint			 bottom_margin;
  gint			 first_cell_center_x;
  gint			 first_cell_center_y;

  gint			 coordinates_x_left;
  gint			 coordinates_x_right;
  gint			 coordinates_y_side;
  gint			 coordinates_y_top;
  gint			 coordinates_y_bottom;

  gint			 stones_left_margin;
  gint			 stones_top_margin;
  gint			 small_stones_left_margin;
  gint			 small_stones_top_margin;

  int			 num_hoshi_points;
  int			 hoshi_point_x[9];
  int			 hoshi_point_y[9];

  TileSet		*main_tile_set;
  TileSet		*small_tile_set;

  int			 pointer_x;
  int			 pointer_y;
  int			 feedback_tile;

  GdkModifierType	 modifiers;
  unsigned int		 button_pressed;
  gint			 press_x;
  gint			 press_y;
  GdkModifierType	 press_modifiers;
  int			 feedback_tile_at_press;
};

struct _GtkGobanClass {
  GtkWidgetClass  parent_class;

  GtkGobanPointerFeedback (* pointer_moved) (GtkGoban *goban,
					     GtkGobanPointerData *data);
  void (* goban_clicked) (GtkGoban *goban, GtkGobanClickData *data);

  void (* navigate) (GtkGoban *goban, GtkGobanNavigationCommand command);
};


GtkType 	gtk_goban_get_type(void);

GtkWidget *	gtk_goban_new(void);

void		gtk_goban_set_parameters(GtkGoban *goban, Game game,
					 int width, int height);

void		gtk_goban_update(GtkGoban *goban,
				 const char grid[BOARD_GRID_SIZE],
				 const char goban_markup[BOARD_GRID_SIZE],
				 const char sgf_markup[BOARD_GRID_SIZE],
				 int last_move_x, int last_move_y);
void		gtk_goban_force_feedback_poll(GtkGoban *goban);

void		gtk_goban_set_overlay_data(GtkGoban *goban, int overlay_index,
					   int x, int y, int tile);

gint		gtk_goban_negotiate_width(GtkWidget *widget, gint height);
gint		gtk_goban_negotiate_height(GtkWidget *widget, gint width);


#endif /* QUARRY_GTK_GOBAN_H */


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
