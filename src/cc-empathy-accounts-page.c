/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (C) 2010 Collabora Ltd.
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

#include <telepathy-glib/account-manager.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-connection-managers.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

#include "cc-empathy-accounts-page.h"
#include "empathy-accounts-common.h"
#include "empathy-account-assistant.h"
#include "empathy-accounts-dialog.h"

#define CC_EMPATHY_ACCOUNTS_PAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_EMPATHY_ACCOUNTS_PAGE, CcEmpathyAccountsPagePrivate))

struct CcEmpathyAccountsPagePrivate
{
  /* the original window holding the dialog content; it needs to be retained and
   * destroyed in our finalize(), since it invalidates its children (even if
   * they've already been reparented by the time it is destroyed) */
  GtkWidget *accounts_window;

  GtkWidget *assistant;
};

G_DEFINE_TYPE (CcEmpathyAccountsPage, cc_empathy_accounts_page, CC_TYPE_PAGE)

static void
page_pack_with_accounts_dialog (CcEmpathyAccountsPage *page)
{
  GtkWidget *content;
  GtkWidget *action_area;

  if (page->priv->accounts_window != NULL)
    {
      gtk_widget_destroy (page->priv->accounts_window);
      gtk_container_remove (GTK_CONTAINER (page),
          gtk_bin_get_child (GTK_BIN (page)));
    }

    page->priv->accounts_window = empathy_accounts_dialog_show (NULL, NULL);
    gtk_widget_hide (page->priv->accounts_window);

    content = gtk_dialog_get_content_area (
        GTK_DIALOG (page->priv->accounts_window));
    action_area = gtk_dialog_get_action_area (
        GTK_DIALOG (page->priv->accounts_window));
    gtk_widget_set_no_show_all (action_area, TRUE);
    gtk_widget_hide (action_area);

    gtk_widget_reparent (content, GTK_WIDGET (page));
}

static void
account_assistant_closed_cb (GtkWidget *widget,
    gpointer user_data)
{
  CcEmpathyAccountsPage *page = CC_EMPATHY_ACCOUNTS_PAGE (user_data);

  if (empathy_accounts_dialog_is_creating (
      EMPATHY_ACCOUNTS_DIALOG (page->priv->accounts_window)))
    {
      empathy_account_dialog_cancel (
        EMPATHY_ACCOUNTS_DIALOG (page->priv->accounts_window));
    }

  gtk_widget_set_sensitive (GTK_WIDGET (page), TRUE);
  page->priv->assistant = NULL;
}

static void
connection_managers_prepare (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyConnectionManagers *cm_mgr = EMPATHY_CONNECTION_MANAGERS (source);
  TpAccountManager *account_mgr;
  CcEmpathyAccountsPage *page;

  account_mgr = TP_ACCOUNT_MANAGER (g_object_get_data (G_OBJECT (cm_mgr),
      "account-manager"));
  page = CC_EMPATHY_ACCOUNTS_PAGE (g_object_get_data (G_OBJECT (cm_mgr),
        "page"));

  if (!empathy_connection_managers_prepare_finish (cm_mgr, result, NULL))
    goto out;

  page_pack_with_accounts_dialog (page);

  empathy_accounts_import (account_mgr, cm_mgr);

  if (!empathy_accounts_has_non_salut_accounts (account_mgr))
    {
      GtkWindow *parent;

      parent = empathy_get_toplevel_window (GTK_WIDGET (page));
      page->priv->assistant = empathy_account_assistant_show (parent, cm_mgr);

      gtk_widget_set_sensitive (GTK_WIDGET (page), FALSE);

      empathy_signal_connect_weak (page->priv->assistant, "hide",
        G_CALLBACK (account_assistant_closed_cb),
        G_OBJECT (page));
    }

out:
  /* remove ref from active_changed() */
  g_object_unref (account_mgr);
  g_object_unref (cm_mgr);
}

static void
account_manager_ready_for_accounts_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *account_mgr = TP_ACCOUNT_MANAGER (source_object);
  CcEmpathyAccountsPage *page = CC_EMPATHY_ACCOUNTS_PAGE (user_data);
  GError *error = NULL;

  if (!tp_account_manager_prepare_finish (account_mgr, result, &error))
    {
      g_warning ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  if (empathy_accounts_has_non_salut_accounts (account_mgr))
    {
      page_pack_with_accounts_dialog (page);

      /* remove ref from active_changed() */
      g_object_unref (account_mgr);
    }
  else
    {
      EmpathyConnectionManagers *cm_mgr;

      cm_mgr = empathy_connection_managers_dup_singleton ();

      g_object_set_data_full (G_OBJECT (cm_mgr), "account-manager",
          g_object_ref (account_mgr), (GDestroyNotify) g_object_unref);
      g_object_set_data_full (G_OBJECT (cm_mgr), "page",
          g_object_ref (page), (GDestroyNotify) g_object_unref);

      empathy_connection_managers_prepare_async (cm_mgr,
          connection_managers_prepare, page);
    }
}

static void
active_changed (CcPage *base_page,
    gboolean is_active)
{
  CcEmpathyAccountsPage *page = CC_EMPATHY_ACCOUNTS_PAGE (base_page);
  TpAccountManager *account_manager;

  DEBUG ("%s: active = %i", G_STRLOC, is_active);

  if (is_active)
    {
      /* unref'd in final endpoint callbacks */
      account_manager = tp_account_manager_dup ();

      tp_account_manager_prepare_async (account_manager, NULL,
          account_manager_ready_for_accounts_cb, page);
    }
}

static void
cc_empathy_accounts_page_finalize (GObject *object)
{
  CcEmpathyAccountsPage *page;

  g_return_if_fail (object != NULL);
  g_return_if_fail (CC_IS_EMPATHY_ACCOUNTS_PAGE (object));

  page = CC_EMPATHY_ACCOUNTS_PAGE (object);

  g_return_if_fail (page->priv != NULL);

  gtk_widget_destroy (page->priv->accounts_window);

  G_OBJECT_CLASS (cc_empathy_accounts_page_parent_class)->finalize (object);
}

static void
cc_empathy_accounts_page_class_init (CcEmpathyAccountsPageClass *klass)
{
  GObjectClass  *object_class = G_OBJECT_CLASS (klass);
  CcPageClass   *page_class = CC_PAGE_CLASS (klass);

  object_class->finalize = cc_empathy_accounts_page_finalize;

  page_class->active_changed = active_changed;

  g_type_class_add_private (klass, sizeof (CcEmpathyAccountsPagePrivate));
}

static void
cc_empathy_accounts_page_init (CcEmpathyAccountsPage *page)
{
  page->priv = CC_EMPATHY_ACCOUNTS_PAGE_GET_PRIVATE (page);

  empathy_gtk_init ();
}

CcPage *
cc_empathy_accounts_page_new (void)
{
  GObject *object;

  object = g_object_new (CC_TYPE_EMPATHY_ACCOUNTS_PAGE,
      "display-name", _("Messaging and VoIP Accounts"),
      "id", "general",
      NULL);

  return CC_PAGE (object);
}

void
cc_empathy_accounts_page_destroy_dialogs (CcEmpathyAccountsPage *self)
{
  /* This function is really kludgey, it is called by the AccountPanel to
   * remove any child dialogs (i.e. this assistant). I personally feel this
   * would be better in active_changed, but the Page doesn't seem to receive
   * that signal when the panel does. */

  if (self->priv->assistant != NULL)
    {
      DEBUG ("Destroying assistant");
      gtk_widget_destroy (self->priv->assistant);
    }
}
