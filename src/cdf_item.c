/**
 * @file cdf_item.c CDF item parsing 
 *
 * Copyright (C) 2003, 2004 Lars Lindner <lars.lindner@gmx.net>
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

#include "support.h"
#include "common.h"
#include "cdf_channel.h"
#include "cdf_item.h"
#include "htmlview.h"
#include "metadata.h"

extern GHashTable *cdf_nslist;

static GHashTable *CDFToMetadataMapping = NULL;

/* FIXME: The 'link' tag used to be used, but I coundn't find its
   use... The spec says to use 'A' instead. */

/* method to parse standard tags for each item element */
itemPtr parseCDFItem(feedPtr fp, CDFChannelPtr cp, xmlDocPtr doc, xmlNodePtr cur) {
	gchar		*tmp = NULL, *tmp2, *tmp3;
	itemPtr		ip;

	if((NULL == cur) || (NULL == doc)) {
		g_warning("internal error: XML document pointer NULL! This should not happen!\n");
		return NULL;
	}

	if(CDFToMetadataMapping == NULL) {
		CDFToMetadataMapping = g_hash_table_new(g_str_hash, g_str_equal);
		g_hash_table_insert(CDFToMetadataMapping, "author", "author");
		g_hash_table_insert(CDFToMetadataMapping, "category", "category");
	}
		
	ip = item_new();
	
	/* save the item link */
	tmp = utf8_fix(xmlGetProp(cur, BAD_CAST"href"));
	if(tmp == NULL)
		tmp = utf8_fix(xmlGetProp(cur, BAD_CAST"HREF"));
	if(tmp != NULL)
		item_set_source(ip, tmp);
	g_free(tmp);
	
	cur = cur->xmlChildrenNode;
	while(cur != NULL) {

		if(NULL == cur->name || cur->type != XML_ELEMENT_NODE) {
			cur = cur->next;
			continue;
		}
		
		/* save first link to a channel image */
		tmp = g_ascii_strdown(cur->name, -1);
		if((tmp2 = g_hash_table_lookup(CDFToMetadataMapping, tmp)) != NULL) {
			tmp3 = utf8_fix(xmlNodeListGetString(cur->doc, cur->xmlChildrenNode, TRUE));
			if(tmp3 != NULL) {
				ip->metadata = metadata_list_append(ip->metadata, tmp2, tmp3);
				g_free(tmp3);
			}
		}
		g_free(tmp);
		
		if((!xmlStrcasecmp(cur->name, BAD_CAST"logo"))) {
			
			tmp = utf8_fix(xmlGetProp(cur, BAD_CAST"href"));
			if (tmp == NULL)
				tmp = utf8_fix(xmlGetProp(cur, BAD_CAST"HREF"));
			if (tmp != NULL)
				ip->metadata = metadata_list_append(ip->metadata, "imageUrl", tmp);
			g_free(tmp);
		} else
		if((!xmlStrcasecmp(cur->name, BAD_CAST"title"))) {
			tmp = unhtmlize(utf8_fix(xmlNodeListGetString(cur->doc, cur->xmlChildrenNode, 1)));
			if (tmp != NULL)
				item_set_title(ip, tmp);
			g_free(tmp);
		} else
		if((!xmlStrcasecmp(cur->name, BAD_CAST"abstract"))) {
			tmp = convertToHTML(utf8_fix(xmlNodeListGetString(cur->doc, cur->xmlChildrenNode, 1)));
			if (tmp != NULL)
				item_set_description(ip, tmp);
			g_free(tmp);
		} else
		if((!xmlStrcasecmp(cur->name, BAD_CAST"a"))) {
			tmp = utf8_fix(xmlGetProp(cur, BAD_CAST"href"));
			if (tmp == NULL)
				tmp = utf8_fix(xmlGetProp(cur, BAD_CAST"HREF"));
			if (tmp != NULL)
				item_set_source(ip, tmp);
			g_free(tmp);
		}
		
		cur = cur->next;
	}
	
	/* FIXME: Where is i->time set? */
	/*item_set_time(ip, i->time);*/

	ip->readStatus = FALSE;
	
	return ip;
}
