/*
 * Copyright (C) 2009 Collabora Ltd.
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

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gconf/gconf-client.h>
#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/defs.h>
#include <dbus/dbus-protocol.h>
#include <gnome-keyring.h>
#include <libempathy/empathy-account-settings.h>
#include <libempathy/empathy-connection-managers.h>

#include "empathy-import-mc4-accounts.h"

#define DEBUG_FLAG EMPATHY_DEBUG_IMPORT_MC4_ACCOUNTS
#include <libempathy/empathy-debug.h>

#define MC_ACCOUNTS_GCONF_BASE "/apps/telepathy/mc/accounts"
#define IMPORTED_MC4_ACCOUNTS "/apps/empathy/accounts/imported_mc4_accounts"

typedef struct
{
  gchar *profile;
  gchar *protocol;
} ProfileProtocolMapItem;

static ProfileProtocolMapItem profile_protocol_map[] =
{
  { "ekiga", "sip" },
  { "fwd", "sip" },
  { "gtalk", "jabber" },
  { "msn-haze", "msn" },
  { "salut", "local-xmpp" },
  { "sipphone", "sip" },
  { "sofiasip", "sip" },
};

typedef struct {
  gchar *account_name;
  gboolean enable;
} Misc;

static gchar *
_account_name_from_key (const gchar *key)
{
  guint base_len = strlen (MC_ACCOUNTS_GCONF_BASE);
  const gchar *base, *slash;

  g_assert (g_str_has_prefix (key, MC_ACCOUNTS_GCONF_BASE));
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
 gchar *ret;

 account_name = _account_name_from_key (key);
 base = strstr (key, account_name);
 slash = strchr (base, '/');

 ret = g_strdup (slash+1);
 g_free (account_name);

 return ret;
}

static gchar *
_create_default_display_name (const gchar *protocol)
{
  if (!tp_strdiff (protocol, "local-xmpp"))
    return g_strdup (_("People Nearby"));

  return g_strdup_printf (_("%s account"), protocol);
}

static const gchar *
_get_manager_for_protocol (EmpathyConnectionManagers *managers,
    const gchar *protocol)
{
  GList *cms = empathy_connection_managers_get_cms (managers);
  GList *l;
  TpConnectionManager *haze = NULL;
  TpConnectionManager *cm = NULL;

  for (l = cms; l; l = l->next)
    {
      TpConnectionManager *tp_cm = l->data;

      /* Only use haze if no other cm provides this protocol */
      if (!tp_strdiff (tp_connection_manager_get_name (tp_cm), "haze"))
        {
          haze = tp_cm;
          continue;
        }

      if (tp_connection_manager_has_protocol (tp_cm, protocol))
        {
          cm = tp_cm;
          goto out;
        }
    }

  if (haze != NULL && tp_connection_manager_has_protocol (haze, protocol))
    return tp_connection_manager_get_name (haze);

out:
  return cm != NULL ? tp_connection_manager_get_name (cm) : NULL;
}

static void
_move_contents (const gchar *old, const gchar *new)
{
  GDir *source;
  const gchar *f;
  int ret;

  ret = g_mkdir_with_parents (new, 0777);
  if (ret == -1)
    return;

  source = g_dir_open (old, 0, NULL);
  if (source == NULL)
    return;

  while ((f = g_dir_read_name (source)) != NULL)
    {
      gchar *old_path;
      gchar *new_path;

      old_path = g_build_path (G_DIR_SEPARATOR_S, old, f, NULL);
      new_path = g_build_path (G_DIR_SEPARATOR_S, new, f, NULL);

      if (g_file_test (old_path, G_FILE_TEST_IS_DIR))
        {
          _move_contents (old_path, new_path);
        }
      else
        {
          GFile *f_old, *f_new;

          f_old = g_file_new_for_path (old_path);
          f_new = g_file_new_for_path (new_path);

          g_file_move (f_old, f_new, G_FILE_COPY_NONE,
            NULL, NULL, NULL, NULL);

          g_object_unref (f_old);
          g_object_unref (f_new);
        }

      g_free (old_path);
      g_free (new_path);
    }

  g_dir_close (source);
}

