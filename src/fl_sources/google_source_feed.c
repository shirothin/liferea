
#include <glib.h>
#include <string.h>
#include <libxml/xpath.h>

#include "common.h"
#include "conf.h"
#include "debug.h"
#include "xml.h"

#include "google_source.h"
#include "subscription.h"
#include "node.h"

/**
 * This is identical to xpath_foreach_match, except that it takes the context
 * as parameter.
 */
static void
google_source_xpath_foreach_match(gchar* expr, xmlXPathContextPtr xpathCtxt, xpathMatchFunc func, gpointer user_data) 
{
	xmlXPathObjectPtr xpathObj = NULL;
	xpathObj = xmlXPathEval ((xmlChar*)expr, xpathCtxt);
	
	if (xpathObj && xpathObj->nodesetval && xpathObj->nodesetval->nodeMax) {
		int	i;
		for (i = 0; i < xpathObj->nodesetval->nodeNr; i++) {
			(*func) (xpathObj->nodesetval->nodeTab[i], user_data);
			xpathObj->nodesetval->nodeTab[i] = NULL ;
		}
	}
	
	if (xpathObj)
		xmlXPathFreeObject (xpathObj);
}

void
google_source_migrate_node(nodePtr node) 
{
	/* scan the node for bad ID's, if so, brutally remove the node */
	itemSetPtr itemset = node_get_itemset(node);
	GList *iter = itemset->ids;
	for (; iter; iter = g_list_next(iter)) {
		itemPtr item = item_load (GPOINTER_TO_UINT (iter->data));
		if (item && item->sourceId) {
			if (!g_str_has_prefix(item->sourceId, "tag:google.com")) {
				debug1(DEBUG_UPDATE, "Item with sourceId [%s] will be deleted.", item->sourceId);
				db_item_remove(GPOINTER_TO_UINT(iter->data));
			} 
		}
		if (item) item_unload (item) ;
	}

	/* cleanup */
	itemset_free(itemset);
}

static void
google_source_xml_unlink_node(xmlNodePtr node, gpointer data) 
{
	xmlUnlinkNode(node) ;
	xmlFreeNode(node) ;
}

void
google_source_item_mark_read (nodePtr node, itemPtr item, 
                              gboolean newStatus)
{
	/**
	 * This global is an internal hack so that this source file
	 * may call item_state_set_read without any network calls being
	 * made. @see google_source_edit_action_cb
	 */
	if (googleSourceBlockEditHack)
		return;

	const gchar* sourceUrl = item_get_real_source_url(item);
	if (!sourceUrl) sourceUrl = node->subscription->source;
	debug1(DEBUG_UPDATE, "GoogleSource: real source is %s", sourceUrl);
	nodePtr root = google_source_get_root_from_node(node);
	google_source_edit_mark_read((GoogleSourcePtr)root->data, 
				     item->sourceId, 
				     sourceUrl,
				     newStatus);
}

static void
google_source_set_real_source_url(const xmlNodePtr node, gpointer userdata)
{
	itemPtr item = (itemPtr) userdata ;
	xmlChar*   value = xmlNodeGetContent(node);
	
	debug1(DEBUG_UPDATE, "GoogleSource: Got %s as id while updating", value);
	const gchar*     prefix = "tag:google.com,2005:reader/feed/";
	if (g_str_has_prefix(value, prefix)) {
		item_set_real_source_url(item, value + strlen(prefix));
		db_item_update(item);
	}
}
static void
google_source_item_retrieve_status (const xmlNodePtr entry, gpointer userdata)
{
	subscriptionPtr subscription = (subscriptionPtr) userdata;
	GoogleSourcePtr gsource = (GoogleSourcePtr) google_source_get_root_from_node(subscription->node)->data ;
	xmlNodePtr xml;
	nodePtr node = subscription->node;

	xmlChar* id;
	
	xml = entry->children;
	g_assert (xml);
	g_assert (g_str_equal (xml->name, "id"));

	id = xmlNodeGetContent (xml);

	gboolean read = FALSE;
	for (xml = entry->children; xml; xml = xml->next ) {
		if (g_str_equal (xml->name, "category")) {
			xmlChar* label = xmlGetProp (xml, "label");
			if (!label)
				continue;

			if (g_str_equal (label, "read")) {
				debug1 (DEBUG_UPDATE, "Google Reader item '%s' will be marked as read", id);
				read = TRUE;
				xmlFree (label);
				break;
			}
			xmlFree (label);
		}
	}

	itemSetPtr itemset = node_get_itemset(node);
	GList *iter = itemset->ids;
	for (; iter; iter = g_list_next(iter)) {
		/* this is extremely inefficient, multiple times loading */
		itemPtr item = item_load (GPOINTER_TO_UINT (iter->data));
		if (item && item->sourceId) {
			if (g_str_equal (item->sourceId, id) && !google_source_edit_is_in_queue(gsource, id)) {
				
				googleSourceBlockEditHack = TRUE;
				item_state_set_read (item, read);
				googleSourceBlockEditHack = FALSE;

				if (g_str_equal(subscription->source, GOOGLE_READER_BROADCAST_FRIENDS_URL)) {
					/* set the real_source, hopefully this is the correct thing to do */
					xmlXPathContextPtr xpathCtxt = xmlXPathNewContext (entry->doc) ;
					xmlXPathRegisterNs(xpathCtxt, "atom", "http://www.w3.org/2005/Atom");
					xpathCtxt->node = entry;

					google_source_xpath_foreach_match("./atom:source/atom:id", xpathCtxt, google_source_set_real_source_url, item);

					/* free up xpath related data */
					if (xpathCtxt) xmlXPathFreeContext(xpathCtxt);
				}
				item_unload (item);
				goto cleanup;
			}
		}
		if (item) item_unload (item) ;
	}

cleanup:
	itemset_free (itemset);
	xmlFree (id);
}


