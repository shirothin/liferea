/*
   GUI feed list handling
   
   Copyright (C) 2004 Lars Lindner <lars.lindner@gmx.net>
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
*/

#include <gtk/gtk.h>
#include "support.h"
#include "interface.h"
#include "callbacks.h"
#include "feed.h"
#include "folder.h"
#include "conf.h"
#include "ui_feedlist.h"
#include "ui_tray.h"
#include "htmlview.h"
#include "favicon.h"

/* possible selected new dialog feed types */
static gint selectableTypes[] = {	FST_AUTODETECT,
					FST_RSS,
					FST_CDF,
					FST_PIE,
					FST_OCS,
					FST_OPML
				};


enum { LIFEREA_FEED_ROW};

static GtkTargetEntry lte[] = {{"LIFEREA_FEED_ROW", GTK_TARGET_SAME_WIDGET, LIFEREA_FEED_ROW}};
#define MAX_TYPE_SELECT	6

extern GtkWidget	*mainwindow;

GtkTreeStore	*feedstore = NULL;

GtkWidget		*filedialog = NULL;
static GtkWidget	*newdialog = NULL;
static GtkWidget	*propdialog = NULL;

folderPtr ui_feedlist_get_parent(nodePtr ptr) {
	GtkTreeIter *iter = &((ui_data*)(ptr->ui_data))->row;
	GtkTreeIter parent;
	folderPtr parentPtr;
	
	if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(feedstore), &parent, iter)) {
		gtk_tree_model_get(GTK_TREE_MODEL(feedstore), &parent,
					    FS_PTR, &parentPtr,
					    -1);
		return parentPtr;
	}
	
	return NULL;
}



nodePtr ui_feedlist_get_selected() {

	GtkWidget		*treeview;
	GtkTreeSelection	*select;
	GtkTreeModel		*model = GTK_TREE_MODEL(feedstore);
	GtkTreeIter	iter;
	nodePtr		ptr;

	g_assert(mainwindow);
	treeview = lookup_widget(mainwindow, "feedlist");
	g_assert(treeview);
	
	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
	g_assert(select);
	
	if(gtk_tree_selection_get_selected(select, &model, &iter)) {
		gtk_tree_model_get(model, &iter, 
					    FS_PTR, &ptr, 
					    -1);
		return ptr;
	} else
		return NULL;
}

/* Selects the proper destination for a new item based on which item
 * is curretnly selected.
 * @returns folder into which the item should be inserted
 */
folderPtr ui_feedlist_get_target_folder() {
	nodePtr ptr = ui_feedlist_get_selected();
	
	if (ptr == NULL) {
		return NULL;
	} if(IS_FOLDER(ptr->type)) {
		return (folderPtr)ptr;
	} else
		return ui_feedlist_get_parent(ptr);
}


static GdkPixbuf* ui_feed_select_icon(feedPtr fp) {
	gpointer	favicon;
	g_assert(!IS_FOLDER(fp->type));
	
	switch(fp->type) {
		case FST_EMPTY:
			return icons[ICON_EMPTY];
		case FST_VFOLDER:
			return icons[ICON_VFOLDER];
		case FST_OPML:
		case FST_OCS:
			if(feed_get_available(fp))
				return icons[ICON_OCS];
			else
				return icons[ICON_UNAVAILABLE];
		case FST_AUTODETECT:
		case FST_HELPFEED:
		case FST_PIE:
		case FST_RSS:			
		case FST_CDF:
			if(feed_get_available(fp)) {
				if(NULL != (favicon = feed_get_favicon(fp))) {
					return favicon;
				} else
					return icons[ICON_AVAILABLE];
			} else
				return icons[ICON_UNAVAILABLE];
		default:
			g_print(_("internal error! unknown entry type! cannot display appropriate icon!\n"));
			return icons[ICON_UNAVAILABLE];
	}	
}

