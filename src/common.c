/*
   common routines for Liferea
   
   Copyright (C) 2003 Lars Lindner <lars.lindner@gmx.net>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERuri[i]ANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <langinfo.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include "support.h"
#include "common.h"
#include "conf.h"
#include "support.h"
#include "feed.h"

#define	TIMESTRLEN	256

#define VFOLDER_EXTENSION	"vfolder"
#define OCS_EXTENSION		"ocs"

static gchar *CACHEPATH = NULL;

gchar * convertCharSet(gchar * from_encoding, gchar * to_encoding, gchar * string);

void addToHTMLBuffer(gchar **buffer, gchar *string) {
	gchar	*newbuffer;
	
	if(NULL == string)
		return;
	
	if(NULL != *buffer) {
		newbuffer = g_strdup_printf("%s%s", *buffer, string);
		g_free(*buffer);
		*buffer = newbuffer;
	} else {
		*buffer = g_strdup(string);
	}
}

/* converts the string string encoded in from_encoding (which
   can be NULL) to to_encoding, frees the original string and 
   returns the result */
gchar * convertCharSet(gchar * from_encoding, gchar * to_encoding, gchar * string) {
	gint	bw, br;
	gchar	*new = NULL;
	
	if(NULL == from_encoding)
		from_encoding = standard_encoding;
		
	if(NULL != string) {		
		// FIXME: is this thread safe?	
		new = g_convert(string, strlen(string), from_encoding, to_encoding, &br, &bw, NULL);
		
		if(NULL != new)
			g_free(string);
		else
			new = string;
	} else {	
		return g_strdup("");
	}

	return new;
}

gchar * convertToHTML(gchar * string) { return convertCharSet("UTF-8", "HTML", string); }

/* the code of this function was taken from a GTK tutorial */
static gchar* convert(unsigned char *in, gchar *encoding)
{
	unsigned char *out;
        int ret,size,out_size,temp;
        xmlCharEncodingHandlerPtr handler;

	if(NULL == in)
		return g_strdup("");
	
        size = (int)strlen(in)+1; 
        out_size = size*2-1; 
        out = g_malloc((size_t)out_size); 

        if (out) {
                handler = xmlFindCharEncodingHandler(encoding);
                
                if (!handler) {
                        g_free(out);
                        out = NULL;
                }
        }
        if (out) {
                temp=size-1;
                ret = handler->input(out, &out_size, in, &temp);
                if (ret || temp-size+1) {
                        if (ret) {
                                g_print(_("conversion wasn't successful.\n"));
                        } else {
                                g_print(_("conversion wasn't successful. converted: %i octets.\n"), temp);
                        }
                        g_free(out);
                        out = NULL;
                } else {
                        out = g_realloc(out,out_size+1); 
                        out[out_size]=0; /*null terminating out*/
                        
                }
        } else {
                g_error(_("not enough memory\n"));
        }
	
	if(NULL == out)
		out = g_strdup("");
		
        return out;
}

/* Conversion function which should be applied to all read XML strings, 
   to ensure proper UTF8. doc points to the xml document and its encoding and
   string is a xmlchar pointer to the read string. The result gchar
   string is returned, the original XML string is freed. */
gchar * CONVERT(xmlChar *string) {

	convert(string, "UTF-8");
}

gchar * parseHTML(htmlDocPtr doc, htmlNodePtr cur, gchar *string) {
	gchar	*newstring = NULL;
	gchar	*oldstring = NULL;
	
	if((NULL == cur) || (NULL == doc)) {
		g_warning(_("internal error: XML document pointer NULL! This should not happen!\n"));
		return;
	}
	
	cur = cur->xmlChildrenNode;
	while (cur != NULL) {
		if(cur->content != NULL) {
			// g_print("%s >>>%s<<<\n", cur->name, cur->content);
			oldstring = newstring;
			if(NULL != newstring)
				newstring = g_strdup_printf("%s%s", newstring, cur->content);
			else
				newstring = g_strdup(cur->content);
			g_free(oldstring);
		}
		
		if(cur->xmlChildrenNode != NULL)
			newstring = parseHTML(doc, cur->xmlChildrenNode, newstring);
			
		cur = cur->next;		
	}
	
	return newstring;
}

gchar * extractHTMLNode(xmlNodePtr cur) {
	xmlBufferPtr	buf = NULL;
	gchar		*result = NULL;
	
	buf = xmlBufferCreate();
	
	if(-1 != xmlNodeDump(buf, cur->doc, cur, 0, 0))
		result = xmlCharStrdup(xmlBufferContent(buf));

	xmlBufferFree(buf);
	
	return result;
}

