/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This file is part of Quarry.                                    *
 *                                                                 *
 * Copyright (C) 2004 Paul Pogonyshev.                             *
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


#include "gtk-goban-base.h"
#include "gtk-sgf-tree-view.h"
#include "gtk-tile-set.h"
#include "gtk-utils.h"
#include "quarry-marshal.h"
#include "sgf.h"
#include "board.h"
#include "utils.h"

#include <assert.h>
#include <gtk/gtk.h>

#if HAVE_MEMORY_H
#include <memory.h>
#endif


/* FIXME: Make it zoomable and delete this (or, rather, throw them to
 * `gtk-configuration.list'.
 */
#define DEFAULT_CELL_SIZE	24


#define PADDING_WIDTH(cell_size)					\
  ((gint) (((cell_size) + 6) / 12))

#define FULL_CELL_SIZE(view)						\
  ((view)->base.cell_size + 2 * PADDING_WIDTH ((view)->base.cell_size))


static void	 gtk_sgf_tree_view_class_init (GtkSgfTreeViewClass *class);
static void	 gtk_sgf_tree_view_init (GtkSgfTreeView *view);
static void	 gtk_sgf_tree_view_realize (GtkWidget *widget);

static void	 gtk_sgf_tree_view_size_request (GtkWidget *widget,
						 GtkRequisition *requisition);
static void	 gtk_sgf_tree_view_size_allocate (GtkWidget *widget,
						  GtkAllocation  *allocation);

static void	 gtk_sgf_tree_view_set_scroll_adjustments
		   (GtkSgfTreeView *view,
		    GtkAdjustment *hadjustment, GtkAdjustment *vadjustment);

static gboolean	 gtk_sgf_tree_view_expose (GtkWidget *widget,
					   GdkEventExpose *event);

static gboolean	 gtk_sgf_tree_view_button_press_event (GtkWidget *widget,
						       GdkEventButton *event);
static gboolean	 gtk_sgf_tree_view_button_release_event
		   (GtkWidget *widget, GdkEventButton *event);

static void	 gtk_sgf_tree_view_unrealize (GtkWidget *widget);

static void	 gtk_sgf_tree_view_finalize (GObject *object);
static void	 gtk_sgf_tree_view_destroy (GtkObject *object);


static void	 configure_adjustment (GtkSgfTreeView *view,
				       gboolean horizontal);
static void	 disconnect_adjustment (GtkSgfTreeView *view,
					GtkAdjustment *adjustment);
static void	 scroll_adjustment_value_changed (GtkSgfTreeView *view);

static void	 update_view_port (GtkSgfTreeView *view);
static gboolean	 update_view_port_and_maybe_move_or_resize_window
		   (GtkSgfTreeView *view);


static GtkGobanBaseClass  *parent_class;


enum {
  SGF_TREE_VIEW_CLICKED,
  NUM_SIGNALS
};

static guint		   sgf_tree_view_signals[NUM_SIGNALS];


GType
gtk_sgf_tree_view_get_type (void)
{
  static GType sgf_tree_view_type = 0;

  if (!sgf_tree_view_type) {
    static GTypeInfo sgf_tree_view_info = {
      sizeof (GtkSgfTreeViewClass),
      NULL,
      NULL,
      (GClassInitFunc) gtk_sgf_tree_view_class_init,
      NULL,
      NULL,
      sizeof (GtkSgfTreeView),
      1,
      (GInstanceInitFunc) gtk_sgf_tree_view_init,
      NULL
    };

    sgf_tree_view_type = g_type_register_static (GTK_TYPE_GOBAN_BASE,
						 "GtkSgfTreeView",
						 &sgf_tree_view_info, 0);
  }

  return sgf_tree_view_type;
}