void ui_update_feed(feedPtr fp) {
	GtkTreeModel      *model;
	GtkTreeIter       *iter;
	gchar     *label, *tmp;
	int		count;
	
	iter = &((ui_data*)fp->ui_data)->row;
	model =  GTK_TREE_MODEL(feedstore);
	
	g_assert(!IS_FOLDER(fp->type));
	g_assert(fp->type != FST_EMPTY);
	
	count = feed_get_unread_counter(fp);
	label = unhtmlize(g_strdup(feed_get_title(fp)));
	/* FIXME: Unescape text here! */
	tmp = g_markup_escape_text(label,-1);
	g_free(label);
	if(count > 0) {
		label = g_strdup_printf("<span weight=\"bold\">%s (%d)</span>", tmp, count);
	} else {
		label = g_strdup_printf("%s", tmp);
	}
	g_free(tmp);
	
	if(NULL != fp->parseErrors) {
		tmp = g_strdup_printf("<span foreground=\"red\">%s</span>", label);
		g_free(label);
		label = tmp;
	}

	gtk_tree_store_set(feedstore, iter,
				    FS_LABEL, label,
				    FS_UNREAD, count,
				    FS_ICON, ui_feed_select_icon(fp),
				    -1);

	
	ui_update_folder((folderPtr)ui_feedlist_get_parent((nodePtr)fp));

	g_free(label);
}

static void ui_feedlist_selection_changed_cb(GtkTreeSelection *selection, gpointer data) {
	GtkTreeIter		iter;
	GtkTreeModel		*model;
	feedPtr			fp;
	GdkGeometry		geometry;
	
	undoTrayIcon();
	
	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		gtk_tree_model_get(model, &iter, 
					    FS_PTR, &fp,
					    -1);
		
		/* make sure thats no grouping iterator */
		if(fp && (IS_FEED(fp->type) || IS_DIRECTORY(fp->type) || FST_VFOLDER == fp->type)) {
			
			/* FIXME: another workaround to prevent strange window
			   size increasings after feed selection changing */
			geometry.min_height=480;
			geometry.min_width=640;
			g_assert(mainwindow != NULL);
			gtk_window_set_geometry_hints(GTK_WINDOW(mainwindow), mainwindow, &geometry, GDK_HINT_MIN_SIZE);
			
			/* Set up the item list */
			ui_itemlist_load(fp, NULL);
		} else { /* Selecting a folder */
			ui_itemlist_clear();
		}
	} else {
		/* If we cannot get the new selection we keep the old one
		   this happens when we're doing drag&drop for example. */
	}
}

/* sets up the entry list store and connects it to the entry list
   view in the main window */
void ui_feedlist_init(GtkWidget *mainview) {
	GtkCellRenderer		*textRenderer;
	GtkCellRenderer		*iconRenderer;	
	GtkTreeViewColumn 	*column;
	GtkTreeSelection	*select;	
	
	g_assert(mainwindow != NULL);
	
	/* Set up store */
	feedstore = gtk_tree_store_new(FS_LEN,
	                               G_TYPE_STRING,
	                               GDK_TYPE_PIXBUF,
	                               G_TYPE_POINTER,
	                               G_TYPE_INT);
	gtk_tree_view_set_model(GTK_TREE_VIEW(mainview), GTK_TREE_MODEL(feedstore));

	/* we only render the state and title */
	iconRenderer = gtk_cell_renderer_pixbuf_new();
	textRenderer = gtk_cell_renderer_text_new();

	column = gtk_tree_view_column_new();
	
	gtk_tree_view_column_pack_start(column, iconRenderer, FALSE);
	gtk_tree_view_column_pack_start(column, textRenderer, TRUE);
	
	gtk_tree_view_column_add_attribute(column, iconRenderer, "pixbuf", FS_ICON);
	gtk_tree_view_column_add_attribute(column, textRenderer, "markup", FS_LABEL);
	
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(mainview), column);

	/* Setup the selection handler for the main view */
	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(mainview));
	gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
	
	g_signal_connect(G_OBJECT(select), "changed",
                 	 G_CALLBACK(ui_feedlist_selection_changed_cb),
                	 lookup_widget(mainwindow, "feedlist"));
			 
	ui_dnd_init();			
}

