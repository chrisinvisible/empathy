/*
 * Copyright (C) 2008 Collabora Ltd.
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
 * Authors: Arnaud Maillet <arnaud.maillet@collabora.co.uk>
 */

#include <string.h>
#include <glib.h>
#include <gconf/gconf-client.h>
#include <telepathy-glib/util.h>
#include <libempathy/empathy-account-manager.h>
#include <libempathy/empathy-connection-managers.h>

#include "empathy-import-mc4-accounts.h"

#define DEBUG_FLAG EMPATHY_DEBUG_IMPORT_MC4_ACCOUNTS
#include <libempathy/empathy-debug.h>

#define MC_ACCOUNTS_GCONF_BASE "/apps/telepathy/mc/accounts"
#define IMPORT_MC4_ACCOUNTS "/apps/empathy/accounts/import_mc4_accounts"

typedef struct
{
  gchar *profile;
  gchar *protocol;
} ProfileProtocolMapItem;

static ProfileProtocolMapItem profile_protocol_map[] =
{
  { "aim", "aim" },
  { "ekiga", "sip" },
  { "fwd", "sip" },
  { "gadugadu", "gadugadu" },
  { "groupwise", "groupwise" },
  { "gtalk", "jabber" },
  { "icq", "icq" },
  { "irc", "irc" },
  { "jabber", "jabber" },
  { "msn-haze", "msn" },
  { "msn", "msn" },
  { "qq", "qq" },
  { "salut", "local-xmpp" },
  { "sametime", "sametime" },
  { "sipphone", "sip" },
  { "sofiasip", "sip" },
  { "yahoo", "yahoo" },
};

typedef struct
{
  gchar *connection_manager;
  gchar *protocol;
  gchar *display_name;
  gchar *param_account;
  gchar *param_server;
  gboolean enabled;
  GHashTable *parameters;
} AccountData;

static void
_account_data_free (AccountData *account)
{
  if (account->connection_manager != NULL)
    {
      g_free (account->connection_manager);
      account->connection_manager = NULL;
    }

  if (account->protocol != NULL)
    {
      g_free (account->protocol);
      account->protocol = NULL;
    }

  if (account->display_name != NULL)
    {
      g_free (account->display_name);
      account->display_name = NULL;
    }

  if (account->param_account != NULL)
    {
      g_free (account->param_account);
      account->param_account = NULL;
    }

  if (account->param_server != NULL)
    {
      g_free (account->param_server);
      account->param_server = NULL;
    }

  if (account->parameters != NULL)
    {
      g_hash_table_destroy (account->parameters);
      account->parameters = NULL;
    }

  g_slice_free (AccountData, account);
}

static gchar *
_account_name_from_key (const gchar *key)
{
  guint base_len = strlen (MC_ACCOUNTS_GCONF_BASE);
  const gchar *base, *slash;

  g_assert (key == strstr (key, MC_ACCOUNTS_GCONF_BASE));
  g_assert (strlen (key) > base_len + 1);

  base = key + base_len + 1;
  slash = strchr (base, '/');

  if (slash == NULL)
    return g_strdup (base);
  else
    return g_strndup (base, slash - base);
}

static gchar *
_param_name_from_key (const gchar *key)
{
 const gchar *base, *slash;
 gchar *account_name;

 account_name = _account_name_from_key (key);
 base = strstr (key, account_name);
 slash = strchr (base, '/');
 g_free (account_name);

 return g_strdup (slash+1);
}

static void
_normalize_display_name (AccountData *account)
{

  if (account->display_name != NULL)
    return;

  if (!tp_strdiff (account->protocol, "local-xmpp"))
    {
      account->display_name = g_strdup ("Nearby People");
    }

  else if (!tp_strdiff (account->protocol, "irc"))
    {

      if (account->display_name != NULL)
        {
          g_free (account->display_name);
          account->display_name = NULL;
        }

      account->display_name = g_strdup_printf
          ("%s on %s", account->param_account, account->param_server);

    }

  else if (account->display_name == NULL)
    {

      if (account->param_account != NULL)
        account->display_name = g_strdup
            ((const gchar *) account->param_account);
      else
        account->display_name = g_strdup
            ("No display name");

    }
}