static void
gtk_sgf_tree_view_class_init (GtkSgfTreeViewClass *class)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  G_OBJECT_CLASS (class)->finalize = gtk_sgf_tree_view_finalize;

  GTK_OBJECT_CLASS (class)->destroy = gtk_sgf_tree_view_destroy;

  widget_class->realize		     = gtk_sgf_tree_view_realize;
  widget_class->unrealize	     = gtk_sgf_tree_view_unrealize;
  widget_class->size_request	     = gtk_sgf_tree_view_size_request;
  widget_class->size_allocate	     = gtk_sgf_tree_view_size_allocate;
  widget_class->expose_event	     = gtk_sgf_tree_view_expose;
  widget_class->button_press_event   = gtk_sgf_tree_view_button_press_event;
  widget_class->button_release_event = gtk_sgf_tree_view_button_release_event;

  class->set_scroll_adjustments	= gtk_sgf_tree_view_set_scroll_adjustments;
  class->sgf_tree_view_clicked	= NULL;

  widget_class->set_scroll_adjustments_signal
    = g_signal_new ("set-scroll-adjustments",
		    G_TYPE_FROM_CLASS (class),
		    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		    G_STRUCT_OFFSET (GtkSgfTreeViewClass,
				     set_scroll_adjustments),
		    NULL, NULL,
		    quarry_marshal_VOID__OBJECT_OBJECT,
		    G_TYPE_NONE, 2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);

  sgf_tree_view_signals[SGF_TREE_VIEW_CLICKED]
    = g_signal_new ("sgf-tree-view-clicked",
		    G_TYPE_FROM_CLASS (class),
		    G_SIGNAL_RUN_LAST,
		    G_STRUCT_OFFSET (GtkSgfTreeViewClass,
				     sgf_tree_view_clicked),
		    NULL, NULL,
		    quarry_marshal_VOID__POINTER,
		    G_TYPE_NONE, 1, G_TYPE_POINTER);
}


static void
gtk_sgf_tree_view_init (GtkSgfTreeView *view)
{
  /* FIXME */
  gtk_goban_base_set_game (GTK_GOBAN_BASE (view), GAME_GO);
  gtk_goban_base_set_cell_size (GTK_GOBAN_BASE (view), DEFAULT_CELL_SIZE);

  view->hadjustment		  = NULL;
  view->vadjustment		  = NULL;
  view->ignore_adjustment_changes = FALSE;

  view->current_tree = NULL;
}


GtkWidget *
gtk_sgf_tree_view_new (void)
{
  return GTK_WIDGET (g_object_new (GTK_TYPE_SGF_TREE_VIEW, NULL));
}


static void
gtk_sgf_tree_view_realize (GtkWidget *widget)
{
  GtkSgfTreeView *view = GTK_SGF_TREE_VIEW (widget);
  GdkWindowAttr attributes;
  const gint attributes_mask = (GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL
				| GDK_WA_COLORMAP);
  const gint event_mask = (GDK_EXPOSURE_MASK
			   | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

  if (!view->hadjustment || !view->vadjustment)
    gtk_sgf_tree_view_set_scroll_adjustments (view, NULL, NULL);

  if (view->current_tree)
    update_view_port (view);

  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

  attributes.event_mask	 = gtk_widget_get_events (widget) | event_mask;
  attributes.x		 = widget->allocation.x;
  attributes.y		 = widget->allocation.y;
  attributes.width	 = widget->allocation.width;
  attributes.height	 = widget->allocation.height;
  attributes.wclass	 = GDK_INPUT_OUTPUT;
  attributes.visual	 = gtk_widget_get_visual (widget);
  attributes.colormap	 = gtk_widget_get_colormap (widget);
  attributes.window_type = GDK_WINDOW_CHILD;

  widget->window = gdk_window_new (gtk_widget_get_parent_window (widget),
				   &attributes, attributes_mask);
  gdk_window_set_user_data (widget->window, view);

  gdk_window_set_back_pixmap (widget->window, NULL, FALSE);

  attributes.x	    = - view->hadjustment->value;
  attributes.y	    = - view->vadjustment->value;
  attributes.width  = view->hadjustment->upper;
  attributes.height = view->vadjustment->upper;

  view->output_window = gdk_window_new (widget->window,
					&attributes, attributes_mask);
  gdk_window_set_user_data (view->output_window, view);

  widget->style = gtk_style_attach (widget->style, widget->window);
  gtk_style_set_background (widget->style, view->output_window,
			    GTK_STATE_NORMAL);

  gdk_window_show (view->output_window);
}


static void
gtk_sgf_tree_view_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
  GtkSgfTreeView *view = GTK_SGF_TREE_VIEW (widget);
  gint full_cell_size = FULL_CELL_SIZE (view);

  if (view->current_tree) {
    requisition->width  = view->current_tree->map_width * full_cell_size;
    requisition->height = view->current_tree->map_height * full_cell_size;
  }
  else {
    requisition->width = full_cell_size;
    requisition->width = full_cell_size;
  }
}


