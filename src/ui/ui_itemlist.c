/**
 * @file ui_itemlist.c Item list/view handling
 *  
 * Copyright (C) 2004-2006 Lars Lindner <lars.lindner@gmx.net>
 * Copyright (C) 2004-2006 Nathan J. Conrad <t98502@users.sourceforge.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 * 
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <langinfo.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include "support.h"
#include "callbacks.h"
#include "common.h"
#include "debug.h"
#include "feed.h"
#include "feedlist.h"
#include "item.h"
#include "itemlist.h"
#include "conf.h"
#include "ui_htmlview.h"
#include "ui_itemlist.h"
#include "ui_mainwindow.h"
#include "ui_tray.h"

extern GdkPixbuf	*icons[];

static GHashTable 	*ilIterHash = NULL;	/* hash table used for fast item->tree iter lookup */
static GtkWidget *itemlist_global_fixme;

gint			itemlist_loading;	/* freaky workaround for item list focussing problem */

// FIXME: remove this
extern itemSetPtr	displayed_itemSet;

#define	TIMESTRLEN	256

static gchar 		*date_format = NULL;	/* date formatting string */

/* sort function for the item list date column */
static gint timeCompFunc(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
	time_t	timea, timeb;
	double diff;
	
	g_assert(model != NULL);
	g_assert(a != NULL);
	g_assert(b != NULL);
	gtk_tree_model_get(model, a, IS_TIME, &timea, -1);
	gtk_tree_model_get(model, b, IS_TIME, &timeb, -1);
	diff = difftime(timeb,timea);
	
	if (diff < 0)
		return 1;
	else if (diff > 0)
		return -1;
	else
		return 0;
}

GtkTreeIter * ui_item_to_iter(itemPtr ip) {

	return (GtkTreeIter *)g_hash_table_lookup(ilIterHash, (gpointer)ip);
}

extern gint disableSortingSaving;

void ui_itemlist_reset_tree_store() {
	GtkTreeModel    *model;

	/* Disable sorting for performance reasons */
	model = GTK_TREE_MODEL(ui_itemlist_get_tree_store());

	ui_itemlist_clear();

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(itemlist_global_fixme));
	gtk_tree_view_set_model(GTK_TREE_VIEW(itemlist_global_fixme), NULL);
	
	g_object_unref(model);
	gtk_tree_view_set_model(GTK_TREE_VIEW(itemlist_global_fixme), GTK_TREE_MODEL(ui_itemlist_get_tree_store()));

	ui_itemlist_prefocus();
}

GtkTreeStore * ui_itemlist_get_tree_store(void) {
	GtkTreeModel	*model;
	GtkTreeStore	*itemstore;
	
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(itemlist_global_fixme));
	if(NULL == model) {
		/* set up a store of these attributes: 
		 	- item creation time
			- item creation time as string
			- item label
			- item state icon
			- unique item id
			- parent node
			- parent node icon
		 */
		itemstore = gtk_tree_store_new(IS_LEN,
		                               G_TYPE_ULONG,
		                               G_TYPE_STRING, 
		                               G_TYPE_STRING,
		                               GDK_TYPE_PIXBUF,
		                               G_TYPE_ULONG,
					       G_TYPE_POINTER,
		                               GDK_TYPE_PIXBUF);
		gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(itemstore), IS_TIME, timeCompFunc, NULL, NULL);
		g_signal_connect(G_OBJECT(itemstore), "sort-column-changed", G_CALLBACK(itemlist_sort_column_changed_cb), NULL);
	} else {
		itemstore = GTK_TREE_STORE(model);
	}
	
	return itemstore;
}

gchar * ui_itemlist_format_date(time_t t) {
	gchar		*tmp;
	gchar		*timestr;

	/* NOTE: This code is partially broken. In the case of a user
	   supplied format string, such a string is in UTF-8. The
	   strftime function expects the user locale as its input, BUT
	   the user's locale may have an alternate representation of '%'
	   (For example UCS16 has 2 byte characters, although this may be
	   handled by glibc correctly) or may not be able to represent a
	   character used in the string. We shall hope that the user's
	   locale has neither of these problems and convert the format
	   string to the user's locale before calling strftime. The
	   result must be converted back to UTF-8 so that it can be
	   displayed by the itemlist correctly. */
	
	if(NULL == date_format) {	
		switch(getNumericConfValue(TIME_FORMAT_MODE)) {
			case 1:
				date_format = g_strdup_printf("%s", nl_langinfo(T_FMT));
				break;
			case 3:
				tmp = getStringConfValue(TIME_FORMAT);
				date_format = g_locale_from_utf8(tmp, -1, NULL, NULL, NULL);
				g_free(tmp);
				break;
			case 2:
			default:
				date_format = g_strdup_printf("%s %s", nl_langinfo(D_FMT), nl_langinfo(T_FMT));
				break;
		}
	}
	
	tmp = g_new0(gchar, TIMESTRLEN+1);
	strftime(tmp, TIMESTRLEN, date_format, localtime(&t));
	timestr = g_locale_to_utf8(tmp, -1, NULL, NULL, NULL);
	g_free(tmp);
	
	return timestr;
}

