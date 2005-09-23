/**
 * @file callbacks.c misc UI stuff
 *
 * Most of the GUI code is distributed over the ui_*.c
 * files but what didn't fit somewhere else stayed here.
 * 
 * Copyright (C) 2003-2005 Lars Lindner <lars.lindner@gmx.net>
 * Copyright (C) 2004-2005 Nathan J. Conrad <t98502@users.sourceforge.net>
 * Copyright (C) 2004 Christophe Barbe <christophe.barbe@ufies.org>	
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <string.h>

#include "debug.h"
#include "interface.h"
#include "support.h"
#include "folder.h"
#include "feedlist.h"
#include "item.h"
#include "conf.h"
#include "export.h"
#include "common.h"
#include "callbacks.h"
#include "ui_mainwindow.h"
#include "ui_folder.h"
#include "ui_feedlist.h"
#include "ui_htmlview.h"
#include "ui_itemlist.h"
#include "ui_tray.h"
#include "ui_queue.h"
#include "ui_notification.h"
#include "ui_enclosure.h"
#include "ui_tabs.h"
	
/* all used icons */
GdkPixbuf *icons[MAX_ICONS];

/* icon names */
static gchar *iconNames[] = {	"read.xpm",		/* ICON_READ */
				"unread.png",		/* ICON_UNREAD */
				"flag.png",		/* ICON_FLAG */
				"available.png",	/* ICON_AVAILABLE */
				NULL,			/* ICON_UNAVAILABLE */
				"ocs.png",		/* ICON_OCS */
				"directory.png",	/* ICON_FOLDER */
				"vfolder.png",		/* ICON_VFOLDER */
				"empty.png",		/* ICON_EMPTY */
				"online.png",		/* ICON_ONLINE */
				"offline.png",		/* ICON_OFFLINE */
				"edit.png",		/* ICON_UPDATED */
				NULL
				};
				
/*------------------------------------------------------------------------------*/
/* generic GUI functions							*/
/*------------------------------------------------------------------------------*/

/* GUI initialization, must be called only once! */

static void callbacks_schedule_update_default_cb(nodePtr ptr) {
	feed_schedule_update((feedPtr)ptr, 0);
}

void ui_init(int mainwindowState) {
	GtkWidget	*widget;
	int		i;

	mainwindow = ui_mainwindow_new();
	ui_tabs_init();
	
	/* load pane proportions */
	if(0 != getNumericConfValue(LAST_VPANE_POS))
		gtk_paned_set_position(GTK_PANED(lookup_widget(mainwindow, "leftpane")), getNumericConfValue(LAST_VPANE_POS));
	if(0 != getNumericConfValue(LAST_HPANE_POS))
		gtk_paned_set_position(GTK_PANED(lookup_widget(mainwindow, "rightpane")), getNumericConfValue(LAST_HPANE_POS));

	/* order important !!! */
	ui_feedlist_init(lookup_widget(mainwindow, "feedlist"));
	ui_itemlist_init(lookup_widget(mainwindow, "Itemlist"));
			
	for(i = 0;  i < MAX_ICONS; i++)
		icons[i] = create_pixbuf(iconNames[i]);

	/* set up icons that are build from stock */
	widget = gtk_button_new();
	icons[ICON_UNAVAILABLE] = gtk_widget_render_icon(widget, GTK_STOCK_DIALOG_ERROR, GTK_ICON_SIZE_MENU, "");
	gtk_widget_destroy(widget);
	
	ui_mainwindow_update_toolbar();
	ui_mainwindow_update_menubar();
	ui_mainwindow_update_onlinebtn();
	
	ui_tray_enable(getBooleanConfValue(SHOW_TRAY_ICON));			/* init tray icon */
	ui_dnd_setup_URL_receiver(mainwindow);	/* setup URL dropping support */
	ui_popup_setup_menues();		/* create popup menues */
	ui_enclosure_init();
	conf_load_subscriptions();

	switch(getNumericConfValue(STARTUP_FEED_ACTION)) {
	case 1: /* Update all feeds */
		ui_feedlist_do_for_all(NULL, ACTION_FILTER_FEED, (gpointer)callbacks_schedule_update_default_cb);
		break;
	case 2:
		ui_feedlist_do_for_all(NULL, ACTION_FILTER_FEED, (gpointer)feed_reset_update_counter);
		break;
	default:
		/* default, which is to use the lastPoll times, does not need any actions here. */;
	}

	feedlist_init();
	
	if (mainwindowState == MAINWINDOW_ICONIFIED || (mainwindowState == MAINWINDOW_HIDDEN && ui_tray_get_count() == 0)) {
		gtk_window_iconify(GTK_WINDOW(mainwindow));
		gtk_widget_show(mainwindow);
	} else if (mainwindowState == MAINWINDOW_SHOWN)
		gtk_widget_show(mainwindow);
	else
		/* Needed so that the window structure can be
		   accessed... otherwise will GTK warning when window is
		   shown by clicking on notification icon. */
		gtk_widget_realize(GTK_WIDGET(mainwindow)); 
	ui_mainwindow_finish(mainwindow); /* Ugly hack to make mozilla work */
		
}