static void
gtk_sgf_tree_view_size_allocate (GtkWidget *widget, GtkAllocation  *allocation)
{
  GtkSgfTreeView *view = GTK_SGF_TREE_VIEW (widget);

  GTK_WIDGET_CLASS (parent_class)->size_allocate (widget, allocation);

  update_view_port_and_maybe_move_or_resize_window (view);
}


static void
gtk_sgf_tree_view_set_scroll_adjustments (GtkSgfTreeView *view,
					  GtkAdjustment *hadjustment,
					  GtkAdjustment *vadjustment)
{
  if (hadjustment)
    assert (GTK_IS_ADJUSTMENT (hadjustment));
  else {
    hadjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0,
						      0.0, 0.0, 0.0));
  }

  if (view->hadjustment != hadjustment) {
    disconnect_adjustment (view, view->hadjustment);

    view->hadjustment = hadjustment;
    g_object_ref (hadjustment);
    gtk_object_sink (GTK_OBJECT (hadjustment));

    configure_adjustment (view, TRUE);

    g_signal_connect_swapped (hadjustment, "value-changed",
			      G_CALLBACK (scroll_adjustment_value_changed),
			      view);
  }

  if (vadjustment)
    assert (GTK_IS_ADJUSTMENT (vadjustment));
  else {
    vadjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0,
						      0.0, 0.0, 0.0));
  }

  if (view->vadjustment != vadjustment) {
    disconnect_adjustment (view, view->vadjustment);

    view->vadjustment = vadjustment;
    g_object_ref (vadjustment);
    gtk_object_sink (GTK_OBJECT (vadjustment));

    configure_adjustment (view, FALSE);

    g_signal_connect_swapped (vadjustment, "value-changed",
			      G_CALLBACK (scroll_adjustment_value_changed),
			      view);
  }
}


