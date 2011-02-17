/**
 * @file rule_editor.c  rule editing dialog functionality
 *
 * Copyright (C) 2008-2010 Lars Lindner <lars.lindner@gmail.com>
 * Copyright (C) 2009 Hubert Figuiere <hub@figuiere.net>
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
 
#include "ui/rule_editor.h"
#include "ui/ui_common.h"

#include "rule.h"

static void rule_editor_class_init	(RuleEditorClass *klass);
static void rule_editor_init		(RuleEditor *ld);

#define RULE_EDITOR_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), RULE_EDITOR_TYPE, RuleEditorPrivate))

struct RuleEditorPrivate {
	GtkWidget	*root;		/**< root widget */
	vfolderPtr	vfolder;	/**< the search folder being edited (FIXME: why do we need this?) */
	GSList		*newRules;	/**< new list of rules currently in editing */
};

struct changeRequest {
	GtkWidget	*hbox;		/**< used for remove button (optional) */
	RuleEditor	*editor;	/**< the rule editor */ 
	gint		rule;		/**< used for rule type change (optional) */
	GtkWidget	*paramHBox;	/**< used for rule type change (optional) */
};

static GObjectClass *parent_class = NULL;

GType
rule_editor_get_type (void) 
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) 
	{
		static const GTypeInfo our_info = 
		{
			sizeof (RuleEditorClass),
			NULL, /* base_init */
			NULL, /* base_finalize */
			(GClassInitFunc) rule_editor_class_init,
			NULL,
			NULL, /* class_data */
			sizeof (RuleEditor),
			0, /* n_preallocs */
			(GInstanceInitFunc) rule_editor_init
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "RuleEditor",
					       &our_info, 0);
	}

	return type;
}

static void
rule_editor_finalize (GObject *object)
{
	RuleEditor *re = RULE_EDITOR (object);
	
	/* delete rules */	
	GSList *iter = re->priv->newRules;
	while (iter) {
		rule_free ((rulePtr)iter->data);
		iter = g_slist_next (iter);
	}
	g_slist_free (re->priv->newRules);
	re->priv->newRules = NULL;

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rule_editor_class_init (RuleEditorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rule_editor_finalize;

	g_type_class_add_private (object_class, sizeof(RuleEditorPrivate));
}

static void
rule_editor_init (RuleEditor *re)
{
	re->priv = RULE_EDITOR_GET_PRIVATE (re);
}

static void
rule_editor_destroy_param_widget (GtkWidget *widget, gpointer data)
{	
	gtk_widget_destroy(widget);
}

static void
on_rulevalue_changed (GtkEditable *editable, gpointer user_data)
{
	rulePtr	rule = (rulePtr)user_data;
	
	if (rule->value)
		g_free (rule->value);
	rule->value = g_strdup (gtk_editable_get_chars (editable,0,-1));
}

/* callback for rule additive option menu */

static void
on_rule_changed_additive (GtkComboBox *optionmenu, gpointer user_data)
{
	rulePtr rule = (rulePtr)user_data;
	gint active = gtk_combo_box_get_active (optionmenu);

	rule->additive = ((active==0) ? TRUE : FALSE);
}


/* sets up the widgets for a single rule */
static void
rule_editor_setup_widgets (struct changeRequest *changeRequest, rulePtr rule)
{
	GtkWidget	*widget;
	ruleInfoPtr	ruleInfo;

	ruleInfo = g_slist_nth_data (rule_get_available_rules (), changeRequest->rule);
	g_object_set_data (G_OBJECT (changeRequest->paramHBox), "rule", rule);
			
	/* remove of old widgets */
	gtk_container_foreach (GTK_CONTAINER (changeRequest->paramHBox), rule_editor_destroy_param_widget, NULL);

	/* add popup menu for selection of positive or negative logic */

	widget = gtk_combo_box_text_new ();
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (widget), ruleInfo->positive);
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (widget), ruleInfo->negative);
	gtk_combo_box_set_active ((GtkComboBox*)widget, (rule->additive)?0:1);
	g_signal_connect (G_OBJECT (widget), "changed", G_CALLBACK (on_rule_changed_additive), rule);
	gtk_widget_show_all (widget);
	gtk_box_pack_start (GTK_BOX (changeRequest->paramHBox), widget, FALSE, FALSE, 0);
		
	/* add new value entry if needed */
	if (ruleInfo->needsParameter) {
		widget = gtk_entry_new ();
		gtk_entry_set_text (GTK_ENTRY (widget), rule->value);
		gtk_widget_show (widget);
		g_signal_connect (G_OBJECT (widget), "changed", G_CALLBACK(on_rulevalue_changed), rule);
		gtk_box_pack_start (GTK_BOX (changeRequest->paramHBox), widget, FALSE, FALSE, 0);
	} else {
		/* nothing needs to be added */
	}
}


static void
do_ruletype_changed (struct changeRequest	*changeRequest)
{
	ruleInfoPtr		ruleInfo;
	rulePtr			rule;

	rule = g_object_get_data (G_OBJECT (changeRequest->paramHBox), "rule");
	if (rule) {
		changeRequest->editor->priv->newRules = g_slist_remove (changeRequest->editor->priv->newRules, rule);
		rule_free (rule);
	}
	ruleInfo = g_slist_nth_data (rule_get_available_rules (), changeRequest->rule);
	rule = rule_new (ruleInfo->ruleId, "", TRUE);
	changeRequest->editor->priv->newRules = g_slist_append (changeRequest->editor->priv->newRules, rule);
	
	rule_editor_setup_widgets (changeRequest, rule);
}