static void
_move_logs (TpAccount *account, const gchar *account_name)
{
  gchar *old_path, *new_path, *escaped;
  const gchar *name;

  name = tp_proxy_get_object_path (account);
  if (g_str_has_prefix (name, TP_ACCOUNT_OBJECT_PATH_BASE))
    name += strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

  escaped = g_strdelimit (g_strdup (name), "/", '_');
  new_path = g_build_path (G_DIR_SEPARATOR_S, g_get_user_data_dir (),
    PACKAGE_NAME, "logs", escaped, NULL);
  g_free (escaped);

  old_path = g_build_path (G_DIR_SEPARATOR_S,
    g_get_home_dir (),
    ".gnome2", PACKAGE_NAME, "logs", account_name, NULL);

  _move_contents (old_path, new_path);
}

static void
_create_account_cb (GObject *source,
  GAsyncResult *result,
  gpointer user_data)
{
  TpAccount *account;
  GError *error = NULL;
  Misc *misc = (Misc *) user_data;

  if (!empathy_account_settings_apply_finish (
      EMPATHY_ACCOUNT_SETTINGS (source), result, &error))
    {
      DEBUG ("Failed to create account: %s", error->message);
      g_error_free (error);
      goto out;
    }

  DEBUG ("account created");
  account = empathy_account_settings_get_account (
    EMPATHY_ACCOUNT_SETTINGS (source));

  _move_logs (account, misc->account_name);

  tp_account_set_enabled_async (account,
      misc->enable, NULL, NULL);

  g_free (misc->account_name);
  g_slice_free (Misc, misc);

out:
  g_object_unref (source);
}

static gchar *
_get_protocol_from_profile (const gchar *profile)
{
  guint i;

  DEBUG ("profile: %s", profile);

  for (i = 0; i < G_N_ELEMENTS (profile_protocol_map); i++)
    if (!tp_strdiff (profile, profile_protocol_map[i].profile))
      return g_strdup (profile_protocol_map[i].protocol);

  return g_strdup (profile);
}

