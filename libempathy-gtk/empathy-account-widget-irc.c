/*
 * Copyright (C) 2007-2008 Guillaume Desmottes
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
 * Authors: Guillaume Desmottes <gdesmott@gnome.org>
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-irc-network-manager.h>

#include "empathy-irc-network-dialog.h"
#include "empathy-account-widget.h"
#include "empathy-account-widget-private.h"
#include "empathy-account-widget-irc.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT | EMPATHY_DEBUG_IRC
#include <libempathy/empathy-debug.h>

#define IRC_NETWORKS_FILENAME "irc-networks.xml"

typedef struct {
  EmpathyAccountWidget *self;
  EmpathyIrcNetworkManager *network_manager;

  GtkWidget *vbox_settings;

  GtkWidget *combobox_network;
} EmpathyAccountWidgetIrc;

enum {
  COL_NETWORK_OBJ,
  COL_NETWORK_NAME,
};

static void
account_widget_irc_destroy_cb (GtkWidget *widget,
                               EmpathyAccountWidgetIrc *settings)
{
  g_object_unref (settings->network_manager);
  g_slice_free (EmpathyAccountWidgetIrc, settings);
}

static void
unset_server_params (EmpathyAccountWidgetIrc *settings)
{
  EmpathyAccountSettings *ac_settings;

  g_object_get (settings->self, "settings", &ac_settings, NULL);
  DEBUG ("Unset server, port and use-ssl");
  empathy_account_settings_unset (ac_settings, "server");
  empathy_account_settings_unset (ac_settings, "port");
  empathy_account_settings_unset (ac_settings, "use-ssl");
}

static void
update_server_params (EmpathyAccountWidgetIrc *settings)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  EmpathyIrcNetwork *network;
  GSList *servers;
  gchar *charset;
  EmpathyAccountSettings *ac_settings;

  g_object_get (settings->self, "settings", &ac_settings, NULL);

  if (!gtk_combo_box_get_active_iter (
        GTK_COMBO_BOX (settings->combobox_network), &iter))
    {
      unset_server_params (settings);
      return;
    }

  model = gtk_combo_box_get_model (GTK_COMBO_BOX (settings->combobox_network));
  gtk_tree_model_get (model, &iter, COL_NETWORK_OBJ, &network, -1);

  g_assert (network != NULL);

  g_object_get (network, "charset", &charset, NULL);
  DEBUG ("Setting charset to %s", charset);
  empathy_account_settings_set_string (ac_settings, "charset", charset);
  g_free (charset);

  servers = empathy_irc_network_get_servers (network);
  if (g_slist_length (servers) > 0)
    {
      /* set the first server as CM server */
      EmpathyIrcServer *server = servers->data;
      gchar *address;
      guint port;
      gboolean ssl;

      g_object_get (server,
          "address", &address,
          "port", &port,
          "ssl", &ssl,
          NULL);

      DEBUG ("Setting server to %s", address);
      empathy_account_settings_set_string (ac_settings, "server", address);
      DEBUG ("Setting port to %u", port);
      empathy_account_settings_set_uint32 (ac_settings, "port", port);
      DEBUG ("Setting use-ssl to %s", ssl ? "TRUE": "FALSE" );
      empathy_account_settings_set_boolean (ac_settings, "use-ssl", ssl);

      g_free (address);
    }
  else
    {
      /* No server. Unset values */
      unset_server_params (settings);
    }

  g_slist_foreach (servers, (GFunc) g_object_unref, NULL);
  g_slist_free (servers);
  g_object_unref (network);
}

static void
irc_network_dialog_destroy_cb (GtkWidget *widget,
                               EmpathyAccountWidgetIrc *settings)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  EmpathyIrcNetwork *network;
  gchar *name;

  /* name could be changed */
  gtk_combo_box_get_active_iter (GTK_COMBO_BOX (settings->combobox_network),
      &iter);
  model = gtk_combo_box_get_model (GTK_COMBO_BOX (settings->combobox_network));
  gtk_tree_model_get (model, &iter, COL_NETWORK_OBJ, &network, -1);

  g_object_get (network, "name", &name, NULL);
  gtk_list_store_set (GTK_LIST_STORE (model), &iter,
      COL_NETWORK_NAME, name, -1);

  update_server_params (settings);

  g_object_unref (network);
  g_free (name);
}

static void
display_irc_network_dialog (EmpathyAccountWidgetIrc *settings,
                            EmpathyIrcNetwork *network)
{
  GtkWindow *window;
  GtkWidget *dialog;

  window = empathy_get_toplevel_window (settings->vbox_settings);
  dialog = empathy_irc_network_dialog_show (network, GTK_WIDGET (window));
  g_signal_connect (dialog, "destroy",
      G_CALLBACK (irc_network_dialog_destroy_cb), settings);
}

