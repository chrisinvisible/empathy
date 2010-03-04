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

  return !salut_created;
}

EmpathyAccountSettings *
create_salut_account_settings (void)
{
  EmpathyAccountSettings  *settings;
  EBook *book;
  EContact *contact;
  gchar *nickname = NULL;
  gchar *first_name = NULL;
  gchar *last_name = NULL;
  gchar *email = NULL;
  gchar *jid = NULL;
  GError *error = NULL;

  settings = empathy_account_settings_new ("salut", "local-xmpp",
      _("People nearby"));

  /* Get self EContact from EDS */
  if (!e_book_get_self (&contact, &book, &error))
    {
      DEBUG ("Failed to get self econtact: %s", error->message);
      g_error_free (error);
      return settings;
    }

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

  g_free (nickname);
  g_free (first_name);
  g_free (last_name);
  g_free (email);
  g_free (jid);
  g_object_unref (contact);
  g_object_unref (book);

  return settings;
}