static gboolean
gtk_sgf_tree_view_expose (GtkWidget *widget, GdkEventExpose *event)
{
  UNUSED (event);

  if (GTK_WIDGET_DRAWABLE (widget)) {
    GtkSgfTreeView *view = GTK_SGF_TREE_VIEW (widget);
    int x;
    int y;
    SgfNode **view_port_scan;
    int k;
    SgfGameTreeMapLine *lines_scan;
    gint full_cell_size = FULL_CELL_SIZE (view);
    gint full_cell_size_half = full_cell_size / 2;
    gint stones_left_margin = (full_cell_size / 2
			       + view->base.main_tile_set->stones_x_offset);
    gint stones_top_margin = (full_cell_size / 2
			      + view->base.main_tile_set->stones_x_offset);

    GdkWindow *window = view->output_window;
    GdkGC *gc = widget->style->fg_gc[GTK_STATE_NORMAL];

    assert (view->current_tree);
    assert (view->view_port_nodes);
    assert (view->view_port_lines);

    gdk_gc_set_line_attributes (gc, 2, GDK_LINE_SOLID,
				GDK_CAP_ROUND, GDK_JOIN_ROUND);

    for (lines_scan = view->view_port_lines, k = 0;
	 k < view->num_view_port_lines; lines_scan++, k++) {
      GdkPoint points[4];
      GdkPoint *next_point = points;
      GdkPoint *point_scan;
      gint y2 = lines_scan->y1 + (lines_scan->x2 - lines_scan->x0);

      /* The clipping below is a workaround for X drawing functions'
       * bug: they cannot handle line lengths over 32K properly.
       */

      if (lines_scan->x2 >= view->view_port_x0) {
	if (lines_scan->x0 >= view->view_port_x0) {
	  if (lines_scan->y1 >= view->view_port_y0) {
	    if (lines_scan->y0 < lines_scan->y1) {
	      next_point->x = lines_scan->x0;
	      next_point->y = MAX (lines_scan->y0, view->view_port_y0 - 1);
	      next_point++;
	    }

	    if (lines_scan->y1 < view->view_port_y1) {
	      next_point->x = lines_scan->x0;
	      next_point->y = lines_scan->y1;
	      next_point++;
	    }
	    else {
	      next_point->x = lines_scan->x0;
	      next_point->y = view->view_port_y1;
	      next_point++;

	      goto line_clipped;
	    }
	  }
	  else {
	    next_point->y = view->view_port_y0 - 1;
	    next_point->x = lines_scan->x0 + (next_point->y - lines_scan->y1);
	    next_point++;
	  }
	}
	else {
	  next_point->x = view->view_port_x0 - 1;
	  next_point->y = lines_scan->y1 + (next_point->x - lines_scan->x0);
	  next_point++;
	}

	if (y2 > lines_scan->y1) {
	  if ((view->view_port_x1 - (next_point - 1)->x)
	      > (view->view_port_y1 - (next_point - 1)->y)) {
	    next_point->y = MIN (y2, view->view_port_y1);
	    next_point->x = lines_scan->x0 + (next_point->y - lines_scan->y1);
	    next_point++;
	  }
	  else {
	    next_point->x = MIN (lines_scan->x2, view->view_port_x1);
	    next_point->y = lines_scan->y1 + (next_point->x - lines_scan->x0);
	    next_point++;
	  }
	}
      }
      else {
	next_point->x = view->view_port_x0 - 1;
	next_point->y = y2;
	next_point++;
      }

      if (y2 < view->view_port_y1) {
	next_point->x = MIN (lines_scan->x3, view->view_port_x1);
	next_point->y = y2;
	next_point++;
      }

    line_clipped:

      for (point_scan = points; point_scan < next_point; point_scan++) {
	point_scan->x = point_scan->x * full_cell_size + full_cell_size_half;
	point_scan->y = point_scan->y * full_cell_size + full_cell_size_half;
      }

      gdk_draw_lines (window, gc, points, next_point - points);
    }

    for (view_port_scan = view->view_port_nodes, y = view->view_port_y0;
	 y < view->view_port_y1; y++) {
      for (x = view->view_port_x0; x < view->view_port_x1; x++) {
	const SgfNode *sgf_node = *view_port_scan++;

	if (sgf_node) {
	  if (sgf_node == view->current_tree->current_node) {
	    /* FIXME: Improve. */
	    gdk_draw_rectangle (window, gc, FALSE,
				x * full_cell_size + 1, y * full_cell_size + 1,
				full_cell_size - 2, full_cell_size - 2);

	  }

	  if (IS_STONE (sgf_node->move_color)) {
	    gdk_draw_pixbuf
	      (window, gc,
	       view->base.main_tile_set->tiles[sgf_node->move_color],
	       0, 0,
	       stones_left_margin + x * full_cell_size,
	       stones_top_margin + y * full_cell_size,
	       -1, -1, GDK_RGB_DITHER_NORMAL, 0, 0);
	  }
	}
      }
    }

    gdk_gc_set_line_attributes (gc, 0, GDK_LINE_SOLID,
				GDK_CAP_BUTT, GDK_JOIN_MITER);
  }

  return FALSE;
}