static gboolean
_protocol_is_supported (AccountData *data)
{
  EmpathyConnectionManagers *cm =
      empathy_connection_managers_dup_singleton ();
  GList *cms = empathy_connection_managers_get_cms (cm);
  GList *l;
  gboolean proto_is_supported = FALSE;

  for (l = cms; l; l = l->next)
    {
      TpConnectionManager *tp_cm = l->data;
      const gchar *cm_name = tp_connection_manager_get_name (tp_cm);
      if (tp_connection_manager_has_protocol (tp_cm,
          (const gchar*)data->protocol))
        {
          data->connection_manager = g_strdup (cm_name);
          proto_is_supported = TRUE;
          break;
        }
    }

  g_object_unref (cm);

  return proto_is_supported;
}

static void
_create_account_cb (GObject *source,
  GAsyncResult *result,
  gpointer user_data)
{
  AccountData *data = (AccountData*) user_data;
  EmpathyAccount *account;
  GError *error = NULL;

  account = empathy_account_manager_create_account_finish (
    EMPATHY_ACCOUNT_MANAGER (source), result, &error);

  if (account == NULL)
    {
      DEBUG ("Failed to create account: %s",
          error ? error->message : "No error given");
      g_clear_error (&error);
      _account_data_free (data);
      return;
    }

  DEBUG ("account created\n");
  empathy_account_set_enabled_async (account, data->enabled, NULL, NULL);

  _account_data_free (data);
}

