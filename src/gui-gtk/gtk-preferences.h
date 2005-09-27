/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This file is part of Quarry.                                    *
 *                                                                 *
 * Copyright (C) 2003, 2004, 2005 Paul Pogonyshev.                 *
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


#ifndef QUARRY_GTK_PREFERENCES_H
#define QUARRY_GTK_PREFERENCES_H


#include "gtk-configuration.h"
#include "gtk-games.h"
#include "gui-back-end.h"
#include "gtp-client.h"
#include "quarry.h"

#include <gtk/gtk.h>


enum {
  PREFERENCES_PAGE_GTK_UI,
  PREFERENCES_PAGE_GAME_TREE,
  PREFERENCES_PAGE_GTP_ENGINES,
  PREFERENCES_PAGE_SGF_SAVING,
  PREFERENCES_PAGE_GO_BOARD_APPEARANCE,
  PREFERENCES_PAGE_AMAZONS_BOARD_APPEARANCE,
  PREFERENCES_PAGE_REVERSI_BOARD_APPEARANCE,

  NUM_PREFERENCES_DIALOG_PAGES
};


typedef void (* GtkEngineChanged) (GtkWidget *selector, gpointer user_data);


typedef enum {
  ENGINES_INSTANTIATED,
  INSTANTIATION_FAILED,
  INSTANTIATION_CANCELLED
} GtkEnginesInstantiationStatus;

typedef void (* GtkEnginesInstantiated) (GtkEnginesInstantiationStatus status,
					 gpointer user_data);

typedef struct _GtkEngineChain	GtkEngineChain;

struct _GtkEngineChain {
  GtkWindow		  *parent_window;

  GtkEnginesInstantiated   instantiation_callback;
  gpointer		   user_data;

  GSList		  *chain_engine_datum;
  gboolean		   have_error;
};


void		     gtk_preferences_init (void);

void		     gtk_preferences_dialog_present (gpointer page_to_select);

void		     gtk_preferences_register_main_toolbar
		       (GtkToolbar *toolbar);
void		     gtk_preferences_register_editing_toolbar
		       (GtkToolbar *toolbar);
void		     gtk_preferences_register_navigation_toolbar
		       (GtkToolbar *toolbar);

BoardAppearance *    game_to_board_appearance_structure (Game game);

gboolean	     gtk_preferences_have_non_hidden_gtp_engine (void);
GtpEngineListItem *  gtk_preferences_guess_engine_by_name
		       (const gchar *name, GtkGameIndex game_index);

GtkWidget *	     gtk_preferences_create_engine_selector
		       (GtkGameIndex game_index, gboolean only_this_game,
			const GtpEngineListItem *engine_data,
			GtkEngineChanged callback, gpointer user_data);
void		     gtk_preferences_set_engine_selector_game_index
		       (GtkWidget *selector, GtkGameIndex game_index);
void		     gtk_preferences_set_engine_selector_selection
		       (GtkWidget *selector,
			const GtpEngineListItem *engine_data);
GtpEngineListItem *  gtk_preferences_get_engine_selector_selection
		       (GtkWidget *selector);


GtkEngineChain *     gtk_preferences_create_engines_instantiation_chain
		       (GtkWindow *parent_window,
			GtkEnginesInstantiated callback, gpointer user_data);
void		     gtk_preferences_instantiate_selected_engine
		       (GtkEngineChain *engine_chain,
			GtkWidget *selector, GtpClient **gtp_client);
void		     gtk_preferences_do_instantiate_engines
		       (GtkEngineChain *engine_chain);


#endif /* QUARRY_GTK_PREFERENCES_H */


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
