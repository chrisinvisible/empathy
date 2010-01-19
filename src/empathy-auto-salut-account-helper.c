/*
 * Copyright (C) 2007-2010 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#include <glib.h>
#include <glib/gi18n.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>
#include <libebook/e-book.h>

#include <libempathy/empathy-account-settings.h>
#include <libempathy-gtk/empathy-conf.h>

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

#include "empathy-auto-salut-account-helper.h"

/* Salut account creation. The TpAccountManager first argument
 * must already be prepared when calling this function. */
gboolean
should_create_salut_account (TpAccountManager *manager)
{
  gboolean salut_created = FALSE;
  GList *accounts, *l;

  /* Check if we already created a salut account */
  empathy_conf_get_bool (empathy_conf_get (),
      EMPATHY_PREFS_SALUT_ACCOUNT_CREATED,
      &salut_created);

  if (salut_created)
    {
      DEBUG ("Gconf says we already created a salut account once");
      return FALSE;
    }

  accounts = tp_account_manager_get_valid_accounts (manager);

  for (l = accounts; l != NULL;  l = g_list_next (l))
    {
      TpAccount *account = TP_ACCOUNT (l->data);

      if (!tp_strdiff (tp_account_get_protocol (account), "local-xmpp"))
        {
          salut_created = TRUE;
          break;
        }
    }

  g_list_free (accounts);

  if (salut_created)
    {
      DEBUG ("Existing salut account already exists, flagging so in gconf");
      empathy_conf_set_bool (empathy_conf_get (),
          EMPATHY_PREFS_SALUT_ACCOUNT_CREATED,
          TRUE);
    }

  return !salut_created;
}

static void
salut_account_created (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyAccountSettings *settings = EMPATHY_ACCOUNT_SETTINGS (source);
  TpAccount *account;
  GError *error = NULL;

  if (!empathy_account_settings_apply_finish (settings, result, &error))
    {
      DEBUG ("Failed to create salut account: %s", error->message);
      g_error_free (error);
      return;
    }

  account = empathy_account_settings_get_account (settings);

  tp_account_set_enabled_async (account, TRUE, NULL, NULL);
  empathy_conf_set_bool (empathy_conf_get (),
      EMPATHY_PREFS_SALUT_ACCOUNT_CREATED,
      TRUE);
}

static void
create_salut_account_am_ready_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
  EmpathyConnectionManagers *managers = user_data;
  EmpathyAccountSettings  *settings;
  TpConnectionManager *manager;
  const TpConnectionManagerProtocol *protocol;
  EBook      *book;
  EContact   *contact;
  gchar      *nickname = NULL;
  gchar      *first_name = NULL;
  gchar      *last_name = NULL;
  gchar      *email = NULL;
  gchar      *jid = NULL;
  GError     *error = NULL;

  if (!tp_account_manager_prepare_finish (account_manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      goto out;
    }

  if (!should_create_salut_account (account_manager))
    goto out;

  manager = empathy_connection_managers_get_cm (managers, "salut");
  if (manager == NULL)
    {
      DEBUG ("Salut not installed, not making a salut account");
      goto out;
    }

  protocol = tp_connection_manager_get_protocol (manager, "local-xmpp");
  if (protocol == NULL)
    {
      DEBUG ("Salut doesn't support local-xmpp!!");
      goto out;
    }

  DEBUG ("Trying to add a salut account...");

  /* Get self EContact from EDS */
  if (!e_book_get_self (&contact, &book, &error))
    {
      DEBUG ("Failed to get self econtact: %s",
          error ? error->message : "No error given");
      g_clear_error (&error);
      goto out;
    }

  settings = empathy_account_settings_new ("salut", "local-xmpp",
      _("People nearby"));

  nickname = e_contact_get (contact, E_CONTACT_NICKNAME);
  first_name = e_contact_get (contact, E_CONTACT_GIVEN_NAME);
  last_name = e_contact_get (contact, E_CONTACT_FAMILY_NAME);
  email = e_contact_get (contact, E_CONTACT_EMAIL_1);
  jid = e_contact_get (contact, E_CONTACT_IM_JABBER_HOME_1);

  if (!tp_strdiff (nickname, "nickname"))
    {
      g_free (nickname);
      nickname = NULL;
    }

  DEBUG ("Salut account created:\nnickname=%s\nfirst-name=%s\n"
     "last-name=%s\nemail=%s\njid=%s\n",
     nickname, first_name, last_name, email, jid);

  empathy_account_settings_set_string (settings,
      "nickname", nickname ? nickname : "");
  empathy_account_settings_set_string (settings,
      "first-name", first_name ? first_name : "");
  empathy_account_settings_set_string (settings,
      "last-name", last_name ? last_name : "");
  empathy_account_settings_set_string (settings, "email", email ? email : "");
  empathy_account_settings_set_string (settings, "jid", jid ? jid : "");

  empathy_account_settings_apply_async (settings,
      salut_account_created, NULL);

  g_free (nickname);
  g_free (first_name);
  g_free (last_name);
  g_free (email);
  g_free (jid);
  g_object_unref (settings);
  g_object_unref (contact);
  g_object_unref (book);

 out:
  g_object_unref (managers);
}

static void
create_salut_account_cms_ready_cb (EmpathyConnectionManagers *managers)
{
  TpAccountManager *manager;

  manager = tp_account_manager_dup ();

  tp_account_manager_prepare_async (manager, NULL,
      create_salut_account_am_ready_cb, managers);

  g_object_unref (manager);
}

void
create_salut_account_if_needed (void)
{
  EmpathyConnectionManagers *managers;

  managers = empathy_connection_managers_dup_singleton ();

  if (empathy_connection_managers_is_ready (managers))
    {
      create_salut_account_cms_ready_cb (managers);
    }
  else
    {
      g_signal_connect (managers, "notify::ready",
            G_CALLBACK (create_salut_account_cms_ready_cb), NULL);
    }
}
