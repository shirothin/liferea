/**
 * @file liferea_htmlview.c  Liferea embedded HTML rendering
 *
 * Copyright (C) 2003-2010 Lars Lindner <lars.lindner@gmail.com>
 * Copyright (C) 2005-2006 Nathan J. Conrad <t98502@users.sourceforge.net> 
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

#include "ui/liferea_htmlview.h"

#include <string.h>
#include <sys/wait.h>
#include <glib.h>

#include "browser.h"
#include "comments.h"
#include "common.h"
#include "conf.h"
#include "debug.h"
#include "enclosure.h"
#include "feed.h"
#include "feedlist.h"
#include "itemlist.h"
#include "net.h"
#include "net_monitor.h"
#include "social.h"
#include "render.h"
#include "htmlview.h"
#include "ui/browser_tabs.h"
#include "ui/liferea_shell.h"
#include "ui/item_list_view.h"
#include "ui/ui_common.h"
#include "ui/ui_prefs.h"

#define RENDERER(htmlview)	(htmlview->priv->impl)

#define LIFEREA_HTMLVIEW_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), LIFEREA_HTMLVIEW_TYPE, LifereaHtmlViewPrivate))

struct LifereaHtmlViewPrivate {
	GtkWidget	*renderWidget;
	gboolean	internal;		/**< TRUE if internal view presenting generated HTML with special links */
	gboolean	forceInternalBrowsing;	/**< TRUE if clicked links should be force loaded within this view (regardless of global preference) */
	
	htmlviewImplPtr impl;			/**< browser widget support implementation */
};

enum {
	STATUSBAR_CHANGED,
	TITLE_CHANGED,
	LOCATION_CHANGED,
	LAST_SIGNAL
};

static guint liferea_htmlview_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

/* -------------------------------------------------------------------- */
/* Liferea HTML rendering object					*/
/* -------------------------------------------------------------------- */

G_DEFINE_TYPE (LifereaHtmlView, liferea_htmlview, G_TYPE_OBJECT);