void ui_redraw_widget(gchar *name) {
	GtkWidget	*list;
	gchar		*msg;
	
	if(NULL == mainwindow)
		return;
	
	if(NULL != (list = lookup_widget(mainwindow, name)))
		gtk_widget_queue_draw(list);
	else {
		msg = g_strdup_printf("Fatal! Could not lookup widget \"%s\"!", name);
		g_warning(msg);
		g_free(msg);
	}
}

/*------------------------------------------------------------------------------*/
/* status bar callback, error box function					*/
/*------------------------------------------------------------------------------*/

void ui_show_error_box(const char *format, ...) {
	GtkWidget	*dialog;
	va_list		args;
	gchar		*msg;

	g_return_if_fail(format != NULL);

	va_start(args, format);
	msg = g_strdup_vprintf(format, args);
	va_end(args);
	
	dialog = gtk_message_dialog_new(GTK_WINDOW(mainwindow),
                  GTK_DIALOG_DESTROY_WITH_PARENT,
                  GTK_MESSAGE_ERROR,
                  GTK_BUTTONS_CLOSE,
                  "%s", msg);
	(void)gtk_dialog_run(GTK_DIALOG (dialog));
	gtk_widget_destroy(dialog);
	g_free(msg);
}

void ui_show_info_box(const char *format, ...) { 
	GtkWidget	*dialog;
	va_list		args;
	gchar		*msg;

	g_return_if_fail(format != NULL);

	va_start(args, format);
	msg = g_strdup_vprintf(format, args);
	va_end(args);
		
	dialog = gtk_message_dialog_new(GTK_WINDOW(mainwindow),
                  GTK_DIALOG_DESTROY_WITH_PARENT,
                  GTK_MESSAGE_INFO,
                  GTK_BUTTONS_CLOSE,
                  "%s", msg);
	(void)gtk_dialog_run(GTK_DIALOG (dialog));
	gtk_widget_destroy(dialog);
	g_free(msg);
}

/*------------------------------------------------------------------------------*/
/* exit handler									*/
/*------------------------------------------------------------------------------*/

void on_popup_quit(gpointer callback_data, guint callback_action, GtkWidget *widget) {

	(void)on_quit(NULL, NULL, NULL);
}

void on_about_activate(GtkMenuItem *menuitem, gpointer user_data) {
	
	gtk_widget_show(create_aboutdialog());
}

void on_homepagebtn_clicked(GtkButton *button, gpointer user_data) {

	/* launch the homepage when button in about dialog is pressed */
	ui_htmlview_launch_in_external_browser(_("http://liferea.sf.net"));
}

void on_topics_activate(GtkMenuItem *menuitem, gpointer user_data) {
	gchar *filename = g_strdup_printf("file://" PACKAGE_DATA_DIR "/" PACKAGE "/doc/html/%s", _("topics_en.html"));
	ui_tabs_new(filename, _("Help Topics"), TRUE);
	g_free(filename);
}


void on_quick_reference_activate(GtkMenuItem *menuitem, gpointer user_data) {
	gchar *filename = g_strdup_printf("file://" PACKAGE_DATA_DIR "/" PACKAGE "/doc/html/%s", _("reference_en.html"));
	ui_tabs_new(filename, _("Quick Reference"), TRUE);
	g_free(filename);
}