static void
account_widget_irc_button_edit_network_clicked_cb (
    GtkWidget *button,
    EmpathyAccountWidgetIrc *settings)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  EmpathyIrcNetwork *network;

  gtk_combo_box_get_active_iter (GTK_COMBO_BOX (settings->combobox_network),
      &iter);
  model = gtk_combo_box_get_model (GTK_COMBO_BOX (settings->combobox_network));
  gtk_tree_model_get (model, &iter, COL_NETWORK_OBJ, &network, -1);

  g_assert (network != NULL);

  display_irc_network_dialog (settings, network);

  g_object_unref (network);
}

static void
account_widget_irc_button_remove_clicked_cb (GtkWidget *button,
                                             EmpathyAccountWidgetIrc *settings)
{
  EmpathyIrcNetwork *network;
  GtkTreeIter iter;
  GtkTreeModel *model;
  gchar *name;

  gtk_combo_box_get_active_iter (GTK_COMBO_BOX (settings->combobox_network),
      &iter);
  model = gtk_combo_box_get_model (GTK_COMBO_BOX (settings->combobox_network));
  gtk_tree_model_get (model, &iter, COL_NETWORK_OBJ, &network, -1);

  g_assert (network != NULL);

  g_object_get (network, "name", &name, NULL);
  DEBUG ("Remove network %s", name);

  gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
  empathy_irc_network_manager_remove (settings->network_manager, network);

  /* Select the first network */
  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      gtk_combo_box_set_active_iter (
          GTK_COMBO_BOX (settings->combobox_network), &iter);
    }

  g_free (name);
  g_object_unref (network);
}

static void
account_widget_irc_button_add_network_clicked_cb (GtkWidget *button,
                                                  EmpathyAccountWidgetIrc *settings)
{
  EmpathyIrcNetwork *network;
  GtkTreeModel *model;
  GtkListStore *store;
  gchar *name;
  GtkTreeIter iter;

  network = empathy_irc_network_new (_("New Network"));
  empathy_irc_network_manager_add (settings->network_manager, network);

  model = gtk_combo_box_get_model (GTK_COMBO_BOX (settings->combobox_network));
  store = GTK_LIST_STORE (model);

  g_object_get (network, "name", &name, NULL);

  gtk_list_store_insert_with_values (store, &iter, -1,
      COL_NETWORK_OBJ, network,
      COL_NETWORK_NAME, name,
      -1);

  gtk_combo_box_set_active_iter (GTK_COMBO_BOX (settings->combobox_network),
      &iter);

  display_irc_network_dialog (settings, network);

  g_free (name);
  g_object_unref (network);
}

static void
account_widget_irc_combobox_network_changed_cb (GtkWidget *combobox,
                                                EmpathyAccountWidgetIrc *settings)
{
  update_server_params (settings);
  empathy_account_widget_changed (settings->self);
}

static void
fill_networks_model (EmpathyAccountWidgetIrc *settings,
                     EmpathyIrcNetwork *network_to_select)
{
  GSList *networks, *l;
  GtkTreeModel *model;
  GtkListStore *store;

  networks = empathy_irc_network_manager_get_networks (
      settings->network_manager);

  model = gtk_combo_box_get_model (GTK_COMBO_BOX (settings->combobox_network));
  store = GTK_LIST_STORE (model);

  for (l = networks; l != NULL; l = g_slist_next (l))
    {
      gchar *name;
      EmpathyIrcNetwork *network = l->data;
      GtkTreeIter iter;

      g_object_get (network, "name", &name, NULL);

      gtk_list_store_insert_with_values (store, &iter, -1,
          COL_NETWORK_OBJ, network,
          COL_NETWORK_NAME, name,
          -1);

       if (network == network_to_select)
         {
           gtk_combo_box_set_active_iter (
               GTK_COMBO_BOX (settings->combobox_network), &iter);
         }

      g_free (name);
      g_object_unref (network);
    }

  if (network_to_select == NULL)
    {
      /* Select the first network */
      GtkTreeIter iter;

      if (gtk_tree_model_get_iter_first (model, &iter))
        {
          gtk_combo_box_set_active_iter (
              GTK_COMBO_BOX (settings->combobox_network), &iter);

          update_server_params (settings);
        }
    }

  g_slist_free (networks);
}