/* callback for rule type option menu */
static void
on_ruletype_changed (GtkComboBox *optionmenu, gpointer user_data)
{
	struct changeRequest	*changeRequest = NULL;
	GtkTreeIter iter;
	
	if (gtk_combo_box_get_active_iter (optionmenu, &iter)) {
		gtk_tree_model_get (gtk_combo_box_get_model (optionmenu), &iter, 1, &changeRequest, -1);
		if (changeRequest)
			do_ruletype_changed (changeRequest);
	}
}

/* callback for each rules remove button */
static void
on_ruleremove_clicked (GtkButton *button, gpointer user_data)
{
	struct changeRequest	*changeRequest = (struct changeRequest *)user_data;
	rulePtr			rule;
	
	rule = g_object_get_data (G_OBJECT (changeRequest->paramHBox), "rule");
	if (rule) {
		changeRequest->editor->priv->newRules = g_slist_remove (changeRequest->editor->priv->newRules, rule);
		rule_free(rule);
	}
	gtk_container_remove (GTK_CONTAINER (changeRequest->editor->priv->root), changeRequest->hbox);
	g_free (changeRequest);
}

void
rule_editor_add_rule (RuleEditor *re, rulePtr rule)
{
	GSList			*ruleIter;
	GtkWidget		*hbox, *hbox2, *widget;
	GtkListStore		*store;
	struct changeRequest	*changeRequest, *selected = NULL;
	gint			i = 0, active = 0;

	hbox = gtk_hbox_new (FALSE, 2);	/* hbox to contain all rule widgets */
	hbox2 = gtk_hbox_new (FALSE, 2);	/* another hbox where the rule specific widgets are added */
		
	/* set up the rule type selection popup */
	store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);
	ruleIter = rule_get_available_rules ();
	while (ruleIter) {
		ruleInfoPtr ruleInfo = (ruleInfoPtr)ruleIter->data;
		GtkTreeIter iter;
		
		/* we add a change request to each popup option */
		changeRequest = g_new0 (struct changeRequest, 1);
		changeRequest->paramHBox = hbox2;
		changeRequest->rule = i;
		changeRequest->editor = re;
		
		if (0 == i)
			selected = changeRequest;
		
		/* build the menu option */
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, ruleInfo->title, 1, changeRequest, -1);

		if (rule) {
			if (ruleInfo == rule->ruleInfo) {
				selected = changeRequest;
				active = i;
			}
		}
		
		ruleIter = g_slist_next (ruleIter);
		i++;
	}
	widget = gtk_combo_box_new ();
	ui_common_setup_combo_text (GTK_COMBO_BOX (widget), 0);

	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), GTK_TREE_MODEL (store));
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), active);
	g_signal_connect (G_OBJECT (widget), "changed", G_CALLBACK (on_ruletype_changed), NULL);


	gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), hbox2, FALSE, FALSE, 0);
	
	if (!rule) {
		/* fake a rule type change to initialize parameter widgets */
		do_ruletype_changed (selected);
	} else {
		rulePtr newRule = rule_new (rule->ruleInfo->ruleId, rule->value, rule->additive);

		/* set up widgets with existing rule type and value */
		rule_editor_setup_widgets (selected, newRule);
		
		/* add the rule to the list of new rules */
		re->priv->newRules = g_slist_append (re->priv->newRules, newRule);
	}
	
	/* add remove button */
	changeRequest = g_new0 (struct changeRequest, 1);
	changeRequest->hbox = hbox;
	changeRequest->paramHBox = hbox2;
	changeRequest->editor = re;
	widget = gtk_button_new_from_stock ("gtk-remove");
	gtk_box_pack_end (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (widget), "clicked", G_CALLBACK (on_ruleremove_clicked), changeRequest);

	/* and insert everything in the dialog */
	gtk_widget_show_all (hbox);
	gtk_box_pack_start (GTK_BOX (re->priv->root), hbox, FALSE, TRUE, 0);
}

RuleEditor *
rule_editor_new (vfolderPtr vfolder) 
{
	RuleEditor	*re;
	GSList		*iter;
	
	re = RULE_EDITOR (g_object_new (RULE_EDITOR_TYPE, NULL));
	
	/* Set up rule list vbox */
	re->priv->root = gtk_vbox_new (FALSE, 0);
	re->priv->vfolder = vfolder;	/* FIXME: why do we need this? */
	
	/* load rules into dialog */	
	iter = vfolder->itemset->rules;
	while (iter) {
		rule_editor_add_rule (re, (rulePtr)(iter->data));
		iter = g_slist_next (iter);
	}
	
	gtk_widget_show_all (re->priv->root);

	return re;
}

void
rule_editor_save (RuleEditor *re, vfolderPtr vfolder)
{
	GSList	*iter;
	
	/* delete all old rules */	
	iter = vfolder->itemset->rules;
	while (iter) {
		rule_free ((rulePtr)iter->data);
		iter = g_slist_next (iter);
	}
	g_slist_free (vfolder->itemset->rules);
	vfolder->itemset->rules = NULL;
	
	/* and add all rules from editor */
	iter = re->priv->newRules;
	while (iter) {
		rulePtr rule = (rulePtr)iter->data;
		itemset_add_rule (vfolder->itemset, rule->ruleInfo->ruleId, rule->value, rule->additive);
		iter = g_slist_next (iter);
	}

	vfolder_reset (vfolder);

/* FIXME: move the following code from search_folder_dialog.c and search_dialog.c here:
	vfolder->anyMatch = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (re->priv->anyRuleRadioBtn));	*/
}

GtkWidget *
rule_editor_get_widget (RuleEditor *re)
{
	return re->priv->root;
}