static gboolean
gtk_sgf_tree_view_button_press_event (GtkWidget *widget, GdkEventButton *event)
{
  if (event->button == 1) {
    GtkSgfTreeView *view = GTK_SGF_TREE_VIEW (widget);
    gint full_cell_size = FULL_CELL_SIZE (view);
    gint x;
    gint y;

    gdk_window_get_pointer (view->output_window, &x, &y, NULL);
    x /= full_cell_size;
    y /= full_cell_size;

    if (view->button_pressed == 0) {
      view->button_pressed  = event->button;
      view->press_x	    = x;
      view->press_y	    = y;
    }
    else
      view->button_pressed = -1;
  }

  return FALSE;
}


static gboolean
gtk_sgf_tree_view_button_release_event (GtkWidget *widget,
					GdkEventButton *event)
{
  if (event->button == 1) {
    GtkSgfTreeView *view = GTK_SGF_TREE_VIEW (widget);
    gint full_cell_size = FULL_CELL_SIZE (view);
    gint x;
    gint y;

    gdk_window_get_pointer (view->output_window, &x, &y, NULL);
    x /= full_cell_size;
    y /= full_cell_size;

    if (view->button_pressed == event->button) {
      view->button_pressed = 0;

      if (x == view->press_x && y == view->press_y) {
	SgfNode *clicked_node = * (view->view_port_nodes
				   + ((y - view->view_port_y0)
				      * (view->view_port_x1
					 - view->view_port_x0))
				   + (x - view->view_port_x0));

	if (clicked_node) {
	  g_signal_emit (G_OBJECT (view),
			 sgf_tree_view_signals[SGF_TREE_VIEW_CLICKED], 0,
			 clicked_node);
	}
      }
    }
    else
      view->button_pressed = 0;
  }

  return FALSE;
}


static void
gtk_sgf_tree_view_unrealize (GtkWidget *widget)
{
  GtkSgfTreeView *view = GTK_SGF_TREE_VIEW (widget);

  gdk_window_set_user_data (view->output_window, NULL);
  gdk_window_destroy (view->output_window);

  GTK_WIDGET_CLASS (parent_class)->unrealize (widget);
}


static void
gtk_sgf_tree_view_destroy (GtkObject *object)
{
  GtkSgfTreeView *view = GTK_SGF_TREE_VIEW (object);

  disconnect_adjustment (view, view->hadjustment);
  disconnect_adjustment (view, view->vadjustment);

  GTK_OBJECT_CLASS (parent_class)->destroy (object);
}


static void
gtk_sgf_tree_view_finalize (GObject *object)
{
  GtkSgfTreeView *view = GTK_SGF_TREE_VIEW (object);

  disconnect_adjustment (view, view->hadjustment);
  disconnect_adjustment (view, view->vadjustment);

  utils_free (view->view_port_nodes);
  utils_free (view->view_port_lines);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}



void
gtk_sgf_tree_view_set_sgf_tree (GtkSgfTreeView *view, SgfGameTree *sgf_tree)
{
  assert (GTK_IS_SGF_TREE_VIEW (view));
  assert (sgf_tree);

  view->current_tree = sgf_tree;

  sgf_game_tree_activate_map (sgf_tree);

  if (!view->hadjustment || !view->vadjustment)
    gtk_sgf_tree_view_set_scroll_adjustments (view, NULL, NULL);
}


