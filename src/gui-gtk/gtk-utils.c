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


#include "gtk-utils.h"
#include "quarry-stock.h"
#include "utils.h"

#include <assert.h>


static void	set_widget_sensitivity_on_toggle
		  (GtkToggleButton *toggle_button, GtkWidget *widget);
static void	set_widget_sensitivity_on_input(GtkEntry *entry,
						GtkWidget *widget);


void
gtk_utils_add_similar_bindings(GtkBindingSet *binding_set,
			       const gchar *signal_name,
			       GtkUtilsBindingInfo *bindings,
			       int num_bindings)
{
  int k;

  assert(binding_set);
  assert(signal_name);
  assert(bindings);
  assert(num_bindings > 0);

  for (k = 0; k < num_bindings; k++) {
    gtk_binding_entry_add_signal(binding_set,
				 bindings[k].keyval, bindings[k].modifiers,
				 signal_name,
				 1, G_TYPE_INT, bindings[k].signal_parameter);
  }
}


void
gtk_utils_make_window_only_horizontally_resizable(GtkWindow *window)
{
  GdkGeometry geometry;

  assert(GTK_IS_WINDOW(window));

  geometry.max_width  = G_MAXINT;
  geometry.max_height = -1;
  gtk_window_set_geometry_hints(window, NULL, &geometry, GDK_HINT_MAX_SIZE);
}


void
gtk_utils_standardize_dialog(GtkDialog *dialog, GtkWidget *contents)
{
  assert(GTK_IS_DIALOG(dialog));
  assert(GTK_IS_CONTAINER(contents));

  gtk_widget_set_name(GTK_WIDGET(dialog), "quarry-dialog");

  gtk_box_set_spacing(GTK_BOX(dialog->vbox), QUARRY_SPACING);
  gtk_box_pack_start_defaults(GTK_BOX(dialog->vbox), contents);
}


GtkWidget *
gtk_utils_create_message_dialog(GtkWindow *parent, const gchar *icon_stock_id,
				GtkUtilsMessageDialogFlags flags,
				GCallback response_callback,
				const gchar *hint,
				const gchar *message_format_string, ...)
{
  GtkWidget *widget = gtk_dialog_new();
  GtkDialog *dialog = GTK_DIALOG(widget);
  GtkWindow *window = GTK_WINDOW(dialog);
  GtkUtilsMessageDialogFlags buttons = flags & GTK_UTILS_BUTTONS_MASK;
  GtkWidget *icon;
  GtkWidget *label;
  GtkWidget *hbox;
  gchar *message_text;
  gchar *label_text;
  va_list arguments;

  assert(icon_stock_id);

  gtk_window_set_title(window, "");
  gtk_window_set_resizable(window, FALSE);
  gtk_window_set_skip_pager_hint(window, TRUE);
  gtk_window_set_skip_taskbar_hint(window, TRUE);

  if (parent) {
    gtk_window_set_transient_for(window, GTK_WINDOW(parent));

    if (!(flags & GTK_UTILS_NON_MODAL_WINDOW))
      gtk_window_set_modal(window, TRUE);
  }

  gtk_dialog_set_has_separator(dialog, FALSE);

  if (buttons == GTK_UTILS_BUTTONS_OK_CANCEL)
    gtk_dialog_add_button(dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

  if (buttons == GTK_UTILS_BUTTONS_OK
      || buttons == GTK_UTILS_BUTTONS_OK_CANCEL) {
    gtk_dialog_add_button(dialog, GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);
  }

  if (buttons == GTK_UTILS_BUTTONS_CLOSE) {
    gtk_dialog_add_button(dialog, GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_CLOSE);
  }

  icon = gtk_image_new_from_stock(icon_stock_id, GTK_ICON_SIZE_DIALOG);
  gtk_misc_set_alignment(GTK_MISC(icon), 0.5, 0.0);

  va_start(arguments, message_format_string);
  message_text = g_strdup_vprintf(message_format_string, arguments);
  va_end(arguments);

  if (hint) {
    label_text = g_strdup_printf(("<span weight=\"bold\" size=\"larger\">%s"
				  "</span>\n\n%s"),
				 message_text, hint);
  }
  else {
    label_text = g_strdup_printf(("<span weight=\"bold\" size=\"larger\">%s"
				  "</span>"),
				 message_text);
  }

  label = gtk_label_new(NULL);
  gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  gtk_label_set_markup(GTK_LABEL(label), label_text);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);

  g_free(message_text);
  g_free(label_text);

  hbox = gtk_utils_pack_in_box(GTK_TYPE_HBOX, QUARRY_SPACING,
			       icon, GTK_UTILS_FILL,
			       label, GTK_UTILS_PACK_DEFAULT, NULL);
  gtk_widget_show_all(hbox);

  gtk_utils_standardize_dialog(dialog, hbox);
  gtk_box_set_spacing(GTK_BOX(dialog->vbox), QUARRY_SPACING_VERY_BIG);

  if (response_callback)
    g_signal_connect(dialog, "response", response_callback, NULL);

  if (!(flags & GTK_UTILS_DONT_SHOW))
    gtk_window_present(window);

  return widget;
}


