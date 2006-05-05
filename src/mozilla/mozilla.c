/**
 * @file mozilla.c a Mozilla/Firefox browser module implementation
 *   
 * Copyright (C) 2003-2006 Lars Lindner <lars.lindner@gmx.net>   
 * Copyright (C) 2004-2006 Nathan J. Conrad <t98502@users.sourceforge.net>
 *
 * Contains code from the Galeon sources
 *
 * Copyright (C) 2000 Marco Pesenti Gritti
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

#include <gtkmozembed.h>
#include "ui/ui_htmlview.h"
#include "mozembed.h"

static void mozilla_init(void) {

	/* Avoid influencing the component loading by $MOZILLA_FIVE_HOME */
	g_unsetenv("MOZILLA_FIVE_HOME");

	
	/* It is important to call gtk_moz_embed_set_comp_path()
	   with NULL. If we would set a hard coded component
	   path here we would run into symbol incompatibilities
	   when LD_LIBRARY_PATH points Liferea to gtkmozembed.so
	   different from the one we compiled against */
	gtk_moz_embed_set_comp_path(NULL);

	mozembed_init();	
}

static htmlviewPluginInfo mozillaInfo = {
	.api_version	= HTMLVIEW_API_VERSION,
	.name		= "Mozilla",
	.init		= mozilla_init,
	.deinit		= mozembed_deinit,
	.create		= mozembed_create,
	.write		= mozembed_write,
	.launch		= mozembed_launch_url,
	.launchInsidePossible = mozembed_launch_inside_possible,
	.zoomLevelGet	= mozsupport_get_zoom,
	.zoomLevelSet	= mozsupport_set_zoom,
	.scrollPagedown	= mozsupport_scroll_pagedown,
	.setProxy	= mozembed_set_proxy,
	.setOffLine	= mozsupport_set_offline_mode
};

DECLARE_HTMLVIEW_PLUGIN(mozillaInfo);