static void
_recurse_account (GSList *entries, AccountData *account)
{
  GSList *tmp;

  for (tmp = entries; tmp != NULL; tmp = tmp->next)
    {

      GConfEntry *entry;
      gchar *param;
      GConfValue *value;

      entry = (GConfEntry*) tmp->data;
      param = _param_name_from_key (gconf_entry_get_key (entry));

      if (!tp_strdiff (param, "profile"))
        {
          const gchar *profile;
          gint i;
          value = gconf_entry_get_value (entry);
          profile = gconf_value_get_string (value);

          DEBUG ("profile: %s\n", profile);

          for (i = 0; i < G_N_ELEMENTS (profile_protocol_map); i++)
            {
              if (!tp_strdiff (profile, profile_protocol_map[i].profile))
                {
                  account->protocol = g_strdup
                      (profile_protocol_map[i].protocol);
                  break;
                }
            }
        }

      else if (!tp_strdiff (param, "enabled"))
        {
          value = gconf_entry_get_value (entry);
          account->enabled = gconf_value_get_bool (value);
        }

      else if (!tp_strdiff (param, "display_name"))
        {
          value = gconf_entry_get_value (entry);
          account->display_name = g_strdup (gconf_value_get_string (value));
        }

      else if (!tp_strdiff (param, "param-account"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          account->param_account = g_strdup (gconf_value_get_string (value));

          my_g_value = tp_g_value_slice_new (G_TYPE_STRING);
          g_value_set_string (my_g_value, account->param_account);
          g_hash_table_insert (account->parameters, "account", my_g_value);

        }

      else if (!tp_strdiff (param, "param-server"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          account->param_account = g_strdup (gconf_value_get_string (value));

          my_g_value = tp_g_value_slice_new (G_TYPE_STRING);
          g_value_set_string (my_g_value, account->param_account);
          g_hash_table_insert (account->parameters, "server", my_g_value);

        }

      else if (!tp_strdiff (param, "param-port"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          my_g_value = tp_g_value_slice_new (G_TYPE_UINT);
          g_value_set_uint (my_g_value, gconf_value_get_int (value));
          g_hash_table_insert (account->parameters, "password", my_g_value);;

        }


      else if (!tp_strdiff (param, "param-password"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          my_g_value = tp_g_value_slice_new (G_TYPE_STRING);
          g_value_set_string (my_g_value, gconf_value_get_string (value));
          g_hash_table_insert (account->parameters, "password", my_g_value);

        }

      else if (!tp_strdiff (param, "param-require-encryption"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          my_g_value = tp_g_value_slice_new (G_TYPE_BOOLEAN);
          g_value_set_boolean (my_g_value, gconf_value_get_bool (value));
          g_hash_table_insert (account->parameters, "require-encryption", my_g_value);

        }

      else if (!tp_strdiff (param, "param-register"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          my_g_value = tp_g_value_slice_new (G_TYPE_BOOLEAN);
          g_value_set_boolean (my_g_value, gconf_value_get_bool (value));
          g_hash_table_insert (account->parameters, "require-register", my_g_value);

        }

      else if (!tp_strdiff (param, "param-ident"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          my_g_value = tp_g_value_slice_new (G_TYPE_STRING);
          g_value_set_string (my_g_value, gconf_value_get_string (value));
          g_hash_table_insert (account->parameters, "ident", my_g_value);

        }

      else if (!tp_strdiff (param, "param-fullname"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          my_g_value = tp_g_value_slice_new (G_TYPE_STRING);
          g_value_set_string (my_g_value, gconf_value_get_string (value));
          g_hash_table_insert (account->parameters, "fullname", my_g_value);

        }

      else if (!tp_strdiff (param, "param-stun-server"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          my_g_value = tp_g_value_slice_new (G_TYPE_STRING);
          g_value_set_string (my_g_value, gconf_value_get_string (value));
          g_hash_table_insert (account->parameters, "stun-server", my_g_value);

        }

      else if (!tp_strdiff (param, "param-stun-port"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          my_g_value = tp_g_value_slice_new (G_TYPE_UINT);
          g_value_set_uint (my_g_value, gconf_value_get_int (value));
          g_hash_table_insert (account->parameters, "stun-port", my_g_value);;

        }

      else if (!tp_strdiff (param, "param-keepalive-interval"))
        {

          GValue *my_g_value;

          value = gconf_entry_get_value (entry);
          my_g_value = tp_g_value_slice_new (G_TYPE_UINT);
          g_value_set_uint (my_g_value, gconf_value_get_int (value));
          g_hash_table_insert (account->parameters, "keepalive-interval", my_g_value);;

        }

      g_free (param);
      gconf_entry_unref (entry);

    }

  _normalize_display_name (account);
}

void empathy_import_mc4_accounts (void)
{
  GConfClient *client;
  GError *error = NULL;
  GSList *dir, *dirs, *entries;
  gboolean import_mc4_accounts;

  client = gconf_client_get_default ();

  import_mc4_accounts = gconf_client_get_bool (client,
      IMPORT_MC4_ACCOUNTS, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to get import_mc4_accounts key: %s\n", error->message);
      g_clear_error (&error);
      g_object_unref (client);
      return;
    }

  if (!import_mc4_accounts)
    {
      g_object_unref (client);
      return;
    }

  DEBUG ("MC 4 accounts are going to be imported\n");

  dirs = gconf_client_all_dirs (client, MC_ACCOUNTS_GCONF_BASE, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to get mc_accounts_gconf_base dirs: %s\n",
          error->message);
      g_clear_error (&error);
      g_object_unref (client);
      return;
    }

  for (dir = dirs; NULL != dir; dir = dir->next)
    {
      gchar *account_name = _account_name_from_key (dir->data);
      AccountData *account;

      account = g_slice_new0 (AccountData);

      account->connection_manager = NULL;
      account->protocol = NULL;
      account->enabled = FALSE;
      account->display_name = NULL;
      account->param_account = NULL;
      account->param_server = NULL;
      account->parameters = g_hash_table_new_full (g_str_hash, g_str_equal,
          NULL, (GDestroyNotify) tp_g_value_slice_free);

      DEBUG ("name : %s\n", account_name);

      entries = gconf_client_all_entries (client, dir->data, &error);

      if (error != NULL)
        {

          DEBUG ("Failed to get all entries: %s\n", error->message);
          g_clear_error (&error);

        }
      else
        {
          _recurse_account (entries, account);
        }

      if (_protocol_is_supported (account))
        {
          EmpathyAccountManager *account_manager;
          GHashTable *properties;

          account_manager = empathy_account_manager_dup_singleton ();

          properties = g_hash_table_new (NULL, NULL);

          DEBUG ("account cm: %s", account->connection_manager);
          DEBUG ("account protocol: %s", account->protocol);
          DEBUG ("account display_name: %s", account->display_name);
          DEBUG ("account param_account: %s", account->param_account);
          DEBUG ("account param_server: %s", account->param_server);
          DEBUG ("enabled: %d", account->enabled);
          tp_asv_dump (account->parameters);

          empathy_account_manager_create_account_async (account_manager,
              (const gchar*) account->connection_manager,
              (const gchar*) account->protocol, account->display_name,
              account->parameters, properties,
              _create_account_cb, account);

          g_hash_table_unref (properties);
          g_object_unref (account_manager);
        }
      else
        {
          DEBUG ("protocol of this account is not supported\n");
          _account_data_free (account);
        }

      g_slist_free (entries);
      g_free (account_name);
      g_free (dir->data);

    }

  gconf_client_set_bool (client,
      IMPORT_MC4_ACCOUNTS, FALSE, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to set import_mc4_accounts key: %s\n", error->message);
      g_clear_error (&error);
    }

  g_slist_free (dirs);
  g_object_unref (client);
}