static void
liferea_htmlview_finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
liferea_htmlview_class_init (LifereaHtmlViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = liferea_htmlview_finalize;
	
	liferea_htmlview_signals[STATUSBAR_CHANGED] = 
		g_signal_new ("statusbar-changed", 
		G_OBJECT_CLASS_TYPE (object_class),
		(GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
		0, 
		NULL,
		NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE,
		1,
		G_TYPE_STRING);

	liferea_htmlview_signals[TITLE_CHANGED] = 
		g_signal_new ("title-changed", 
		G_OBJECT_CLASS_TYPE (object_class),
		(GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
		0, 
		NULL,
		NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE,
		1,
		G_TYPE_STRING);

	liferea_htmlview_signals[LOCATION_CHANGED] = 
		g_signal_new ("location-changed", 
		G_OBJECT_CLASS_TYPE (object_class),
		(GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
		0, 
		NULL,
		NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE,
		1,
		G_TYPE_STRING);

	htmlview_get_impl ()->init ();

	g_type_class_add_private (object_class, sizeof (LifereaHtmlViewPrivate));
}

static void
liferea_htmlview_init (LifereaHtmlView *htmlview)
{
	htmlview->priv = LIFEREA_HTMLVIEW_GET_PRIVATE (htmlview);
	htmlview->priv->internal = FALSE;
	htmlview->priv->impl = htmlview_get_impl ();
	htmlview->priv->renderWidget = RENDERER (htmlview)->create (htmlview);
}

static void
liferea_htmlview_set_online (LifereaHtmlView *htmlview, gboolean online)
{
	if (RENDERER (htmlview)->setOffLine)
		(RENDERER (htmlview)->setOffLine) (!online);
}

static void
liferea_htmlview_online_status_changed (NetworkMonitor *nm, gboolean online, gpointer userdata)
{
	LifereaHtmlView *htmlview = LIFEREA_HTMLVIEW (userdata);
	
	liferea_htmlview_set_online (htmlview, online);
}

static void
liferea_htmlview_proxy_changed (NetworkMonitor *nm, gpointer userdata)
{
	LifereaHtmlView *htmlview = LIFEREA_HTMLVIEW (userdata);

	(RENDERER (htmlview)->setProxy) (network_get_proxy_host (),
	                                 network_get_proxy_port (),
	                                 network_get_proxy_username (),
	                                 network_get_proxy_password ());
}

LifereaHtmlView *
liferea_htmlview_new (gboolean forceInternalBrowsing)
{
	LifereaHtmlView *htmlview;
		
	htmlview = LIFEREA_HTMLVIEW (g_object_new (LIFEREA_HTMLVIEW_TYPE, NULL));
	htmlview->priv->forceInternalBrowsing = forceInternalBrowsing;

	liferea_htmlview_clear (htmlview);
	
	g_signal_connect (network_monitor_get (), "online-status-changed",
	                  G_CALLBACK (liferea_htmlview_online_status_changed),
	                  htmlview);
	g_signal_connect (network_monitor_get (), "proxy-changed",
	                  G_CALLBACK (liferea_htmlview_proxy_changed),
	                  htmlview);

	if (NULL != network_get_proxy_host ()) {
		debug0 (DEBUG_NET, "Setting initial HTML widget proxy...");
		liferea_htmlview_proxy_changed (network_monitor_get (), htmlview);
	}
	
	return htmlview;
}

/* Needed when adding widget to a parent and querying GTK theme */
GtkWidget *
liferea_htmlview_get_widget (LifereaHtmlView *htmlview)
{
	return htmlview->priv->renderWidget;
}

void
liferea_htmlview_write (LifereaHtmlView *htmlview, const gchar *string, const gchar *base)
{ 
	const gchar	*baseURL = base;
	
	htmlview->priv->internal = TRUE;	/* enables special links */
	
	if (baseURL == NULL)
		baseURL = "file:///";

	if (debug_level & DEBUG_HTML) {
		gchar *filename = common_create_cache_filename (NULL, "output", "xhtml");
		g_file_set_contents (filename, string, -1, NULL);
		g_free (filename);
	}
	
	if (!g_utf8_validate (string, -1, NULL)) {
		/* It is really a bug if we get invalid encoded UTF-8 here!!! */
		g_error ("Invalid encoded UTF8 buffer passed to HTML widget!");
	} else {
		(RENDERER (htmlview)->write) (htmlview->priv->renderWidget, string, strlen (string), baseURL, "application/xhtml+xml");
	}
}

void
liferea_htmlview_clear (LifereaHtmlView *htmlview)
{
	GString	*buffer;

	buffer = g_string_new (NULL);
	htmlview_start_output (buffer, NULL, FALSE, FALSE);
	htmlview_finish_output (buffer); 
	liferea_htmlview_write (htmlview, buffer->str, NULL);
	g_string_free (buffer, TRUE);
}

gboolean
liferea_htmlview_is_special_url (const gchar *url)
{
	/* match against all special protocols, simple
	   convention: all have to start with "liferea-" */
	if (url == strstr (url, "liferea-"))
		return TRUE;
	
	return FALSE;
}

struct internalUriType {
	const gchar	*suffix;
	void		(*func)(itemPtr item);
};

static struct internalUriType internalUriTypes[] = {
	{ "flag",		itemlist_toggle_flag },
	{ "bookmark",		social_add_bookmark },
	{ "refresh-comments",	comments_refresh },
	{ NULL,			NULL }
};

void
liferea_htmlview_on_url (LifereaHtmlView *htmlview, const gchar *url)
{
	if (!liferea_htmlview_is_special_url (url))
		g_signal_emit_by_name (htmlview, "statusbar-changed", url);
}

void
liferea_htmlview_title_changed (LifereaHtmlView *htmlview, const gchar *title)
{
	g_signal_emit_by_name (htmlview, "title-changed", title);
}

void
liferea_htmlview_location_changed (LifereaHtmlView *htmlview, const gchar *location)
{
	g_signal_emit_by_name (htmlview, "location-changed", location);
}

gboolean
liferea_htmlview_handle_URL (LifereaHtmlView *htmlview, const gchar *url)
{
	struct internalUriType	*uriType;
	gboolean browse_inside_application;
	
	g_return_val_if_fail (htmlview, TRUE);
	g_return_val_if_fail (url, TRUE);

	conf_get_bool_value (BROWSE_INSIDE_APPLICATION, &browse_inside_application);

	debug3 (DEBUG_GUI, "handle URL: %s %s %s",
	        browse_inside_application?"true":"false",
	        htmlview->priv->forceInternalBrowsing?"true":"false",
		htmlview->priv->internal?"true":"false");

	/* first catch all links with special URLs... */
	if (liferea_htmlview_is_special_url (url)) {
		if (htmlview->priv->internal) {
	
			/* it is a generic item list URI type */		
			uriType = internalUriTypes;
			while (uriType->suffix) {
				if (!strncmp (url + strlen ("liferea-"), uriType->suffix, strlen (uriType->suffix))) {
					gchar *nodeid, *itemnr;
					nodeid = strstr (url, "://");
					if (nodeid) {
						nodeid += 3;
						itemnr = strchr (nodeid, '-');
						if (itemnr) {
							itemPtr item;

							*itemnr = 0;
							itemnr++;

							item = item_load (atol (itemnr));
							if (item) {
								(*uriType->func) (item);
								item_unload (item);
							} else {
								g_warning ("Fatal: no item with id (node=%s, item=%s) found!!!", nodeid, itemnr);
							}

							return TRUE;
						}
					}
				}
				uriType++;
			}
			g_warning ("Internal error: unhandled protocol in URL \"%s\"!", url);
		} else {
			g_warning ("Security: Prevented external HTML document to use internal link scheme (%s)!", url);
		}
		return TRUE;
	}
	
	if(htmlview->priv->forceInternalBrowsing || browse_inside_application) {	   
	   	/* before loading external content suppress internal link schema again */
		htmlview->priv->internal = FALSE;
		
		return FALSE;
	} else {
		(void)browser_launch_URL_external (url);
	}
	
	return TRUE;
}

void
liferea_htmlview_launch_URL_internal (LifereaHtmlView *htmlview, const gchar *url)
{
	/* before loading untrusted URLs suppress internal link schema */
	htmlview->priv->internal = FALSE;
	
	(RENDERER (htmlview)->launch) (htmlview->priv->renderWidget, url);
}

void
liferea_htmlview_set_zoom (LifereaHtmlView *htmlview, gfloat diff)
{
	(RENDERER (htmlview)->zoomLevelSet) (htmlview->priv->renderWidget, diff); 
}

gfloat
liferea_htmlview_get_zoom (LifereaHtmlView *htmlview)
{
	return (RENDERER (htmlview)->zoomLevelGet) (htmlview->priv->renderWidget);
}

gboolean
liferea_htmlview_scroll (LifereaHtmlView *htmlview)
{
	return (RENDERER (htmlview)->scrollPagedown) (htmlview->priv->renderWidget);
}

void
liferea_htmlview_do_zoom (LifereaHtmlView *htmlview, gboolean in)
{
	gfloat factor = in?1.2:0.8;
	
	liferea_htmlview_set_zoom (htmlview, factor * liferea_htmlview_get_zoom (htmlview));
}

/* popup callbacks and popup handling */

static void
on_popup_launch_link_activate (GtkWidget *widget, gpointer user_data)
{
	browser_launch_URL_external ((gchar *)user_data);
}

static void
on_popup_copy_activate (GtkWidget *widget, LifereaHtmlView *htmlview)
{
	(RENDERER (htmlview)->copySelection) (htmlview->priv->renderWidget); 
}

static void
on_popup_copy_url_activate (GtkWidget *widget, gpointer user_data)
{
	GtkClipboard	*clipboard;
	gchar		*link = common_uri_sanitize ((gchar *)user_data);

	clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);
	gtk_clipboard_set_text (clipboard, link, -1);
 
	clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text (clipboard, link, -1);

	g_free (link);
}

static void
on_save_url (const gchar *filename, gpointer user_data)
{
	/* FIXME: The following partially duplicates download code in enclosure.c! */
	enclosureDownloadToolPtr tool;
	gchar	*stdout_message = NULL, *stderr_message = NULL;
	GError	*error = NULL;
	gint	status = 0;
	gchar	*uriQ, *uri = (gpointer)user_data;
	gchar	*cmd;
	
	if (!filename)
		return;

	tool = prefs_get_download_tool ();
	uriQ = g_shell_quote (uri);
	cmd = g_strdup_printf (tool->format, filename, uriQ);

	debug1 (DEBUG_UPDATE, "running download command \"%s\"", cmd);
	g_spawn_command_line_sync (cmd, &stdout_message, &stderr_message, &status, &error);
	
	if ((error && (0 != error->code)) || !WIFEXITED(status) || WEXITSTATUS(status)) {
		g_warning ("Failed to execute command \"%s\", exited: %i, status: %i, stderr: %s, stdout: %s", cmd, WIFEXITED(status), WEXITSTATUS(status), stderr_message?:"", stdout_message?:"");
		liferea_shell_set_status_bar (_("Download FAILED: \"%s\""), uri);
	} else {
		liferea_shell_set_status_bar (_("Download finished."));
	}
	
	if (error)
		g_error_free (error);
	g_free (stdout_message);
	g_free (stderr_message);
	g_free (cmd);
}

static void
on_popup_save_url_activate (GtkWidget *widget, gpointer user_data)
{
	gchar	*uri = (gpointer)user_data;
	gchar	*filename;
	
	filename = strrchr (uri, '/');
	if (filename)
		filename++; /* Skip the slash to find the filename */
	else
		filename = uri;
	
	ui_choose_file (_("Choose File"), GTK_STOCK_SAVE_AS, TRUE, on_save_url, NULL, filename, NULL, NULL, uri);
}

static void
on_popup_subscribe_url_activate (GtkWidget *widget, gpointer user_data)
{
	feedlist_add_subscription ((gchar *)user_data, NULL, NULL, 0);
}

static void
on_popup_zoomin_activate (GtkWidget *widget, gpointer user_data)
{
	liferea_htmlview_do_zoom (LIFEREA_HTMLVIEW (user_data), TRUE);
}

static void
on_popup_zoomout_activate (GtkWidget *widget, gpointer user_data)
{
	liferea_htmlview_do_zoom (LIFEREA_HTMLVIEW (user_data), FALSE);
}

static void
on_popup_open_link_in_tab_activate (GtkWidget *widget, gpointer user_data)
{
	browser_tabs_add_new ((gchar *)user_data, (gchar *)user_data, FALSE);
}

static void
on_popup_social_bm_link_activate (GtkWidget *widget, gpointer user_data)
{	
	gchar *url = social_get_bookmark_url ((gchar *)user_data, "");
	(void)browser_tabs_add_new (url, url, TRUE);
	g_free (url);
}

static GtkWidget *
menu_add_option (GtkMenu *menu, const gchar *label, const gchar *stock, gpointer cb, gpointer user_data)
{
	GtkWidget *item, *image;

	if (label) {
		image = gtk_image_new_from_stock (stock, GTK_ICON_SIZE_MENU);
		item = gtk_image_menu_item_new_with_mnemonic (label);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	}
	else
		item = gtk_image_menu_item_new_from_stock (stock, NULL);
	g_signal_connect (item, "activate", G_CALLBACK (cb), user_data);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	return item;
}

static void
menu_add_separator (GtkMenu *menu)
{
	GtkWidget *widget;
	
	widget = gtk_separator_menu_item_new ();
	gtk_widget_show (widget);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
}

void
liferea_htmlview_prepare_context_menu (LifereaHtmlView *htmlview, GtkMenu *menu, const gchar *linkUri, const gchar *imageUri)
{
	GList *item, *items;
	gboolean link = (linkUri != NULL);
	gboolean image = (imageUri != NULL);

	/* first drop all menu items that are provided by the browser widget (necessary for WebKit) */
	item = items = gtk_container_get_children (GTK_CONTAINER (menu));
	while (item) {
		gtk_widget_destroy (GTK_WIDGET (item->data));
		item = g_list_next (item);
	}
	g_list_free (items);

	/* do not expose internal links */
	if (linkUri && liferea_htmlview_is_special_url (linkUri) && !g_str_has_prefix (linkUri, "javascript:") && !g_str_has_prefix (linkUri, "data:"))
		link = FALSE;

	/* and now add all we want to see */
	if (link) {
		gchar *path;
		menu_add_option (menu, _("Launch Link In _Tab"), NULL, G_CALLBACK (on_popup_open_link_in_tab_activate), (gpointer)linkUri);
		menu_add_option (menu, _("_Launch Link In Browser"), NULL, G_CALLBACK (on_popup_launch_link_activate), (gpointer)linkUri);
		menu_add_separator (menu);
		
		path = g_strdup_printf (_("_Bookmark Link at %s"), social_get_bookmark_site ());
		menu_add_option (menu, path, NULL, on_popup_social_bm_link_activate, (gpointer)linkUri);
		g_free (path);

		menu_add_option (menu, _("_Copy Link Location"), "gtk-copy", G_CALLBACK (on_popup_copy_url_activate), (gpointer)linkUri);
	}
	if (image)
		menu_add_option (menu, _("_Copy Image Location"), "gtk-copy", G_CALLBACK (on_popup_copy_url_activate), (gpointer)imageUri);
	if (link)
		menu_add_option (menu, _("S_ave Link As"), "gtk-save", G_CALLBACK (on_popup_save_url_activate), (gpointer)linkUri);
	if (image)
		menu_add_option (menu, _("S_ave Image As"), "gtk-save", G_CALLBACK (on_popup_save_url_activate), (gpointer)imageUri);
	if (link) {	
		menu_add_separator (menu);
		menu_add_option (menu, _("_Subscribe..."), "gtk-add", G_CALLBACK (on_popup_subscribe_url_activate), (gpointer)linkUri);
	}
	
	if(!link && !image) {
		GtkWidget *item;
		item = menu_add_option (menu, NULL, GTK_STOCK_COPY, G_CALLBACK (on_popup_copy_activate), htmlview);
		if (!(RENDERER (htmlview)->hasSelection) (htmlview->priv->renderWidget)) 
			gtk_widget_set_sensitive (item, FALSE);

		menu_add_separator (menu);
		menu_add_option (menu, _("_Increase Text Size"), "gtk-zoom-in", G_CALLBACK (on_popup_zoomin_activate), htmlview);
		menu_add_option (menu, _("_Decrease Text Size"), "gtk-zoom-out", G_CALLBACK (on_popup_zoomout_activate), htmlview);
	}
}
