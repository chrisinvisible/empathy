/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gi18n-lib.h>

#include <gconf/gconf-client.h>

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

#include "cc-empathy-accounts-panel.h"
#include "cc-empathy-accounts-page.h"

#define CC_EMPATHY_ACCOUNTS_PANEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_EMPATHY_ACCOUNTS_PANEL, CcEmpathyAccountsPanelPrivate))

struct CcEmpathyAccountsPanelPrivate
{
  CcPage *empathy_accounts_page;
};

G_DEFINE_DYNAMIC_TYPE (CcEmpathyAccountsPanel, cc_empathy_accounts_panel, CC_TYPE_PANEL)

static void
setup_panel (CcEmpathyAccountsPanel *panel)
{
  panel->priv->empathy_accounts_page = cc_empathy_accounts_page_new ();

  gtk_container_add (GTK_CONTAINER (panel),
      GTK_WIDGET (panel->priv->empathy_accounts_page));

  gtk_widget_show (GTK_WIDGET (panel->priv->empathy_accounts_page));

  g_object_set (panel,
      "current-page", panel->priv->empathy_accounts_page,
      NULL);
}

static void
cc_empathy_accounts_panel_active_changed (CcPanel *self,
    gboolean is_active)
{
  DEBUG ("%s: active = %i", G_STRLOC, is_active);

  if (!is_active)
    {
      /* why doesn't control-center call active-changed on the Page? */
      cc_empathy_accounts_page_destroy_dialogs (
          CC_EMPATHY_ACCOUNTS_PAGE (
            CC_EMPATHY_ACCOUNTS_PANEL (self)->priv->empathy_accounts_page));
    }

  CC_PANEL_CLASS (cc_empathy_accounts_panel_parent_class)->active_changed (
      self, is_active);
}

static GObject *
cc_empathy_accounts_panel_constructor (GType type,
    guint n_construct_properties,
    GObjectConstructParam *construct_properties)
{
  CcEmpathyAccountsPanel *empathy_accounts_panel;

  empathy_accounts_panel = CC_EMPATHY_ACCOUNTS_PANEL (
      G_OBJECT_CLASS (cc_empathy_accounts_panel_parent_class)->constructor (
          type, n_construct_properties, construct_properties));

  g_object_set (empathy_accounts_panel,
      "display-name", _("Messaging and VoIP Accounts"),
      "id", "empathy-accounts.desktop",
      NULL);

  setup_panel (empathy_accounts_panel);

  return G_OBJECT (empathy_accounts_panel);
}

static void
cc_empathy_accounts_panel_finalize (GObject *object)
{
  CcEmpathyAccountsPanel *empathy_accounts_panel;

  g_return_if_fail (object != NULL);
  g_return_if_fail (CC_IS_EMPATHY_ACCOUNTS_PANEL (object));

  empathy_accounts_panel = CC_EMPATHY_ACCOUNTS_PANEL (object);

  g_return_if_fail (empathy_accounts_panel->priv != NULL);

  g_object_unref (empathy_accounts_panel->priv->empathy_accounts_page);

  G_OBJECT_CLASS (cc_empathy_accounts_panel_parent_class)->finalize (object);
}

static void
cc_empathy_accounts_panel_class_init (CcEmpathyAccountsPanelClass *klass)
{
  CcPanelClass *panel_class = CC_PANEL_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  panel_class->active_changed = cc_empathy_accounts_panel_active_changed;

  object_class->constructor = cc_empathy_accounts_panel_constructor;
  object_class->finalize = cc_empathy_accounts_panel_finalize;

  g_type_class_add_private (klass, sizeof (CcEmpathyAccountsPanelPrivate));
}

static void
cc_empathy_accounts_panel_class_finalize (CcEmpathyAccountsPanelClass *klass)
{
}

static void
cc_empathy_accounts_panel_init (CcEmpathyAccountsPanel *panel)
{
  GConfClient *client;

  panel->priv = CC_EMPATHY_ACCOUNTS_PANEL_GET_PRIVATE (panel);

  client = gconf_client_get_default ();
  gconf_client_add_dir (client, "/desktop/gnome/peripherals/empathy_accounts",
      GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
  gconf_client_add_dir (client, "/desktop/gnome/interface",
      GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
  g_object_unref (client);
}

void
cc_empathy_accounts_panel_register (GIOModule *module)
{
  cc_empathy_accounts_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_PANEL_EXTENSION_POINT_NAME,
      CC_TYPE_EMPATHY_ACCOUNTS_PANEL, "empathy-accounts", 10);
}