void ui_feedlist_select(nodePtr np) {
	static gboolean firstCall = TRUE;
	GtkTreeIter iter = ((ui_data*)(np->ui_data))->row;
	GtkWidget		*treeview;
	GtkTreeSelection	*selection;
	GtkTreePath		*path;

	/* To work around a GTK+ bug. The first time this is called, it
	   would always select the first feed in the list */
	if (firstCall) {
		firstCall = FALSE;
		ui_feedlist_select(np);
	}

	/* some comfort: select the created iter */
	if(NULL != (treeview = lookup_widget(mainwindow, "feedlist"))) {
		if(NULL != (selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)))) {
			path = gtk_tree_model_get_path(GTK_TREE_MODEL(feedstore), &iter);
			gtk_tree_view_expand_to_path(GTK_TREE_VIEW(treeview), path);
			gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(treeview), path, NULL, FALSE, 0.0, 0.0);	
			gtk_tree_selection_select_path(selection, path);
			gtk_tree_path_free(path);
		} else
			g_warning(_("internal error! could not get feed tree view selection!\n"));
	} else {
			g_warning("internal error! could not select newly created treestore iter!");
	}
}

void on_popup_refresh_selected(gpointer callback_data,
						 guint callback_action,
						 GtkWidget *widget) {
	feedPtr fp = (feedPtr)callback_data;
	g_assert(fp);
	g_assert(FEED_MENU(fp->type));

	feed_update(fp);
}

/*------------------------------------------------------------------------------*/
/* delete entry callbacks 							*/
/*------------------------------------------------------------------------------*/

void on_popup_delete_selected(gpointer callback_data,
                                             guint callback_action,
                                             GtkWidget *widget) {
	feedPtr fp = (feedPtr)callback_data;

	g_assert(fp);
	g_assert(FEED_MENU(fp->type));
	
	/* block deleting of empty entries */

	/* block deleting of help feeds */
	if(fp->type == FST_HELPFEED || fp->type == FST_HELPFOLDER) {
		ui_show_error_box(_("You can't delete the help! Edit the preferences to disable loading the help."));
		return;
	}
	
	ui_mainwindow_set_status_bar("%s \"%s\"",_("Deleting entry"), feed_get_title(fp));
	g_assert((nodePtr)fp == ui_feedlist_get_selected());

	ui_itemlist_clear();
	ui_htmlview_clear();
	feed_free(fp);
}

/*------------------------------------------------------------------------------*/
/* property dialog callbacks 							*/
/*------------------------------------------------------------------------------*/

GtkWidget *ui_feedlist_build_prop_dialog(void) {

	if(NULL == propdialog || !G_IS_OBJECT(propdialog))
		propdialog = create_propdialog();

	return propdialog;
}