void ui_itemlist_reset_date_format(void) {

	g_free(date_format);
	date_format = NULL;
}

void ui_itemlist_remove_item(itemPtr ip) {
	GtkTreeIter	*iter;

	g_assert(NULL != ip);
	if(NULL != (iter = ui_item_to_iter(ip))) {
		gtk_tree_store_remove(ui_itemlist_get_tree_store(), iter);
		g_hash_table_remove(ilIterHash, (gpointer)ip);
	} else {
		/*g_warning("item to be removed not found in tree iter lookup hash!");*/
	}
}

/* cleans up the item list, sets up the iter hash when called for the first time */
void ui_itemlist_clear(void) {

	GtkAdjustment		*adj;
	GtkTreeView		*treeview;
	GtkTreeStore		*itemstore = ui_itemlist_get_tree_store();

	treeview = GTK_TREE_VIEW(itemlist_global_fixme);

	/* unselecting all items is important to remove items
	   whose removal is deferred until unselecting */
	gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(treeview));
	
	adj = gtk_tree_view_get_vadjustment(treeview);
	gtk_adjustment_set_value(adj, 0.0);
	gtk_tree_view_set_vadjustment(treeview, adj);

	if(NULL != itemstore)
		gtk_tree_store_clear(itemstore);
	if(NULL != ilIterHash)
		g_hash_table_destroy(ilIterHash);
	
	ilIterHash = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
}

void ui_itemlist_update_item(itemPtr ip) {
	GtkTreeIter	*iter;
	gchar		*title, *label, *time_str, *esc_title, *esc_time_str, *tmp;
	GdkPixbuf	*icon = NULL, *favicon;

	/* favicon for feed icon column (visible in folders/vfolders/searches) */
	if(NULL != ip->sourceNode)
		favicon = ip->sourceNode->icon;
	else
		favicon = ip->itemSet->node->icon;

	/* Time */
	if(0 != ip->time) {
		esc_time_str = ui_itemlist_format_date((time_t)ip->time);
		/* the time value is no markup, so we escape it... */
		tmp = g_markup_escape_text(esc_time_str,-1);
		g_free(esc_time_str);
		esc_time_str = tmp;
	} else {
		esc_time_str = g_strdup("");
	}
	
	/* Label and state icon */
	title = g_strdup(ip->title);
	if(title == NULL) 
		title = g_strdup(_("[No title]"));
		
	/* we escape here to use Pango markup (the parsing ensures that
	   titles never contain escaped HTML) */
	esc_title = g_markup_escape_text(title, -1);
	esc_title = g_strstrip(esc_title);
	
	if(FALSE == ip->readStatus) {
		time_str = g_strdup_printf("<span weight=\"bold\">%s</span>", esc_time_str);
		label = g_strdup_printf("<span weight=\"bold\">%s</span>", esc_title);
		icon = icons[ICON_UNREAD];
	} else if(TRUE == ip->updateStatus) {
		time_str = g_strdup_printf("<span weight=\"bold\" color=\"#333\">%s</span>", esc_time_str);
		label = g_strdup_printf("<span weight=\"bold\" color=\"#333\">%s</span>", esc_title);
		icon = icons[ICON_UPDATED];
	} else {
		time_str = g_strdup(esc_time_str);
		label = g_strdup(esc_title);
		icon = icons[ICON_READ];
	}
	g_free(esc_title);
	g_free(esc_time_str);
	
	if(TRUE == ip->flagStatus) 
		icon = icons[ICON_FLAG];

	/* Finish 'em... */
	if(NULL != (iter = ui_item_to_iter(ip))) {
		gtk_tree_store_set(ui_itemlist_get_tree_store(), iter,
					    IS_LABEL, label,
					    IS_TIME_STR, time_str,
					    IS_ICON, icon,
					    IS_ICON2, favicon,
					    -1);
	}
	
	g_free(time_str);
	g_free(title);
	g_free(label);
}

static void ui_itemlist_update_foreach(gpointer key, gpointer value, gpointer data) {
	
	ui_itemlist_update_item((itemPtr)key);
}