static void
_set_password_from_keyring (EmpathyAccountSettings *settings,
    const gchar *account_name, const gchar *key)
{
  GnomeKeyringResult res;
  gchar *password;
  GnomeKeyringPasswordSchema keyring_schema = {
      GNOME_KEYRING_ITEM_GENERIC_SECRET,
      {
        { "account", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
        { "param", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
        { NULL, 0 }
      }
   };

  res = gnome_keyring_find_password_sync (&keyring_schema,
    &password,
    "account", account_name,
    "param", key,
    NULL);

  if (res == GNOME_KEYRING_RESULT_OK)
    {
       empathy_account_settings_set_string (settings, key, password);
       gnome_keyring_free_password (password);
    }
}

static void
_handle_entry (EmpathyAccountSettings *settings, const gchar *account_name,
    const gchar *key,
    GConfEntry *entry)
{
  const gchar *signature;

  signature = empathy_account_settings_get_dbus_signature (settings, key);
  if (signature == NULL)
    {
      DEBUG ("Parameter %s is unknown", signature);
      return;
    }

  switch ((int)*signature)
    {
      case DBUS_TYPE_INT16:
      case DBUS_TYPE_INT32:
        {
          gint v = gconf_value_get_int (gconf_entry_get_value (entry));
          empathy_account_settings_set_int32 (settings, key, v);
          break;
        }
      case DBUS_TYPE_UINT16:
      case DBUS_TYPE_UINT32:
        {
          gint v = gconf_value_get_int (gconf_entry_get_value (entry));
          empathy_account_settings_set_uint32 (settings, key, v);
          break;
        }
      case DBUS_TYPE_STRING:
        {
          const gchar *v = gconf_value_get_string (
              gconf_entry_get_value (entry));

          /* MC 4 would put password in the keyring and leave the password in
           * gconf keyring */

          if (!tp_strdiff (key, "password") && !tp_strdiff (v, "keyring"))
            _set_password_from_keyring (settings, account_name, key);
          else
              empathy_account_settings_set_string (settings, key, v);
          break;
        }
      case DBUS_TYPE_BOOLEAN:
        {
          gboolean v = gconf_value_get_bool (
              gconf_entry_get_value (entry));

          empathy_account_settings_set_boolean (settings, key, v);
          break;
        }
     default:
       DEBUG ("Unsupported type in signature: %s", signature);
    }
}

static void
_recurse_account (GSList *entries, EmpathyAccountSettings *settings,
  const gchar *account_name)
{
  GSList *tmp;

  for (tmp = entries; tmp != NULL; tmp = tmp->next)
    {

      GConfEntry *entry;
      gchar *param;

      entry = (GConfEntry *) tmp->data;
      param = _param_name_from_key (gconf_entry_get_key (entry));

      if (g_str_has_prefix (param, "param-"))
        {
          _handle_entry (settings, account_name, param + strlen ("param-"),
            entry);
        }

      g_free (param);
      gconf_entry_unref (entry);
    }
}

static gboolean
import_one_account (const char *path,
  EmpathyConnectionManagers *managers,
  GConfClient *client)
{
  gchar *account_name = _account_name_from_key (path);
  EmpathyAccountSettings *settings = NULL;
  GError *error = NULL;
  GSList *entries = NULL;
  gchar *profile = NULL;
  gchar *protocol = NULL;
  const gchar *manager;
  gchar *display_name;
  gchar *key;
  gboolean enabled = FALSE;
  gboolean ret = FALSE;
  Misc *misc;

  DEBUG ("Starting import of %s (%s)", path, account_name);

  key = g_strdup_printf ("%s/profile", path);
  profile = gconf_client_get_string (client, key, NULL);
  g_free (key);

  if (profile == NULL)
    {
      DEBUG ("Account is missing a profile entry");
      goto failed;
    }

  protocol = _get_protocol_from_profile (profile);
  manager = _get_manager_for_protocol (managers, protocol);
  if (manager == NULL)
    {
      DEBUG ("No manager available for this protocol %s", protocol);
      goto failed;
    }

  key = g_strdup_printf ("%s/display_name", path);
  display_name = gconf_client_get_string (client, key, NULL);
  g_free (key);

  if (display_name == NULL)
    display_name = _create_default_display_name (protocol);

  settings = empathy_account_settings_new (manager, protocol, display_name);
  g_free (display_name);

  /* Bit of a hack, as we know EmpathyConnectionManagers is ready the
   * EmpathyAccountSettings should be ready right away as well */
  g_assert (empathy_account_settings_is_ready (settings));

  entries = gconf_client_all_entries (client, path, &error);

  if (entries == NULL)
    {
      DEBUG ("Failed to get all entries: %s", error->message);
      g_error_free (error);
      goto failed;
    }

  _recurse_account (entries, settings, account_name);

  key = g_strdup_printf ("%s/enabled", path);
  enabled = gconf_client_get_bool (client, key, NULL);
  g_free (key);

  misc = g_slice_new (Misc);
  misc->account_name = account_name;
  misc->enable = enabled;

  empathy_account_settings_apply_async (settings,
          _create_account_cb, misc);
  ret = TRUE;

out:
  g_free (protocol);
  g_free (profile);
  g_slist_free (entries);

  return ret;

failed:
  DEBUG ("Failed to import %s", path);
  g_free (account_name);
  if (settings != NULL)
    g_object_unref (settings);
  goto out;
}

gboolean
empathy_import_mc4_has_imported (void)
{
  GConfClient *client;
  gboolean ret;

  client = gconf_client_get_default ();

  ret = gconf_client_get_bool (client, IMPORTED_MC4_ACCOUNTS, NULL);
  g_object_unref (client);

  return ret;
}

gboolean
empathy_import_mc4_accounts (EmpathyConnectionManagers *managers)
{
  GConfClient *client;
  GError *error = NULL;
  GSList *dir, *dirs = NULL;
  gboolean imported_mc4_accounts;
  gboolean imported = FALSE;

  g_return_val_if_fail (empathy_connection_managers_is_ready (managers),
    FALSE);

  client = gconf_client_get_default ();

  imported_mc4_accounts = gconf_client_get_bool (client,
      IMPORTED_MC4_ACCOUNTS, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to get import_mc4_accounts key: %s", error->message);
      g_error_free (error);
      goto out;
    }

  if (imported_mc4_accounts)
    {
      DEBUG ("Mc4 accounts previously imported");
      goto out;
    }

  DEBUG ("MC 4 accounts are going to be imported");

  dirs = gconf_client_all_dirs (client, MC_ACCOUNTS_GCONF_BASE, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to get MC4 account dirs: %s",
          error->message);
      g_clear_error (&error);
      g_object_unref (client);
      goto out;
    }

  for (dir = dirs; NULL != dir; dir = dir->next)
    {
      if (import_one_account ((gchar *) dir->data, managers, client))
        imported = TRUE;
      g_free (dir->data);
    }

out:
  gconf_client_set_bool (client, IMPORTED_MC4_ACCOUNTS, TRUE, NULL);

  g_slist_free (dirs);
  g_object_unref (client);
  return imported;
}