static void
account_widget_irc_setup (EmpathyAccountWidgetIrc *settings)
{
  const gchar *nick = NULL;
  const gchar *fullname = NULL;
  const gchar *server = NULL;
  gint port = 6667;
  const gchar *charset;
  gboolean ssl = FALSE;
  EmpathyIrcNetwork *network = NULL;
  EmpathyAccountSettings *ac_settings;

  g_object_get (settings->self, "settings", &ac_settings, NULL);

  nick = empathy_account_settings_get_string (ac_settings, "account");
  fullname = empathy_account_settings_get_string (ac_settings,
      "fullname");
  server = empathy_account_settings_get_string (ac_settings, "server");
  charset = empathy_account_settings_get_string (ac_settings, "charset");
  port = empathy_account_settings_get_uint32 (ac_settings, "port");
  ssl = empathy_account_settings_get_boolean (ac_settings, "use-ssl");

  if (!nick)
    {
      nick = g_strdup (g_get_user_name ());
      empathy_account_settings_set_string (ac_settings,
        "account", nick);
    }

  if (!fullname)
    {
      fullname = g_strdup (g_get_real_name ());
      if (!fullname)
        {
          fullname = g_strdup (nick);
        }
      empathy_account_settings_set_string (ac_settings,
          "fullname", fullname);
    }

  if (server != NULL)
    {
      GtkListStore *store;

      network = empathy_irc_network_manager_find_network_by_address (
          settings->network_manager, server);


      store = GTK_LIST_STORE (gtk_combo_box_get_model (
            GTK_COMBO_BOX (settings->combobox_network)));

      if (network != NULL)
        {
          gchar *name;

          g_object_set (network, "charset", charset, NULL);

          g_object_get (network, "name", &name, NULL);
          DEBUG ("Account use network %s", name);

          g_free (name);
        }
      else
        {
          /* We don't have this network. Let's create it */
          EmpathyIrcServer *srv;
          GtkTreeIter iter;

          DEBUG ("Create a network %s", server);
          network = empathy_irc_network_new (server);
          srv = empathy_irc_server_new (server, port, ssl);

          empathy_irc_network_append_server (network, srv);
          empathy_irc_network_manager_add (settings->network_manager, network);

          gtk_list_store_insert_with_values (store, &iter, -1,
              COL_NETWORK_OBJ, network,
              COL_NETWORK_NAME, server,
              -1);

          gtk_combo_box_set_active_iter (
              GTK_COMBO_BOX (settings->combobox_network), &iter);

          g_object_unref (srv);
          g_object_unref (network);
        }
    }


  fill_networks_model (settings, network);
}

void
empathy_account_widget_irc_build (EmpathyAccountWidget *self,
    const char *filename,
    GtkWidget **table_common_settings)
{
  EmpathyAccountWidgetIrc *settings;
  gchar *dir, *user_file_with_path, *global_file_with_path;
  GtkListStore *store;
  GtkCellRenderer *renderer;

  settings = g_slice_new0 (EmpathyAccountWidgetIrc);
  settings->self = self;

  dir = g_build_filename (g_get_user_config_dir (), PACKAGE_NAME, NULL);
  g_mkdir_with_parents (dir, S_IRUSR | S_IWUSR | S_IXUSR);
  user_file_with_path = g_build_filename (dir, IRC_NETWORKS_FILENAME, NULL);
  g_free (dir);

  global_file_with_path = g_build_filename (g_getenv ("EMPATHY_SRCDIR"),
      "libempathy-gtk", IRC_NETWORKS_FILENAME, NULL);
  if (!g_file_test (global_file_with_path, G_FILE_TEST_EXISTS))
    {
      g_free (global_file_with_path);
      global_file_with_path = g_build_filename (DATADIR, "empathy",
          IRC_NETWORKS_FILENAME, NULL);
    }

  settings->network_manager = empathy_irc_network_manager_new (
      global_file_with_path,
      user_file_with_path);

  g_free (global_file_with_path);
  g_free (user_file_with_path);

  self->ui_details->gui = empathy_builder_get_file (filename,
      "table_irc_settings", table_common_settings,
      "vbox_irc", &self->ui_details->widget,
      "table_irc_settings", &settings->vbox_settings,
      "combobox_network", &settings->combobox_network,
      NULL);

  /* Fill the networks combobox */
  store = gtk_list_store_new (2, G_TYPE_OBJECT, G_TYPE_STRING);

  gtk_cell_layout_clear (GTK_CELL_LAYOUT (settings->combobox_network));
  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (settings->combobox_network),
      renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (settings->combobox_network),
      renderer,
      "text", COL_NETWORK_NAME,
      NULL);

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
      COL_NETWORK_NAME,
      GTK_SORT_ASCENDING);

  gtk_combo_box_set_model (GTK_COMBO_BOX (settings->combobox_network),
      GTK_TREE_MODEL (store));
  g_object_unref (store);

  account_widget_irc_setup (settings);

  empathy_account_widget_handle_params (self,
      "entry_nick", "account",
      "entry_fullname", "fullname",
      "entry_password", "password",
      "entry_quit_message", "quit-message",
      NULL);

  empathy_builder_connect (self->ui_details->gui, settings,
      "table_irc_settings", "destroy", account_widget_irc_destroy_cb,
      "button_network", "clicked",
          account_widget_irc_button_edit_network_clicked_cb,
      "button_add_network", "clicked",
          account_widget_irc_button_add_network_clicked_cb,
      "button_remove_network", "clicked",
          account_widget_irc_button_remove_clicked_cb,
      "combobox_network", "changed",
          account_widget_irc_combobox_network_changed_cb,
      NULL);

  self->ui_details->default_focus = g_strdup ("entry_nick");
}