/* update all item list entries */
void ui_itemlist_update(void) {
 
	g_hash_table_foreach(ilIterHash, ui_itemlist_update_foreach, NULL);
}

static gboolean ui_itemlist_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data) {

	if((event->type == GDK_KEY_PRESS) &&
	   (event->state == 0) &&
	   (event->keyval == GDK_Delete)) {
		on_remove_item_activate(NULL, NULL);
	}
	return FALSE;
}

GtkWidget* ui_itemlist_new() {
	GtkCellRenderer		*renderer;
	GtkTreeViewColumn 	*column;
	GtkTreeSelection	*select;
	GtkTreeStore		*itemstore;	
	GtkWidget 			*itemlist;
	GtkWidget 			*ilscrolledwindow;
	
	ilscrolledwindow = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_show (ilscrolledwindow);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (ilscrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (ilscrolledwindow), GTK_SHADOW_IN);

	itemlist_global_fixme = itemlist = gtk_tree_view_new ();
	gtk_container_add (GTK_CONTAINER (ilscrolledwindow), itemlist);
	gtk_widget_show (itemlist);
	gtk_widget_set_name(itemlist, "itemlist");
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (itemlist), TRUE);

	ilIterHash = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);

	itemstore = ui_itemlist_get_tree_store();

	gtk_tree_view_set_model(GTK_TREE_VIEW(itemlist), GTK_TREE_MODEL(itemstore));

	/* we only render the state, title and time */
	renderer = gtk_cell_renderer_pixbuf_new();
	column = gtk_tree_view_column_new_with_attributes("", renderer, "pixbuf", IS_ICON, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(itemlist), column);
	/*gtk_tree_view_column_set_sort_column_id(column, IS_STATE); ...leads to segfaults on tab-bing through 
	 Also might be a bad idea because when an item is clicked, it will immediatly change the sorting order */

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Date"), renderer, "markup", IS_TIME_STR, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(itemlist), column);
	gtk_tree_view_column_set_sort_column_id(column, IS_TIME);
	g_object_set(column, "resizable", TRUE, NULL);
	
	renderer = gtk_cell_renderer_pixbuf_new();
	column = gtk_tree_view_column_new_with_attributes("", renderer, "pixbuf", IS_ICON2, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(itemlist), column);
	
	renderer = gtk_cell_renderer_text_new();	
	column = gtk_tree_view_column_new_with_attributes(_("Headline"), renderer, "markup", IS_LABEL, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(itemlist), column);
	gtk_tree_view_column_set_sort_column_id(column, IS_LABEL);

	g_object_set(column, "resizable", TRUE, NULL);
#if GTK_CHECK_VERSION(2,6,0)
	g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
#endif

	/* And connect signals */	
	g_signal_connect(G_OBJECT(GTK_TREE_VIEW(itemlist)), "key-press-event", G_CALLBACK(ui_itemlist_key_press_cb), NULL);
	
	/* Setup the selection handler */
	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(itemlist));
	gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
	g_signal_connect(G_OBJECT(select), "changed",
                  G_CALLBACK(on_itemlist_selection_changed),
                  NULL);
	g_signal_connect ((gpointer) itemlist, "button_press_event",
					  G_CALLBACK (on_itemlist_button_press_event),
					  NULL);
	g_signal_connect ((gpointer) itemlist, "row_activated",
					  G_CALLBACK (on_Itemlist_row_activated),
					  NULL);
		  
	ui_itemlist_reset_date_format();
	
	return ilscrolledwindow;
}

/* typically called when filling the item tree view */
void ui_itemlist_prefocus(void) {
	GtkWidget		*itemlist, *focus_widget;
	GtkTreeSelection	*itemselection;
	
	/* the following is important to prevent setting the unread
	   flag for the first item in the item list when the user does
	   the first click into the treeview, if we don't do a focus and
	   unselect, GTK would always (exception: clicking on first item)
	   generate two selection-change events (one for the clicked and
	   one for the selected item)!!! */

	itemlist = itemlist_global_fixme;
	
	/* we need to restore the focus after we temporarily select the itemlist */
	focus_widget = gtk_window_get_focus(GTK_WINDOW(mainwindow));

	/* prevent marking as unread before focussing, which leads to a selection */
	gtk_widget_grab_focus(itemlist);

	if(NULL != (itemselection = gtk_tree_view_get_selection(GTK_TREE_VIEW(itemlist))))
		gtk_tree_selection_unselect_all(itemselection);

	gtk_widget_grab_focus(focus_widget);		
}