void
gtk_sgf_tree_view_update_view_port (GtkSgfTreeView *view)
{
  assert (GTK_IS_SGF_TREE_VIEW (view));

  if (GTK_WIDGET_REALIZED (view)) {
    SgfNode **view_port_nodes = view->view_port_nodes;
    SgfNode *tree_current_node = view->tree_current_node;
    SgfGameTreeMapLine *view_port_lines = view->view_port_lines;

    assert (view->current_tree);

    view->view_port_nodes = NULL;
    view->view_port_lines = NULL;

    if (!update_view_port_and_maybe_move_or_resize_window (view)
	&& (!view_port_nodes || !view_port_lines
	    || (memcmp (view->view_port_nodes, view_port_nodes,
			((view->view_port_x1 - view->view_port_x0)
			 * (view->view_port_y1 - view->view_port_y0)
			 * sizeof (SgfNode *)))
		!= 0)
	    || (memcmp (view->view_port_lines, view_port_lines,
			(view->num_view_port_lines
			 * sizeof (SgfGameTreeMapLine)))
		!= 0)))
      gtk_widget_queue_draw (GTK_WIDGET (view));
    else if (tree_current_node != view->tree_current_node) {
      gint full_cell_size = FULL_CELL_SIZE (view);
      GdkRectangle rectangle;
      SgfNode **view_port_scan;
      gint x;
      gint y;

      rectangle.width  = full_cell_size;
      rectangle.height = full_cell_size;

      for (view_port_scan = view->view_port_nodes, y = view->view_port_y0;
	   y < view->view_port_y1; y++) {
	for (x = view->view_port_x0; x < view->view_port_x1; x++) {
	  const SgfNode *sgf_node = *view_port_scan++;

	  if (sgf_node == tree_current_node
	      || sgf_node == view->tree_current_node) {
	    rectangle.x = x * full_cell_size;
	    rectangle.y = y * full_cell_size;

	    gdk_window_invalidate_rect (view->output_window, &rectangle,
					FALSE);
	  }
	}
      }
    }

    utils_free (view_port_nodes);
    utils_free (view_port_lines);
  }
}



static void
configure_adjustment (GtkSgfTreeView *view, gboolean horizontal)
{
  GtkAdjustment *adjustment = (horizontal
			       ? view->hadjustment : view->vadjustment);
  GtkWidget *widget = GTK_WIDGET (view);
  gboolean adjustment_has_changed = FALSE;
  gboolean value_has_changed = FALSE;
  gint page_size = (horizontal
		    ? widget->allocation.width : widget->allocation.height);
  gint upper = page_size;
  gint full_cell_size = FULL_CELL_SIZE (view);

  if (adjustment->lower != 0.0) {
    adjustment->lower = 0.0;
    adjustment_has_changed = TRUE;
  }

  if (view->current_tree) {
    upper = MAX (upper,
		 (horizontal
		  ? view->current_tree->map_width
		  : view->current_tree->map_height)
		 * full_cell_size);
  }

  if ((gint) adjustment->upper != upper) {
    adjustment->upper = upper;
    adjustment_has_changed = TRUE;
  }

  if ((gint) adjustment->page_size != page_size) {
    adjustment->page_size = page_size;
    adjustment_has_changed = TRUE;
  }

  if ((gint) adjustment->page_increment != (gint) (page_size * 0.9)) {
    adjustment->page_increment = (gint) (page_size * 0.9);
    adjustment_has_changed = TRUE;
  }

  if ((gint) adjustment->step_increment != full_cell_size) {
    adjustment->step_increment = full_cell_size;
    adjustment_has_changed = TRUE;
  }

  if (adjustment->value < 0.0 || (upper - page_size) < adjustment->value) {
    adjustment->value = (adjustment->value < 0.0 ? 0.0 : upper - page_size);
    value_has_changed = TRUE;
  }

  if (adjustment_has_changed)
    gtk_adjustment_changed (adjustment);

  if (value_has_changed)
    gtk_adjustment_value_changed (adjustment);
}


