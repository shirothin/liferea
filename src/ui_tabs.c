/**
 * @file ui_tabs.c browser tabs
 *
 * Copyright (C) 2004 Lars Lindner <lars.lindner@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include "support.h"
#include "ui_mainwindow.h"
#include "ui_htmlview.h"
#include "ui_tabs.h"

extern GtkWidget	*mainwindow;
static GHashTable	*tabHash = NULL;

void ui_tabs_init(void) {

	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(lookup_widget(mainwindow, "browsertabs")), FALSE);
	tabHash = g_hash_table_new(g_direct_hash, g_direct_equal);
}

static gboolean on_tab_url_entry_activate(GtkWidget *widget, gpointer user_data) {

	ui_htmlview_launch_URL(GTK_WIDGET(user_data), (gchar *)gtk_entry_get_text(GTK_ENTRY(widget)), FALSE);
}

static gboolean on_tab_close_clicked(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	int i;	
	
	i = gtk_notebook_get_current_page(GTK_NOTEBOOK(lookup_widget(mainwindow, "browsertabs")));
	gtk_notebook_remove_page(GTK_NOTEBOOK(lookup_widget(mainwindow, "browsertabs")), i);
	
	/* check if all tabs are closed */
	if(1 == gtk_notebook_get_n_pages(GTK_NOTEBOOK(lookup_widget(mainwindow, "browsertabs"))))
		gtk_notebook_set_show_tabs(GTK_NOTEBOOK(lookup_widget(mainwindow, "browsertabs")), FALSE);
		
	g_hash_table_remove(tabHash, GINT_TO_POINTER(i));
}

void ui_tabs_new(const gchar *url, const gchar *title) {
	GtkWidget	*widget;
	GtkWidget	*label;
	GtkWidget	*vbox;
	GtkWidget	*toolbar;
	GtkWidget	*htmlframe;
	GtkWidget	*htmlview;
	GtkWidget	*image;
	gchar		*tmp, *tmp2;
	int		i;

	/* make nice short title */
	if(NULL != strstr(title, "http://"))
		title += strlen("http://");
		
	tmp2 = tmp = g_strdup(title);
	if(g_utf8_strlen(tmp, -1) > 20) {
		for(i = 0; i < 20; i++)
			tmp2 = g_utf8_find_next_char(tmp2, NULL);
			*tmp2 = 0;
		tmp2 = g_strdup_printf("%s...", tmp);
		g_free(tmp);
		tmp = tmp2;
	}
	label = gtk_label_new(tmp2);	
	gtk_widget_show(label);
	g_free(tmp);
	
	/* create widgets */
	vbox = gtk_vbox_new(FALSE, 0);
	htmlview = ui_htmlview_new();
	toolbar = gtk_hbox_new(FALSE, 6);

	widget = gtk_label_new(_("URL"));
	gtk_box_pack_start(GTK_BOX(toolbar), widget, FALSE, FALSE, 6);

	widget = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(widget), url);
	gtk_box_pack_start(GTK_BOX(toolbar), widget, TRUE, TRUE, 0);
	g_signal_connect((gpointer)widget, "activate", G_CALLBACK(on_tab_url_entry_activate), (gpointer)htmlview);

	widget = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NONE);
	image = gtk_image_new_from_stock("gtk-close", GTK_ICON_SIZE_BUTTON);
	gtk_widget_show(image);
	gtk_container_add(GTK_CONTAINER(widget), image);
	gtk_box_pack_end(GTK_BOX(toolbar), widget, FALSE, FALSE, 0);
	g_signal_connect((gpointer)widget, "clicked", G_CALLBACK(on_tab_close_clicked), (gpointer)htmlview);
	
	htmlframe = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(htmlframe), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(htmlframe), htmlview);	
	
	gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), htmlframe, TRUE, TRUE, 0);
	gtk_widget_show_all(vbox);
	
	gtk_notebook_append_page(GTK_NOTEBOOK(lookup_widget(mainwindow, "browsertabs")), vbox, label);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(lookup_widget(mainwindow, "browsertabs")), TRUE);

	if(NULL != url)	
		ui_htmlview_launch_URL(htmlview, (gchar *)url, FALSE);
	
	i = gtk_notebook_get_n_pages(GTK_NOTEBOOK(lookup_widget(mainwindow, "browsertabs")));
	g_hash_table_insert(tabHash, GINT_TO_POINTER(i-1), (gpointer)htmlview);
}

void ui_tabs_show_headlines(void) {

	gtk_notebook_set_current_page(GTK_NOTEBOOK(lookup_widget(mainwindow, "browsertabs")), 0);
}

GtkWidget * ui_tabs_get_active_htmlview(void) {
	int current;

	current = gtk_notebook_get_current_page(GTK_NOTEBOOK(lookup_widget(mainwindow, "browsertabs")));
	if(0 == current)
		return ui_mainwindow_get_active_htmlview();
		
	return (GtkWidget *)g_hash_table_lookup(tabHash, GINT_TO_POINTER(current));
}

void on_popup_open_link_in_tab_selected(gpointer url, guint callback_action, GtkWidget *widget) {

	ui_tabs_new((gchar *)url, (gchar *)url);
}
