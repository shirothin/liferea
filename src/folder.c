/**
 * @file folder.c feed list and plugin callbacks for folders
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

#include "folder.h"
#include "common.h"
#include "conf.h"
#include "debug.h"
#include "render.h"
#include "support.h"
#include "ui/ui_folder.h"
#include "ui/ui_node.h"
#include "fl_providers/fl_plugin.h"

static void folder_initial_load(nodePtr node) {
	node_foreach_child(node, node_initial_load);
}

/* This callback is used to compute the itemset of folder nodes */
static void folder_merge_itemset(nodePtr node, gpointer userdata) {
	itemSetPtr	itemSet = (itemSetPtr)userdata;

	debug1(DEBUG_GUI, "merging items of node \"%s\"", node_get_title(node));

	switch(node->type) {
		case FST_FOLDER:
			node_foreach_child_data(node, folder_merge_itemset, itemSet);
			break;
		case FST_VFOLDER:
			return;
			break;
		default:
			debug1(DEBUG_GUI, "   pre merge item set: %d items", g_list_length(itemSet->items));
			itemSet->items = g_list_concat(itemSet->items, g_list_copy(node->itemSet->items));
			debug1(DEBUG_GUI, "  post merge item set: %d items", g_list_length(itemSet->items));
			break;
	}
}

static void folder_load(nodePtr node) {

	node_foreach_child(node, node_load);

	if(0 >= node->loaded) {
		/* Concatenate all child item sets to form the folders item set */
		itemSetPtr itemSet = g_new0(struct itemSet, 1);
		itemSet->type = ITEMSET_TYPE_FOLDER;
		node_foreach_child_data(node, folder_merge_itemset, itemSet);
		node_set_itemset(node, itemSet);
	}

	node->loaded++;
}

static void folder_save(nodePtr node) {
	node_foreach_child(node, node_save);
}

static void folder_unload(nodePtr node) {

	if(0 >= node->loaded)
		return;

	if(1 == node->loaded) {
		g_assert(NULL != node->itemSet);
		g_list_free(node->itemSet->items);
		node->itemSet->items = NULL;
	}

	node->loaded--;

	node_foreach_child(node, node_unload);
}

static void folder_reset_update_counter(nodePtr node) {
	node_foreach_child(node, node_reset_update_counter);
}

static void folder_request_update(nodePtr node, guint flags) {
	// FIXME: int -> gpointer
	node_foreach_child_data(node, node_request_update, (gpointer)flags);
}

static void folder_request_auto_update(nodePtr node) {
	
	node_foreach_child(node, node_request_auto_update);
}

static void folder_schedule_update(nodePtr node, guint flags) {

	/* nothing to do */
}

static void folder_remove(nodePtr node) {

	/* remove all children */
	node_foreach_child(node, node_request_remove);

	/* remove the folder */
	node->parent->children = g_slist_remove(node->parent->children, node);
	g_slist_free(node->children);
	node->children = NULL;
	ui_node_remove_node(node);
}

static void folder_mark_all_read(nodePtr node) {

	node_foreach_child(node, node_mark_all_read);
}

static gchar * folder_render(nodePtr node) {
	gchar	*result, *filename, **params = NULL;

	params = render_add_parameter(params, "id='%s'", node->id);
	params = render_add_parameter(params, "headlineCount='%d'", g_list_length(node->itemSet->items));
	filename = FL_PLUGIN(node)->source_get_feedlist(node->source->root);
	result = render_file(filename, "folder", params);
	g_free(filename);
	g_strfreev(params);
	
	return result;
}

static struct nodeType nti = {
	folder_initial_load,
	folder_load,
	folder_save,
	folder_unload,
	folder_reset_update_counter,
	folder_request_update,
	folder_request_auto_update,
	folder_schedule_update,
	folder_remove,
	folder_mark_all_read,
	folder_render,
	ui_folder_add,
	ui_folder_properties
};

nodeTypePtr folder_get_node_type(void) { return &nti; }