void ui_itemlist_add_item(itemPtr ip, gboolean merge) {
	GtkTreeStore	*itemstore = ui_itemlist_get_tree_store();
	GtkTreeIter	*iter = NULL;
	
	if((TRUE == merge) && (NULL != (iter = ui_item_to_iter(ip)))) {
		/* g_print("found iter for item %p\n", ip); */
	}
	
	if((NULL != iter) && (FALSE == ip->newStatus)) {
		/* nothing to do */
		/* g_print("nothing to do for iter %p\n", ip); */
	} else {
		if(NULL == iter) {
			iter = g_new0(GtkTreeIter, 1);
			gtk_tree_store_prepend(itemstore, iter, NULL);
			g_hash_table_insert(ilIterHash, (gpointer)ip, (gpointer)iter);
		}	
		gtk_tree_store_set(itemstore, iter,
		                	      IS_NR, ip->nr,
					      IS_PARENT, ip->itemSet->node,
		                	      IS_TIME, item_get_time(ip),
		                	      -1);
		ui_itemlist_update_item(ip);
	}
}

void ui_itemlist_enable_favicon_column(gboolean enabled) {

	/* we depend on the fact that the third column is the favicon column!!! 
	   if we are in search mode (or have a vfolder) we show the favicon 
	   column to give a hint where the item comes from ... */
	gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(itemlist_global_fixme), 2), enabled);
}

void on_popup_launchitem_selected(void) {
	itemPtr		ip;

	if(NULL != (ip = itemlist_get_selected()))
		ui_htmlview_launch_URL(ui_tabs_get_active_htmlview(), (gchar *)item_get_source(ip), UI_HTMLVIEW_LAUNCH_DEFAULT);
	else
		ui_mainwindow_set_status_bar(_("No item has been selected"));
}

void on_popup_launchitem_in_tab_selected(void) {
	itemPtr		ip;
	const gchar	*link;

	if(NULL != (ip = itemlist_get_selected())) {
		if(NULL != (link = item_get_source(ip)))
			ui_tabs_new(link, link, FALSE);
		else
			ui_show_error_box(_("This item has no link specified!"));
	} else {
		ui_mainwindow_set_status_bar(_("No item has been selected"));
	}
}

void on_Itemlist_row_activated(GtkTreeView *treeview, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {

	on_popup_launchitem_selected();
}

/* menu callbacks */					
void on_toggle_item_flag(GtkMenuItem *menuitem, gpointer user_data) {
	itemPtr		ip;
	
	if(NULL != (ip = itemlist_get_selected()))
		itemlist_toggle_flag(ip);
}

void on_popup_toggle_flag(gpointer callback_data, guint callback_action, GtkWidget *widget) { on_toggle_item_flag(NULL, NULL); }

void on_toggle_unread_status(GtkMenuItem *menuitem, gpointer user_data) {
	itemPtr		ip;

	if(NULL != (ip = itemlist_get_selected())) 
		itemlist_toggle_read_status(ip);
}

void on_popup_toggle_read(gpointer callback_data, guint callback_action, GtkWidget *widget) { on_toggle_unread_status(NULL, NULL); }

void on_remove_items_activate(GtkMenuItem *menuitem, gpointer user_data) {
	nodePtr		np;
	
	np = feedlist_get_selected();
	if((NULL != np) && (FST_FEED == np->type))
		itemlist_remove_items(np->itemSet);
	else
		ui_show_error_box(_("You must select a feed to delete its items!"));
}

void on_remove_item_activate(GtkMenuItem *menuitem, gpointer user_data) {
	GtkTreeSelection	*selection;
	itemPtr			ip;
	
	if(NULL != (ip = itemlist_get_selected())) {
		/* 1. order is important, first remove then unselect! */
		itemlist_remove_item(ip);

		/* 2. deferred removal forces us to unselect the item 
		      One way to do it is to move forward the cursor. */
		on_treeview_move("Itemlist", 1);

		/* 3. But 2. might not be enough, e.g. if ip was the
		      last item and is still selected (not removed) */
		if(itemlist_get_selected() == ip) {
			selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(itemlist_global_fixme));
			gtk_tree_selection_unselect_all(selection);
		}
	} else {
		ui_mainwindow_set_status_bar(_("No item has been selected"));
	}
}

void on_popup_remove_selected(gpointer callback_data, guint callback_action, GtkWidget *widget) { on_remove_item_activate(NULL, NULL); }

