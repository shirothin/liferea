/**
 * @file itemview.c    item display interface abstraction
 * 
 * Copyright (C) 2006 Lars Lindner <lars.lindner@gmx.net>
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
#include <string.h>

#include "common.h"
#include "conf.h"
#include "debug.h"
#include "htmlview.h"
#include "itemview.h"
#include "node.h"
#include "ui/ui_itemlist.h"
#include "ui/ui_mainwindow.h"

gint disableSortingSaving;		/* set in ui_itemlist.c to disable sort-changed callback */

static struct itemView_priv {
	gboolean	htmlOnly;		/**< TRUE if HTML only mode */
	guint		mode;			/**< current item view mode */
	itemSetPtr	itemSet;		/**< currently item set */
	gboolean	needsUpdate;		/**< item view needs to be updated */
	gchar 		*userDefinedDateFmt;	/**< user defined date formatting string */
} itemView_priv;

void itemview_init(void) {

	/* Determine date format code */
	
	itemView_priv.userDefinedDateFmt = getStringConfValue(DATE_FORMAT);
	
	/* We now have an empty string or a format string... */
	if(itemView_priv.userDefinedDateFmt && !strlen(itemView_priv.userDefinedDateFmt)) {
	   	/* It's empty and useless... */
		g_free(itemView_priv.userDefinedDateFmt);
		itemView_priv.userDefinedDateFmt = NULL;
	}
	
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

	if(itemView_priv.userDefinedDateFmt) {
		debug1(DEBUG_GUI, "new user defined date format: >>>%s<<<", itemView_priv.userDefinedDateFmt);
		gchar *tmp = itemView_priv.userDefinedDateFmt;
		itemView_priv.userDefinedDateFmt = g_locale_from_utf8(tmp, -1, NULL, NULL, NULL);
		g_free(tmp);
	}
	
	/* Setup HTML widget */

	htmlview_init();
}

void itemview_clear(void) {

	ui_itemlist_clear();
	htmlview_clear();
	itemView_priv.needsUpdate = TRUE;
}

void itemview_set_mode(guint mode) {

	if(itemView_priv.mode != mode) {
		itemView_priv.mode = mode;
		itemView_priv.needsUpdate = TRUE;
		htmlview_clear();	/* drop HTML rendering cache */
	}
}

void itemview_set_itemset(itemSetPtr itemSet) {
	GtkTreeModel	*model;

	if(itemSet != itemView_priv.itemSet) {
		itemView_priv.itemSet = itemSet;
		itemView_priv.needsUpdate = TRUE;

		/* 1. Perform UI item list preparations ... */
		
		/* a) Clear item list and disable sorting for performance reasons */

		/* Free the old itemstore and create a new one; this is the only way to disable sorting */
		ui_itemlist_reset_tree_store();	 /* this also clears the itemlist. */
		model = GTK_TREE_MODEL(ui_itemlist_get_tree_store());

		/* b) Enable item list columns as necessary */
		ui_itemlist_enable_encicon_column(FALSE);

		switch(itemSet->type) {
			case ITEMSET_TYPE_FEED:
				ui_itemlist_enable_favicon_column(FALSE);
				break;
			case ITEMSET_TYPE_VFOLDER:
			case ITEMSET_TYPE_FOLDER:
				ui_itemlist_enable_favicon_column(TRUE);
				break;
		}

		/* c) Set sorting again... */
		disableSortingSaving++;
		gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model), 
	                        		     itemSet->node->sortColumn, 
	                        		     itemSet->node->sortReversed?GTK_SORT_DESCENDING:GTK_SORT_ASCENDING);
		disableSortingSaving--;

		/* 2. Reset view state */
		itemview_clear();
		
		/* 3. And repare HTML view */
		htmlview_set_itemset(itemSet);
	}
}

void itemview_add_item(itemPtr item) {

	if(ITEMVIEW_ALL_ITEMS != itemView_priv.mode)
		ui_itemlist_add_item(item);
		
	htmlview_add_item(item);
	itemView_priv.needsUpdate = TRUE;
}

void itemview_remove_item(itemPtr item) {

	if(ITEMVIEW_ALL_ITEMS != itemView_priv.mode)
		ui_itemlist_remove_item(item);

	htmlview_remove_item(item);
	itemView_priv.needsUpdate = TRUE;
}

void itemview_select_item(itemPtr item) {

	ui_itemlist_select(item);
	itemView_priv.needsUpdate = TRUE;
}

void itemview_update_item(itemPtr item) {

	if(!itemset_lookup_item(itemView_priv.itemSet, item->itemSet->node, item->nr))
		return;
		
	if(ITEMVIEW_ALL_ITEMS != itemView_priv.mode)
		ui_itemlist_update_item(item);

	htmlview_update_item(item);
	itemView_priv.needsUpdate = TRUE;
}

void itemview_update(void) {

	if(!itemView_priv.itemSet)
		return;

	if(itemView_priv.needsUpdate) {
		itemView_priv.needsUpdate = FALSE;
		
		ui_itemlist_update();
			
		htmlview_update(ui_mainwindow_get_active_htmlview(), itemView_priv.mode);
	}
}

/* date format handling (not sure if this is the right place) */

gchar * itemview_format_date(time_t date) {
	gchar		*timestr, *tmp;

	if(itemView_priv.userDefinedDateFmt)
		tmp = common_format_date(date, itemView_priv.userDefinedDateFmt);
	else
		tmp = common_format_nice_date(date);
	
	timestr = g_locale_to_utf8(tmp, -1, NULL, NULL, NULL);
	g_free(tmp);
	
	return timestr;
}
