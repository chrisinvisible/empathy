/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * Authors: Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 *          Jonathan Tellier <jonathan.tellier@gmail.com>
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <unique/unique.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/util.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-connection-managers.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include "empathy-accounts.h"
#include "empathy-accounts-common.h"
#include "empathy-accounts-dialog.h"
#include "empathy-account-assistant.h"
#include "empathy-import-mc4-accounts.h"
#include "empathy-auto-salut-account-helper.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

#define EMPATHY_ACCOUNTS_DBUS_NAME "org.gnome.EmpathyAccounts"

static gboolean only_if_needed = FALSE;
static gboolean hidden = FALSE;
static gchar *selected_account_name = NULL;

static void
account_prepare_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (user_data);
  TpAccount *account = TP_ACCOUNT (source_object);
  GError *error = NULL;

  if (!tp_account_prepare_finish (account, result, &error))
    {
      DEBUG ("Failed to prepare account: %s", error->message);
      g_error_free (error);

      account = NULL;
    }

  empathy_accounts_show_accounts_ui (manager, account,
      G_CALLBACK (gtk_main_quit));
}

static void
maybe_show_accounts_ui (TpAccountManager *manager)
{
  if (hidden ||
      (only_if_needed && empathy_accounts_has_non_salut_accounts (manager)))
    gtk_main_quit ();
  else
    empathy_accounts_show_accounts_ui (manager, NULL, gtk_main_quit);
}

static void
cm_manager_prepared_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  if (!empathy_connection_managers_prepare_finish (
      EMPATHY_CONNECTION_MANAGERS (source), result, NULL))
    {
      g_warning ("Failed to prepare connection managers singleton");
      gtk_main_quit ();
      return;
    }

  empathy_accounts_import (TP_ACCOUNT_MANAGER (user_data),
    EMPATHY_CONNECTION_MANAGERS (source));

  maybe_show_accounts_ui (TP_ACCOUNT_MANAGER (user_data));
}

static void
account_manager_ready_for_accounts_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
  const gchar *account_id = (const gchar*) user_data;
  GError *error = NULL;

  if (!tp_account_manager_prepare_finish (manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_clear_error (&error);
      return;
    }

  if (account_id != NULL)
    {
      gchar *account_path;
      TpAccount *account = NULL;
      TpDBusDaemon *bus;

      /* create and prep the corresponding TpAccount so it's fully ready by the
       * time we try to select it in the accounts dialog */
      account_path = g_strdup_printf ("%s%s", TP_ACCOUNT_OBJECT_PATH_BASE,
          account_id);
      bus = tp_dbus_daemon_dup (NULL);
      if ((account = tp_account_new (bus, account_path, &error)))
        {
          tp_account_prepare_async (account, NULL, account_prepare_cb, manager);
          return;
        }
      else
        {
          DEBUG ("Failed to find account with path %s: %s", account_path,
              error->message);
          g_clear_error (&error);
        }

      g_object_unref (bus);
      g_free (account_path);
    }
  else
    {
      if (empathy_import_mc4_has_imported ())
        {
          maybe_show_accounts_ui (manager);
        }
      else
        {
          EmpathyConnectionManagers *cm_mgr =
            empathy_connection_managers_dup_singleton ();

          empathy_connection_managers_prepare_async (
            cm_mgr, cm_manager_prepared_cb, manager);
        }
    }
}

static UniqueResponse
unique_app_message_cb (UniqueApp *unique_app,
    gint command,
    UniqueMessageData *data,
    guint timestamp,
    gpointer user_data)
{
  DEBUG ("Other instance launched, presenting the main window. "
      "Command=%d, timestamp %u", command, timestamp);

  if (command == UNIQUE_ACTIVATE)
    {
      TpAccountManager *account_manager;

      account_manager = tp_account_manager_dup ();

      empathy_accounts_show_accounts_ui (account_manager, NULL,
          G_CALLBACK (gtk_main_quit));

      g_object_unref (account_manager);
    }
  else
    {
      g_warning (G_STRLOC "unhandled unique app command %d", command);

      return UNIQUE_RESPONSE_PASSTHROUGH;
    }

  return UNIQUE_RESPONSE_OK;
}

#define COMMAND_ACCOUNTS_DIALOG 1

int
main (int argc, char *argv[])
{
  TpAccountManager *account_manager;
  GError *error = NULL;
  UniqueApp *unique_app;

  GOptionContext *optcontext;
  GOptionEntry options[] = {
      { "hidden", 'h',
        0, G_OPTION_ARG_NONE, &hidden,
        N_("Don't display any dialogs; do any work (eg, importing) and exit"),
        NULL },
      { "if-needed", 'n',
        0, G_OPTION_ARG_NONE, &only_if_needed,
        N_("Don't display any dialogs if there are any non-salut accounts"),
        NULL },
      { "select-account", 's',
        G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_STRING, &selected_account_name,
        N_("Initially select given account (eg, "
            "gabble/jabber/foo_40example_2eorg0)"),
        N_("<account-id>") },

      { NULL }
  };

  g_thread_init (NULL);
  empathy_init ();

  optcontext = g_option_context_new (N_("- Empathy Accounts"));
  g_option_context_add_group (optcontext, gtk_get_option_group (TRUE));
  g_option_context_add_main_entries (optcontext, options, GETTEXT_PACKAGE);

  if (!g_option_context_parse (optcontext, &argc, &argv, &error))
    {
      g_print ("%s\nRun '%s --help' to see a full list of available command line options.\n",
          error->message, argv[0]);
      g_warning ("Error in empathy init: %s", error->message);
      return EXIT_FAILURE;
    }

  g_option_context_free (optcontext);

  empathy_gtk_init ();

  g_set_application_name (_("Empathy Accounts"));

  gtk_window_set_default_icon_name ("empathy");
  textdomain (GETTEXT_PACKAGE);

  unique_app = unique_app_new (EMPATHY_ACCOUNTS_DBUS_NAME, NULL);

  if (unique_app_is_running (unique_app))
    {
      unique_app_send_message (unique_app, UNIQUE_ACTIVATE, NULL);

      g_object_unref (unique_app);
      return EXIT_SUCCESS;
    }

  account_manager = tp_account_manager_dup ();

  tp_account_manager_prepare_async (account_manager, NULL,
    account_manager_ready_for_accounts_cb, selected_account_name);

  g_signal_connect (unique_app, "message-received",
      G_CALLBACK (unique_app_message_cb), NULL);

  gtk_main ();

  g_object_unref (account_manager);
  g_object_unref (unique_app);

  return EXIT_SUCCESS;
}
