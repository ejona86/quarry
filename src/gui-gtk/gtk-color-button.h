/*
 * GTK - The GIMP Toolkit
 * Copyright (C) 1998, 1999 Red Hat, Inc.
 * All rights reserved.
 *
 * This Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Gnome Library; see the file COPYING.LIB.  If
 * not, write to the Free Software Foundation, Inc., 51 Franklin
 * Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
/* Color picker button for GNOME
 *
 * Author: Federico Mena <federico@nuclecu.unam.mx>
 *
 * Modified by the GTK+ Team and others 2003.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 */

/* 2005-05-07  Paul Pogonyshev
 *
 *	Update FSF address in the legal notice above.
 *
 * 2004-04-27  Paul Pogonyshev
 *
 *	Move `GtkColorButtonPrivate' into `GtkColorButton' (apparently
 *	pre-2.4 GLib doesn't have support for privates).
 *
 * 2004-04-26  Paul Pogonyshev
 *
 *	Include into Quarry to backport to pre-2.4 GTK+.  Modify to
 *	compile with Quarry.  Modify to exclude all declarations if
 *	compiling with GTK+ 2.4 or later.
 */


#include "gtk-utils.h"
#include "quarry.h"


#if !GTK_2_4_OR_LATER


#ifndef __GTK_COLOR_BUTTON_H__
#define __GTK_COLOR_BUTTON_H__


#include <gtk/gtk.h>

G_BEGIN_DECLS


/* The GtkColorSelectionButton widget is a simple color picker in a button.  
 * The button displays a sample of the currently selected color.  When 
 * the user clicks on the button, a color selection dialog pops up.  
 * The color picker emits the "color_set" signal when the color is set.
 */

#define GTK_TYPE_COLOR_BUTTON             (gtk_color_button_get_type ())
#define GTK_COLOR_BUTTON(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_COLOR_BUTTON, GtkColorButton))
#define GTK_COLOR_BUTTON_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_COLOR_BUTTON, GtkColorButtonClass))
#define GTK_IS_COLOR_BUTTON(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_COLOR_BUTTON))
#define GTK_IS_COLOR_BUTTON_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_COLOR_BUTTON))
#define GTK_COLOR_BUTTON_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_COLOR_BUTTON, GtkColorButtonClass))

typedef struct _GtkColorButtonPrivate   GtkColorButtonPrivate;
typedef struct _GtkColorButton          GtkColorButton;
typedef struct _GtkColorButtonClass     GtkColorButtonClass;

struct _GtkColorButtonPrivate 
{
  GdkPixbuf *pixbuf;    /* Pixbuf for rendering sample */
  GdkGC *gc;            /* GC for drawing */
  
  GtkWidget *drawing_area;/* Drawing area for color sample */
  GtkWidget *cs_dialog; /* Color selection dialog */
  
  gchar *title;         /* Title for the color selection window */
  
  GdkColor color;
  guint16 alpha;
  
  guint use_alpha : 1;  /* Use alpha or not */
};

struct _GtkColorButton {
  GtkButton button;

  /*< private >*/

  GtkColorButtonPrivate *priv;
  GtkColorButtonPrivate  the_priv;
};

struct _GtkColorButtonClass {
  GtkButtonClass parent_class;
  
  void (* color_set) (GtkColorButton *cp);
  
  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};


GType      gtk_color_button_get_type       (void) G_GNUC_CONST;
GtkWidget *gtk_color_button_new            (void);
GtkWidget *gtk_color_button_new_with_color (const GdkColor *color);
void       gtk_color_button_set_color      (GtkColorButton *color_button, 
					    const GdkColor *color);
void       gtk_color_button_set_alpha      (GtkColorButton *color_button,
					    guint16         alpha);
void       gtk_color_button_get_color      (GtkColorButton *color_button, 
					    GdkColor       *color);
guint16    gtk_color_button_get_alpha      (GtkColorButton *color_button);
void       gtk_color_button_set_use_alpha  (GtkColorButton *color_button, 
					    gboolean        use_alpha);
gboolean   gtk_color_button_get_use_alpha  (GtkColorButton *color_button);
void       gtk_color_button_set_title      (GtkColorButton *color_button, 
					    const gchar    *title);
G_CONST_RETURN gchar *gtk_color_button_get_title (GtkColorButton *color_button);


G_END_DECLS

#endif  /* __GTK_COLOR_BUTTON_H__ */


#endif /* !GTK_2_4_OR_LATER */
