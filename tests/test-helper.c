/*
 * test-helper.c - Source for some test helper functions
 * Copyright (C) 2007-2009 Collabora Ltd.
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
 */

#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include <libempathy-gtk/empathy-ui-utils.h>

#include "test-helper.h"

void
test_init (int argc,
    char **argv)
{
  g_test_init (&argc, &argv, NULL);
  gtk_init (&argc, &argv);
  empathy_gtk_init ();
}

void
test_deinit (void)
{
  ;
}

gchar *
get_xml_file (const gchar *filename)
{
  return g_build_filename (g_getenv ("EMPATHY_SRCDIR"), "tests", "xml",
      filename, NULL);
}

gchar *
get_user_xml_file (const gchar *filename)
{
  return g_build_filename (g_get_tmp_dir (), filename, NULL);
}

void
copy_xml_file (const gchar *orig,
               const gchar *dest)
{
  gboolean result;
  gchar *buffer;
  gsize length;
  gchar *sample;
  gchar *file;

  sample = get_xml_file (orig);
  result = g_file_get_contents (sample, &buffer, &length, NULL);
  g_assert (result);

  file = get_user_xml_file (dest);
  result = g_file_set_contents (file, buffer, length, NULL);
  g_assert (result);

  g_free (sample);
  g_free (file);
  g_free (buffer);
}

#if 0
EmpathyAccount *
get_test_account (void)
{
  McProfile *profile;
  EmpathyAccountManager *account_manager;
  EmpathyAccount *account;
  GList *accounts;

  account_manager = empathy_account_manager_dup_singleton ();
  profile = mc_profile_lookup ("test");
  accounts = mc_accounts_list_by_profile (profile);
  if (g_list_length (accounts) == 0)
    {
      /* need to create a test account */
      account = empathy_account_manager_create_by_profile (account_manager,
          profile);
    }
  else
    {
      /* reuse an existing test account */
      McAccount *mc_account;
      mc_account = accounts->data;
      account = empathy_account_manager_lookup (account_manager,
        mc_account_get_unique_name (mc_account));
    }
  g_object_unref (account_manager);

  g_object_unref (profile);

  return account;
}

/* Not used for now as there is no API to remove completely gconf keys.
 * So we reuse existing accounts instead of creating new ones */
void
destroy_test_account (EmpathyAccount *account)
{
  GConfClient *client;
  gchar *path;
  GError *error = NULL;
  GSList *entries = NULL, *l;
  EmpathyAccountManager *manager;

  client = gconf_client_get_default ();
  path = g_strdup_printf ("/apps/telepathy/mc/accounts/%s",
      empathy_account_get_unique_name (account));

  entries = gconf_client_all_entries (client, path, &error);
  if (error != NULL)
    {
      g_print ("failed to list entries in %s: %s\n", path, error->message);
      g_error_free (error);
      error = NULL;
    }

  for (l = entries; l != NULL; l = g_slist_next (l))
    {
      GConfEntry *entry = l->data;

      if (g_str_has_suffix (entry->key, "data_dir"))
        {
          gchar *dir;

          dir = gconf_client_get_string (client, entry->key, &error);
          if (error != NULL)
            {
              g_print ("get data_dir string failed: %s\n", entry->key);
              g_error_free (error);
              error = NULL;
            }
          else
            {
              if (g_rmdir (dir) != 0)
                g_print ("can't remove %s\n", dir);
            }

          g_free (dir);
        }

      /* FIXME: this doesn't remove the key */
      gconf_client_unset (client, entry->key, &error);
      if (error != NULL)
        {
          g_print ("unset of %s failed: %s\n", path, error->message);
          g_error_free (error);
          error = NULL;
        }

      gconf_entry_unref (entry);
    }

  g_slist_free (entries);

  g_object_unref (client);
  g_free (path);

  manager = empathy_account_manager_dup_singleton ();
  empathy_account_manager_remove (manager, account);
  g_object_unref (account);
  g_object_unref (manager);
}
#endif