static void
disconnect_adjustment (GtkSgfTreeView *view, GtkAdjustment *adjustment)
{
  if (adjustment) {
    g_signal_handlers_disconnect_by_func (adjustment,
					  scroll_adjustment_value_changed,
					  view);

    g_object_unref (adjustment);

    if (view->hadjustment == adjustment)
      view->hadjustment = NULL;
    else
      view->vadjustment = NULL;
  }
}


static void
scroll_adjustment_value_changed (GtkSgfTreeView *view)
{
  if (GTK_WIDGET_REALIZED (view) && !view->ignore_adjustment_changes) {
    gint old_x_value;
    gint old_y_value;
    gint new_x_value = - view->hadjustment->value;
    gint new_y_value = - view->vadjustment->value;

    gdk_window_get_position (view->output_window, &old_x_value, &old_y_value);

    if (old_x_value != new_x_value || old_y_value != new_y_value) {
      update_view_port (view);

      gdk_window_move (view->output_window, new_x_value, new_y_value);
      gdk_window_process_updates (view->output_window, FALSE);
    }
  }
}


static void
update_view_port (GtkSgfTreeView *view)
{
  GtkAllocation *allocation = & GTK_WIDGET (view)->allocation;
  gint full_cell_size = FULL_CELL_SIZE (view);

  view->view_port_x0 = (gint) view->hadjustment->value / full_cell_size;
  view->view_port_y0 = (gint) view->vadjustment->value / full_cell_size;
  view->view_port_x1 = (((gint) view->hadjustment->value + allocation->width
			 + full_cell_size - 1)
			/ full_cell_size);
  view->view_port_y1 = (((gint) view->vadjustment->value + allocation->height
			 + full_cell_size - 1)
			/ full_cell_size);

  utils_free (view->view_port_nodes);
  utils_free (view->view_port_lines);

  sgf_game_tree_update_view_port (view->current_tree,
				  view->view_port_x0, view->view_port_y0,
				  view->view_port_x1, view->view_port_y1,
				  &view->view_port_nodes,
				  &view->view_port_lines,
				  &view->num_view_port_lines);
  view->tree_current_node = view->current_tree->current_node;
}


static gboolean
update_view_port_and_maybe_move_or_resize_window (GtkSgfTreeView *view)
{
  gint original_hadjustment_upper = view->hadjustment->upper;
  gint original_vadjustment_upper = view->vadjustment->upper;
  gint original_hadjustment_value = view->hadjustment->value;
  gint original_vadjustment_value = view->vadjustment->value;
  gboolean need_to_resize_window;
  gboolean need_to_move_window;

  update_view_port (view);

  view->ignore_adjustment_changes = TRUE;

  configure_adjustment (view, TRUE);
  configure_adjustment (view, FALSE);

  view->ignore_adjustment_changes = FALSE;

  need_to_resize_window = ((original_hadjustment_upper
			    != (gint) view->hadjustment->upper)
			   || (original_vadjustment_upper
			       != (gint) view->vadjustment->upper));
  need_to_move_window	= ((original_hadjustment_value
			    != (gint) view->hadjustment->value)
			   || (original_vadjustment_value
			       != (gint) view->vadjustment->value));

  if (need_to_move_window)
    update_view_port (view);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (view))
      && (need_to_resize_window || need_to_move_window)) {
    if (need_to_resize_window) {
      if (need_to_move_window) {
	gdk_window_move_resize (view->output_window,
				- (gint) view->hadjustment->value,
				- (gint) view->vadjustment->value,
				(gint) view->hadjustment->upper,
				(gint) view->vadjustment->upper);
      }
      else {
	gdk_window_resize (view->output_window,
			   (gint) view->hadjustment->upper,
			   (gint) view->vadjustment->upper);
      }
    }
    else {
      gdk_window_move (view->output_window,
		       - (gint) view->hadjustment->value,
		       - (gint) view->vadjustment->value);
    }

    gdk_window_process_updates (view->output_window, FALSE);

    return TRUE;
  }

  return FALSE;
}


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