void on_popup_prop_selected(gpointer callback_data,
                                             guint callback_action,
                                             GtkWidget *widget) {
	GtkWidget 	*feednameentry, *feedurlentry, *updateIntervalBtn;
	GtkAdjustment	*updateInterval;
	gint		defaultInterval;
	gchar		*defaultIntervalStr;
     feedPtr fp = (feedPtr)callback_data;
	
	if(!fp || !FEED_MENU(feed_get_type(fp))) {
		g_message(_("You have to select a feed entry!"));
		ui_show_error_box(_("You have to select a feed entry!"));
		return;
	}
	
	if(fp->type == FST_HELPFEED) {
		ui_show_error_box("You can't modify help feeds!");
		return;
	}
	
	/* prop dialog may not yet exist */
	ui_feedlist_build_prop_dialog();
		
	feednameentry = lookup_widget(propdialog, "feednameentry");
	feedurlentry = lookup_widget(propdialog, "feedurlentry");
	updateIntervalBtn = lookup_widget(propdialog, "feedrefreshcount");
	updateInterval = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(updateIntervalBtn));

	g_object_set_data(G_OBJECT(propdialog), "fp", fp);

	gtk_entry_set_text(GTK_ENTRY(feednameentry), feed_get_title(fp));
	gtk_entry_set_text(GTK_ENTRY(feedurlentry), feed_get_source(fp));

	if(IS_DIRECTORY(feed_get_type(fp))) {	
		/* disable the update interval selector for directories (should this be the case for OPML?) */
		gtk_widget_set_sensitive(lookup_widget(propdialog, "feedrefreshcount"), FALSE);
	} else {
		/* enable and adjust values otherwise */
		gtk_widget_set_sensitive(lookup_widget(propdialog, "feedrefreshcount"), TRUE);	
		
		gtk_adjustment_set_value(updateInterval, feed_get_update_interval(fp));

		defaultInterval = feed_get_default_update_interval(fp);
		if(-1 != defaultInterval)
			defaultIntervalStr = g_strdup_printf(_("The provider of this feed suggests an update interval of %d minutes"), defaultInterval);
		else
			defaultIntervalStr = g_strdup(_("This feed specifies no default update interval."));
		gtk_label_set_text(GTK_LABEL(lookup_widget(propdialog, "feedupdateinfo")), defaultIntervalStr);
		g_free(defaultIntervalStr);		
	}

	gtk_widget_show(propdialog);
}

void on_propchangebtn_clicked(GtkButton *button, gpointer user_data) {
	gchar		*feedurl, *feedname;
	GtkWidget 	*feedurlentry;
	GtkWidget 	*feednameentry;
	GtkWidget 	*updateIntervalBtn;
	GtkAdjustment	*updateInterval;
	gint			interval;
	feedPtr		fp = g_object_get_data(G_OBJECT(propdialog), "fp");
	
	g_assert(NULL != propdialog);
		
	if(NULL != fp) {
		feednameentry = lookup_widget(propdialog, "feednameentry");
		feedurlentry = lookup_widget(propdialog, "feedurlentry");

		feedurl = (gchar *)gtk_entry_get_text(GTK_ENTRY(feedurlentry));
		feedname = (gchar *)gtk_entry_get_text(GTK_ENTRY(feednameentry));
	
		feed_set_title(fp, feedname);  

		/* if URL has changed... */
		if(strcmp(feedurl, feed_get_source(fp))) {
			feed_set_source(fp, feedurl);
			feed_update(fp);
		}
		
		if(IS_FEED(feed_get_type(fp))) {
			updateIntervalBtn = lookup_widget(propdialog, "feedrefreshcount");
			updateInterval = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(updateIntervalBtn));

			interval = gtk_adjustment_get_value(updateInterval);
			
			if(0 == interval) 
				interval = -1;	/* this is due to ignore this feed while updating */
			feed_set_update_interval(fp, interval);
		}
		ui_update_feed(fp);
	} else {
		g_warning(_("Internal error! No feed selected, but property change requested...\n"));
	}
}

/*------------------------------------------------------------------------------*/
/* new entry dialog callbacks 							*/
/*------------------------------------------------------------------------------*/

void ui_feedlist_new_subscription(gint type, gchar *source, gboolean showPropDialog) {
	feedPtr		fp;

	fp = feed_new();
	fp->displayProps = showPropDialog; 	// FIXME!
	feed_set_id(fp, conf_new_id());
	feed_set_title(fp, g_strdup("New feed...."));
	feed_set_type(fp, type);
	feed_set_source(fp, source);
	favicon_download(fp);
	feed_update(fp);
	
	ui_folder_add_feed(ui_feedlist_get_target_folder(), fp, -1);
}

void on_newbtn_clicked(GtkButton *button, gpointer user_data) {	
	GtkWidget 	*sourceentry;	
	
	if(NULL == newdialog || !G_IS_OBJECT(newdialog)) 
		newdialog = create_newdialog();
		
	if(NULL == propdialog || !G_IS_OBJECT(propdialog))
		propdialog = create_propdialog();

	sourceentry = lookup_widget(newdialog, "newfeedentry");
	gtk_entry_set_text(GTK_ENTRY(sourceentry), "");

	g_assert(NULL != newdialog);
	g_assert(NULL != propdialog);
	gtk_widget_show(newdialog);
}