void ui_itemlist_select(itemPtr ip) {
	GtkTreeStore		*itemstore = ui_itemlist_get_tree_store();
	GtkWidget		*treeview;
	GtkTreeSelection	*selection;
	GtkTreeIter		*iter;
	GtkTreePath		*path;

	g_assert(NULL != ip);

	iter = ui_item_to_iter(ip);
	g_return_if_fail(NULL != iter);

	treeview = itemlist_global_fixme;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));

	path = gtk_tree_model_get_path(GTK_TREE_MODEL(itemstore), iter);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(treeview), path, NULL, FALSE, 0.0, 0.0);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(treeview), path, NULL, FALSE);
	gtk_tree_path_free(path);
}

void on_next_unread_item_activate(GtkMenuItem *menuitem, gpointer user_data) {

	itemlist_select_next_unread();
}

void on_popup_next_unread_item_selected(gpointer callback_data, guint callback_action, GtkWidget *widget) { 

	itemlist_select_next_unread();
}

void on_nextbtn_clicked(GtkButton *button, gpointer user_data) {

	itemlist_select_next_unread();
}

// FIXME: get rid of this method
static itemPtr ui_itemlist_get_item_from_iter(GtkTreeIter *iter) {
	itemPtr		ip;
	nodePtr		np;
	gulong		nr;
	
	gtk_tree_model_get(GTK_TREE_MODEL(ui_itemlist_get_tree_store()), 
	                   iter, 
			   IS_NR, &nr, 
			   IS_PARENT, &np, 
			   -1);
	ip = itemset_lookup_item(displayed_itemSet, np, nr);
	g_assert(ip != NULL);
	return ip;
}

gboolean on_itemlist_button_press_event(GtkWidget *treeview, GdkEventButton *event, gpointer user_data) {
	GdkEventButton		*eb;
	GtkTreeViewColumn	*column;
	GtkTreePath		*path;
	GtkTreeIter		iter;
	itemPtr			ip = NULL;
	
	if(event->type != GDK_BUTTON_PRESS)
		return FALSE;

	if(!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview), event->x, event->y, &path, NULL, NULL, NULL))
		return FALSE;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(ui_itemlist_get_tree_store()), &iter, path);
	gtk_tree_path_free(path);
	
	ip = ui_itemlist_get_item_from_iter(&iter);

	eb = (GdkEventButton*)event; 
	switch(eb->button) {
		case 2:
			/* depending on the column middle mouse click toggles flag or read status
			   We depent on the fact that the state column is the first!!! 
			   code inspired by the GTK+ 2.0 Tree View Tutorial at:
			   http://scentric.net/tutorial/sec-misc-get-renderer-from-click.html */
			if(NULL != (column = gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), 0))) {
				g_assert(NULL != column);
				if(event->x <= column->width)
					itemlist_toggle_flag(ip);
				else
					itemlist_toggle_read_status(ip);
			}
			return TRUE;
			break;
		case 3:
			ui_itemlist_select(ip);
			gtk_menu_popup(make_item_menu(ip), NULL, NULL, NULL, NULL, eb->button, eb->time);
			return TRUE;
			break;
	}	
	return FALSE;
}

void on_popup_copy_URL_clipboard(void) {
	itemPtr         ip;

	if(NULL != (ip = itemlist_get_selected()))
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), item_get_source(ip), -1);
	else
		ui_mainwindow_set_status_bar(_("No item has been selected"));
}

void on_itemlist_selection_changed(GtkTreeSelection *selection, gpointer data) {
	GtkTreeIter 	iter;
	GtkTreeModel	*model;
	itemPtr		ip = NULL;

	if(gtk_tree_selection_get_selected(selection, &model, &iter))
		ip = ui_itemlist_get_item_from_iter(&iter);

	itemlist_selection_changed(ip);
}

void itemlist_sort_column_changed_cb(GtkTreeSortable *treesortable, gpointer user_data) {
	gint		sortColumn;
	GtkSortType	sortType;
	gboolean	sorted;
	
	if(feedlist_get_selected() == NULL || disableSortingSaving != 0)
		return;
	
	sorted = gtk_tree_sortable_get_sort_column_id(treesortable, &sortColumn, &sortType);
	node_set_sort_column(feedlist_get_selected(), sortColumn, sortType == GTK_SORT_DESCENDING);
}

/* needed because switching does sometimes returns to the tree 
   view with a very disturbing horizontal scrolling state */
void ui_itemlist_scroll_left() {
	GtkTreeViewColumn 	*column;
	GtkTreePath		*path;

	if (FALSE == itemlist_get_two_pane_mode()) {
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(itemlist_global_fixme), &path, &column);
		if(path) {
			column = gtk_tree_view_get_column(GTK_TREE_VIEW(itemlist_global_fixme), 1);
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(itemlist_global_fixme), path, column, FALSE);
			gtk_tree_path_free(path);
		}
	}
}