static void
google_feed_subscription_process_update_result (subscriptionPtr subscription,
                                                const struct updateResult* const result, 
                                                updateFlags flags)
{

	if ( result->data ) { 
		updateResultPtr resultCopy ;
		
		resultCopy = update_result_new() ;
		resultCopy->source = g_strdup(result->source); 
		resultCopy->returncode = result->returncode;
		resultCopy->httpstatus = result->httpstatus;
		resultCopy->contentType = g_strdup(result->contentType);
		resultCopy->retriesCount = result->retriesCount ;
		resultCopy->updateState = update_state_copy(result->updateState);
		
		/* update the XML by removing 'read', 'reading-list' etc. as labels. */
		xmlDocPtr doc = xml_parse (result->data, result->size, FALSE, NULL);
		xmlXPathContextPtr xpathCtxt = xmlXPathNewContext (doc) ;
		xmlXPathRegisterNs(xpathCtxt, "atom", "http://www.w3.org/2005/Atom");
		google_source_xpath_foreach_match("/atom:feed/atom:entry/atom:category[@scheme='http://www.google.com/reader/']", xpathCtxt, google_source_xml_unlink_node, NULL);
		xmlXPathFreeContext(xpathCtxt);
		
		/* good now we have removed the read and unread labels. */
		
		xmlChar    *newXml; 
		int        newXmlSize ;
		
		xmlDocDumpMemory(doc, &newXml, &newXmlSize) ;
		
		resultCopy->data = g_strndup((gchar*)newXml, newXmlSize);
		resultCopy->size = newXmlSize ;
		
		xmlFree(newXml) ;
		xmlFreeDoc(doc) ;
		
		feed_get_subscription_type ()->process_update_result (subscription, resultCopy, flags);
		update_result_free(resultCopy);
	} else { 
		feed_get_subscription_type()->process_update_result(subscription, result, flags);
		return ; 
	}
	
	debug_start_measurement(DEBUG_UPDATE);
	xmlDocPtr doc = xml_parse (result->data, result->size, FALSE, NULL);
	if (doc) {		
		xmlNodePtr root = xmlDocGetRootElement (doc);
		xmlNodePtr entry = root->children ; 
		
		while (entry) { 
			if (!g_str_equal (entry->name, "entry")) {
				entry = entry->next;
				continue; /* not an entry */
			}
			
			google_source_item_retrieve_status (entry, subscription);
			entry = entry->next;
		}
		
		xmlFreeDoc (doc);
	} else { 
		debug0 (DEBUG_UPDATE, "google_feed_subscription_process_update_result(): Couldn't parse XML!");
		g_warning ("google_feed_subscription_process_update_result(): Couldn't parse XML!");
	}
	debug_end_measurement(DEBUG_UPDATE, "time taken to update statuses");
}



static gboolean
google_feed_subscription_prepare_update_request (subscriptionPtr subscription, 
                                                 struct updateRequest *request)
{
	debug0 (DEBUG_UPDATE, "preparing google reader feed subscription for update\n");
	GoogleSourcePtr gsource = (GoogleSourcePtr) google_source_get_root_from_node (subscription->node)->data; 
	
	g_assert(gsource); 
	if (gsource->loginState == GOOGLE_SOURCE_STATE_NONE) { 
		subscription_update(google_source_get_root_from_node (subscription->node)->subscription, 0) ;
		return FALSE;
	}
	debug0 (DEBUG_UPDATE, "Setting cookies for a Google Reader subscription");

	if ( !g_str_equal(request->source, GOOGLE_READER_BROADCAST_FRIENDS_URL) ) { 
		gchar* source_escaped = g_uri_escape_string(request->source,
							    NULL, TRUE);
		gchar* newUrl = g_strdup_printf("http://www.google.com/reader/atom/feed/%s", source_escaped) ;
		update_request_set_source(request, newUrl) ;
		g_free (newUrl);
		g_free(source_escaped);
	}
	update_state_set_cookies (request->updateState, gsource->sid);
	return TRUE;
}

struct subscriptionType googleSourceFeedSubscriptionType = {
	google_feed_subscription_prepare_update_request,
	google_feed_subscription_process_update_result,
	NULL  /* free */
};