/* converts a UTF-8 strings containing any HTML stuff to proper HTML

   FIXME: still buggy, correctly converts entities and
   preserves encodings, but does loose text inside enclosing
   formatting tags like "<b>Hallo</b>" 
 */
gchar * unhtmlize(gchar *string) {
	htmlParserCtxtPtr	ctxt; 
	xmlDocPtr		pDoc;
	int			length;
	gchar			*newstring = NULL;

	if(NULL == string)
		return NULL;
	
	/* only do something if there are any entities */
	if(NULL == (strchr(string, '&')))
		return string;

	length = strlen(string);
	newstring = (gchar *)g_malloc(length + 1);
	memset(newstring, 0, length + 1);
	
	ctxt = htmlCreatePushParserCtxt(NULL, NULL, newstring, length, 0, (xmlCharEncoding)"UTF-8");
	ctxt->sax->warning = NULL;	/* disable XML errors and warnings */
	ctxt->sax->error = NULL;
	
        htmlParseChunk(ctxt, string, length, 0);
        htmlParseChunk(ctxt, string, 0, 1);
        pDoc = ctxt->myDoc;
	newstring = parseHTML(pDoc, xmlDocGetRootElement(pDoc), "");	
        htmlFreeParserCtxt(ctxt);
	
	g_free(string);
	
	return newstring;
}

/* converts a ISO 8601 time string to a time_t value */
time_t convertDate(char *date) {
	struct tm	tm;
	time_t		t;
	char		*pos;
	
	memset(&tm, 0, sizeof(struct tm));

	/* we expect at least something like "2003-08-07T15:28:19" and
	   don't require the second fractions and the timezone info

	   the most specific format:   YYYY-MM-DDThh:mm:ss.sTZD
	 */
	pos = (char *)strptime((const char *)date, "%Y-%m-%dT%H:%M", &tm);
	if(pos != NULL) {
		if((time_t)(-1) != (t = mktime(&tm))) {
			return t;
		} else {
			g_warning(_("internal error! time conversion error! mktime failed!\n"));
		}
	} else {
		g_print(_("Invalid date format! Ignoring <dc:date> information!\n"));				
	}
	
	return 0;
}

gchar * getActualTime(void) {
	time_t		t;
	gchar		*timestr;
	gchar		*timeformat;
	
	/* get receive time */
	if((time_t)-1 != time(&t)) {
		if(NULL != (timestr = (gchar *)g_malloc(TIMESTRLEN+1))) {
			timeformat = getStringConfValue(TIME_FORMAT);
			
			/* if not set conf.c delivers a "", from version 0.3.8
			   there is no more time format setting and D_T_FMT
			   'll always be used... */
			if(0 == strlen(timeformat)) {
				g_free(timeformat);
				timeformat =  g_strdup_printf("%s %s", nl_langinfo(D_FMT), nl_langinfo(T_FMT));
				
			}
			
			if(NULL != timeformat) {
				strftime(timestr, TIMESTRLEN, (char *)timeformat, gmtime(&t));
				g_free(timeformat);
			}
		}
	}
	
	return timestr;
}

gchar * formatDate(time_t t) {
	gchar		*timestr;
	gchar		*timeformat;
	
	if(NULL != (timestr = (gchar *)g_malloc(TIMESTRLEN+1))) {
		switch(getNumericConfValue(TIME_FORMAT_MODE)) {
			case 1:
				timeformat =  g_strdup_printf("%s", nl_langinfo(T_FMT));	
				break;
			case 3:
				timeformat = getStringConfValue(TIME_FORMAT);				
				break;
			case 2:
			default:
				timeformat =  g_strdup_printf("%s %s", nl_langinfo(D_FMT), nl_langinfo(T_FMT));	
				break;
		}		
		strftime(timestr, TIMESTRLEN, (char *)timeformat, localtime(&t));
		g_free(timeformat);
	}
	
	return timestr;
}

void initCachePath(void) {
	struct stat	statinfo;
	struct passwd	*pwent;

	if(NULL != (pwent = getpwuid(getuid()))) {
		CACHEPATH = g_strdup_printf("%s/.liferea", pwent->pw_dir);
	
		if(0 != stat(CACHEPATH, &statinfo)) {
			if(0 != mkdir(CACHEPATH, S_IRUSR | S_IWUSR | S_IXUSR)) {
				g_error(g_strdup_printf(_("Cannot create cache directory %s!"), CACHEPATH));
			}
		}
		//free(pwent);	crashes???
	} else {
		g_error(g_strerror(errno));
	}
}

gchar * getCachePath(void) {
	
	if(NULL == CACHEPATH)
		initCachePath();
		
	return CACHEPATH;
}