GtkWidget *
gtk_utils_create_titled_page(GtkWidget *contents,
			     const gchar *icon_stock_id, const gchar *title)
{
  GtkWidget *page;
  GtkBox *page_box;
  GtkWidget *image = NULL;
  GtkWidget *label = NULL;
  GtkWidget *title_widget;

  assert(GTK_IS_WIDGET(contents));
  assert(icon_stock_id || title);

  page = gtk_vbox_new(FALSE, QUARRY_SPACING_SMALL);
  page_box = GTK_BOX(page);

  if (icon_stock_id) {
    image = gtk_image_new_from_stock(icon_stock_id,
				     GTK_ICON_SIZE_LARGE_TOOLBAR);
  }

  if (title) {
    char *marked_up_title
      = utils_cat_strings(NULL,
			  "<span weight=\"bold\" size=\"x-large\">",
			  title, "</span>", NULL);

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), marked_up_title);

    utils_free(marked_up_title);
  }

  if (image && label) {
    title_widget = gtk_hbox_new(FALSE, QUARRY_SPACING_SMALL);

    gtk_box_pack_start(GTK_BOX(title_widget), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(title_widget), label, FALSE, FALSE, 0);
  }
  else if (label) {
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    title_widget = label;
  }
  else
    title_widget = image;

  gtk_box_pack_start(page_box, title_widget, FALSE, TRUE, 0);
  gtk_box_pack_start(page_box, gtk_hseparator_new(), FALSE, TRUE, 0);
  gtk_box_pack_start_defaults(page_box, contents);

  gtk_widget_show_all(page);
  return page;
}


GtkWidget *
gtk_utils_pack_in_box(GType box_type, gint spacing, ...)
{
  GtkBox *box = GTK_BOX(g_object_new(box_type, NULL));
  GtkWidget *widget;
  va_list arguments;

  gtk_box_set_spacing(box, spacing);

  va_start(arguments, spacing);
  while ((widget = va_arg(arguments, GtkWidget *)) != NULL) {
    guint packing_parameters = va_arg(arguments, guint);

    assert(GTK_IS_WIDGET(widget));

    gtk_box_pack_start(box, widget,
		       packing_parameters & GTK_UTILS_EXPAND ? TRUE : FALSE,
		       packing_parameters & GTK_UTILS_FILL ? TRUE : FALSE,
		       packing_parameters & GTK_UTILS_PACK_PADDING_MASK);
  }

  return GTK_WIDGET(box);
}


GtkWidget *
gtk_utils_align_widget(GtkWidget *widget,
		       gfloat x_alignment, gfloat y_alignment)
{
  GtkWidget *alignment = gtk_alignment_new(x_alignment, y_alignment,
					   0.0, 0.0);

  assert(GTK_IS_WIDGET(widget));

  gtk_container_add(GTK_CONTAINER(alignment), widget);

  return alignment;
}


GtkWidget *
gtk_utils_sink_widget(GtkWidget *widget)
{
  GtkWidget *frame = gtk_frame_new(NULL);

  assert(GTK_IS_WIDGET(widget));

  gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
  gtk_container_add(GTK_CONTAINER(frame), widget);

  return frame;
}


GtkWidget *
gtk_utils_make_widget_scrollable(GtkWidget *widget,
				 GtkPolicyType hscrollbar_policy,
				 GtkPolicyType vscrollbar_policy)
{
  GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);

  assert(GTK_IS_WIDGET(widget));

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				 hscrollbar_policy, vscrollbar_policy);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window),
				      GTK_SHADOW_IN);
  gtk_container_add(GTK_CONTAINER(scrolled_window), widget);

  gtk_widget_show_all(scrolled_window);
  return scrolled_window;
}


GtkWidget *
gtk_utils_create_entry(const gchar *text)
{
  GtkWidget *entry = gtk_entry_new();

  if (text)
    gtk_entry_set_text(GTK_ENTRY(entry), text);

  gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

  return entry;
}


void
gtk_utils_create_radio_chain(GtkWidget **radio_buttons,
			     const gchar **label_texts,
			     gint num_radio_buttons)
{
  GtkRadioButton *last_button = NULL;
  int k;

  assert(radio_buttons);
  assert(label_texts);

  for (k = 0; k < num_radio_buttons; k++) {
    radio_buttons[k]
      = gtk_radio_button_new_with_mnemonic_from_widget(last_button,
						       label_texts[k]);
    last_button = GTK_RADIO_BUTTON(radio_buttons[k]);
  }
}


GtkSizeGroup *
gtk_utils_create_size_group(GtkSizeGroupMode mode, ...)
{
  GtkSizeGroup *size_group = gtk_size_group_new(mode);
  GtkWidget *widget;
  va_list arguments;

  va_start(arguments, mode);
  while ((widget = va_arg(arguments, GtkWidget *)) != NULL) {
    assert(GTK_IS_WIDGET(widget));
    gtk_size_group_add_widget(size_group, widget);
  }

  va_end(arguments);

  g_object_unref(size_group);
  return size_group;
}


void
gtk_utils_set_sensitive_on_toggle(GtkToggleButton *toggle_button,
				  GtkWidget *widget)
{
  assert(GTK_IS_TOGGLE_BUTTON(toggle_button));
  assert(GTK_IS_WIDGET(widget));

  set_widget_sensitivity_on_toggle(toggle_button, widget);
  g_signal_connect(toggle_button, "toggled",
		   G_CALLBACK(set_widget_sensitivity_on_toggle), widget);
}


static void
set_widget_sensitivity_on_toggle(GtkToggleButton *toggle_button,
				 GtkWidget *widget)
{
  gtk_widget_set_sensitive(widget,
			   gtk_toggle_button_get_active(toggle_button));
}


void
gtk_utils_set_sensitive_on_input(GtkEntry *entry, GtkWidget *widget)
{
  assert(GTK_IS_ENTRY(entry));
  assert(GTK_IS_WIDGET(widget));

  set_widget_sensitivity_on_input(entry, widget);
  g_signal_connect(entry, "changed",
		   G_CALLBACK(set_widget_sensitivity_on_input), widget);
}


static void
set_widget_sensitivity_on_input(GtkEntry *entry, GtkWidget *widget)
{
  const gchar *text = gtk_entry_get_text(entry);

  gtk_widget_set_sensitive(widget, *text ? TRUE : FALSE);
}


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