void on_newfeedbtn_clicked(GtkButton *button, gpointer user_data) {
	gchar		*source;
	GtkWidget 	*sourceentry;	
	GtkWidget 	*titleentry, *typeoptionmenu;
	gint		type;
	
	g_assert(newdialog != NULL);
	g_assert(propdialog != NULL);

	sourceentry = lookup_widget(newdialog, "newfeedentry");
	titleentry = lookup_widget(propdialog, "feednameentry");
	typeoptionmenu = lookup_widget(newdialog, "typeoptionmenu");
		
	source = g_strdup(gtk_entry_get_text(GTK_ENTRY(sourceentry)));
	type = gtk_option_menu_get_history(GTK_OPTION_MENU(typeoptionmenu));
	
	/* the retrieved number is not yet the real feed type! */
	if(type > MAX_TYPE_SELECT) {
		g_error(_("internal error! invalid type selected! This should never happen!\n"));
		return;
	} else
		type = selectableTypes[type];

	ui_feedlist_new_subscription(type, source, TRUE);
	/* don't free source for it is reused by newFeed! */
}

void on_localfileselect_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget	*source;
	
	gtk_widget_hide(filedialog);
	g_assert(NULL != newdialog);
	if(NULL != (source = lookup_widget(newdialog, "newfeedentry")))
		gtk_entry_set_text(GTK_ENTRY(source), gtk_file_selection_get_filename(GTK_FILE_SELECTION(filedialog)));
}

void on_localfilebtn_pressed(GtkButton *button, gpointer user_data) {
	GtkWidget	*okbutton;
	
	if(NULL == filedialog || !G_IS_OBJECT(filedialog))
		filedialog = create_fileselection();
		
	if(NULL == (okbutton = lookup_widget(filedialog, "fileselectbtn")))
		g_warning("internal error! could not find file dialog select button!");

	g_signal_connect((gpointer) okbutton, "clicked", G_CALLBACK (on_localfileselect_clicked), NULL);
	gtk_widget_show(filedialog);
}

/* recursivly calls func for every feed in the feed list */
void ui_feedlist_do_for_all_full(nodePtr ptr, gint filter, gpointer func, gint params, gpointer user_data) {
	GtkTreeIter	childiter;
	gboolean	valid, apply, descend;
	nodePtr		child;
	
	if(NULL == ptr)
		valid = gtk_tree_model_get_iter_root(GTK_TREE_MODEL(feedstore), &childiter);
	else {
		g_assert(ptr->ui_data);
		valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(feedstore), &childiter, &((ui_data*)ptr->ui_data)->row);
	}
	
	while(valid) {
		gtk_tree_model_get(GTK_TREE_MODEL(feedstore), &childiter,
					    FS_PTR, &child, -1);
		/* If child == NULL, this is an empty node. */
		if (child != NULL) {
			apply = (filter & ACTION_FILTER_CHILDREN) ||
				((filter & ACTION_FILTER_FEED) && IS_FEED(child->type)) ||
				((filter & ACTION_FILTER_DIRECTORY) && IS_DIRECTORY(child->type)) ||
				((filter & ACTION_FILTER_FOLDER) && IS_FOLDER(child->type));
			descend = !(filter & ACTION_FILTER_CHILDREN);
			
			if(TRUE == apply) {
				if (params==0)
					((nodeActionFunc)func)(child);
				else 
					((nodeActionDataFunc)func)(child, user_data);
			}
			
			/* if the iter has children and we are descending, iterate over the children. */
			if(descend && (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(feedstore), &childiter) > 0))
				ui_feedlist_do_for_all_data(child, filter, func, user_data);
		}
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(feedstore), &childiter);
	}
}