/* returns the extension for the type type */
gchar *getExtension(gint type) {
	gchar	*extension;
	
	switch(type) {
		case FST_VFOLDER:
			extension = VFOLDER_EXTENSION;
			break;
		case FST_OCS:
			extension = OCS_EXTENSION;
			break;
		default:
			extension = NULL;
			break;
	}
	
	return extension;
}

gchar * getCacheFileName(gchar *keyprefix, gchar *key, gchar *extension) {
	gchar	*keypos;
	
	/* build filename */	
	keypos = strrchr(key, '/');
	if(NULL == keypos)
		keypos = key;
	else
		keypos++;
	
	if(NULL != extension)	
		return g_strdup_printf("%s/%s_%s.%s", getCachePath(), keyprefix, keypos, extension);
	else
		return g_strdup_printf("%s/%s_%s", getCachePath(), keyprefix, keypos);
}

static gchar * byte_to_hex(gint nr) {
	gchar *result = NULL;
	
	result = g_strdup_printf("%%%d%d", nr - (nr % 0x10), nr % 0x10);
	return result;
}

/* Encodes any UTF-8 string in uriString and returns a 
   valid UTF-8 encoded HTTP URI. Note that the uriString will be freed. */
gchar * encodeURIString(gchar *uriString) {
	gchar		*newURIString;
	gchar		*tmp, *hex;
	int		i, j, len;

	newURIString = g_strdup("");
	len = strlen(uriString);
	for(i = 0; i < len; i++) {
		if((('A' <= uriString[i]) && (uriString[i] <= 'Z')) ||
		   (('a' <= uriString[i]) && (uriString[i] <= 'z')) ||
		   (('0' <= uriString[i]) && (uriString[i] <= '9')) ||
		   (uriString[i] == '-') || 
		   (uriString[i] == '_') ||
		   (uriString[i] == '.') || 
		   (uriString[i] == '?') || 
		   (uriString[i] == '!') ||
		   (uriString[i] == '~') ||
		   (uriString[i] == '*') ||
		   (uriString[i] == '\'') ||
		   (uriString[i] == '(') ||
		   (uriString[i] == ')'))
		   	tmp = g_strdup_printf("%s%c", newURIString, uriString[i]);
		else if(uriString[i] == ' ')
			tmp = g_strdup_printf("%s%c", newURIString, '+');
		else if((unsigned char)uriString[i] <= 127) {
			tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i]));g_free(hex);
		} else if(((unsigned char)uriString[i] >= 192) && ((unsigned char)uriString[i] <= 223)) {	// two bytes
			if((i + 1) > len) {
				g_warning(_("Unexpected end of character sequence or corrupt UTF-8 encoding! Some characters were dropped!"));
				break;
			}
			tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i++]));g_free(hex);g_free(newURIString);newURIString = tmp;
			tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i]));g_free(hex);
		} else if(((unsigned char)uriString[i] > 223) && ((unsigned char)uriString[i] <= 239)) {	// three bytes
			if((i + 2) > len) {
				g_warning(_("Unexpected end of character sequence or corrupt UTF-8 encoding! Some characters were dropped!"));
				break;
			}
			for(j=0; j < 2; j++) {
				tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i++]));g_free(hex);g_free(newURIString);newURIString = tmp;
			}
			tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i]));g_free(hex);
		} else if(((unsigned char)uriString[i] > 239) && ((unsigned char)uriString[i] <= 247)) {	// four bytes
			if((i + 3) > len) {
				g_warning(_("Unexpected end of character sequence or corrupt UTF-8 encoding! Some characters were dropped!"));
				break;
			}
			for(j=0; j < 3; j++) {
				tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i++]));g_free(hex);g_free(newURIString);newURIString = tmp;
			}
			tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i]));g_free(hex);
		} else if(((unsigned char)uriString[i] > 247) && ((unsigned char)uriString[i] <= 251)) {	// five bytes
			if((i + 4) > len) {
				g_warning(_("Unexpected end of character sequence or corrupt UTF-8 encoding! Some characters were dropped!"));
				break;
			}
			for(j=0; j < 4; j++) {
				tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i++]));g_free(hex);g_free(newURIString);newURIString = tmp;
			}			
			tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i]));g_free(hex);
		} else if(((unsigned char)uriString[i] > 247) && ((unsigned char)uriString[i] <= 251)) {	// guess how many!
			if((i + 5) > len) {
				g_warning(_("Unexpected end of character sequence or corrupt UTF-8 encoding! Some characters were dropped!"));
				break;
			}
			for(j=0; j < 5; j++) {
				tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i++]));g_free(hex);g_free(newURIString);newURIString = tmp;
			}
			tmp = g_strdup_printf(newURIString, hex = byte_to_hex(uriString[i]));g_free(hex);
		}
		g_free(newURIString); 
		newURIString = tmp;
	}
	g_free(uriString);

	return newURIString;
}
