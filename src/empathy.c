/*
 * Copyright (C) 2007-2009 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 */

#include <config.h>

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <unique/unique.h>

#if HAVE_LIBCHAMPLAIN
#include <clutter-gtk/clutter-gtk.h>
#endif

#include <libnotify/notify.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/debug-sender.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/connection-manager.h>
#include <telepathy-glib/interfaces.h>

#ifdef ENABLE_TPL
#include <telepathy-logger/log-manager.h>
#include <telepathy-logger/log-store-empathy.h>
#else

#include <libempathy/empathy-log-manager.h>
#endif /* ENABLE_TPL */
#include <libempathy/empathy-idle.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-call-factory.h>
#include <libempathy/empathy-chatroom-manager.h>
#include <libempathy/empathy-account-settings.h>
#include <libempathy/empathy-connectivity.h>
#include <libempathy/empathy-connection-managers.h>
#include <libempathy/empathy-dispatcher.h>
#include <libempathy/empathy-dispatch-operation.h>
#include <libempathy/empathy-ft-factory.h>
#include <libempathy/empathy-tp-chat.h>
#include <libempathy/empathy-tp-call.h>

#include <libempathy-gtk/empathy-conf.h>
#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-location-manager.h>

#include "empathy-main-window.h"
#include "empathy-import-mc4-accounts.h"
#include "empathy-accounts-common.h"
#include "empathy-accounts-dialog.h"
#include "empathy-status-icon.h"
#include "empathy-call-window.h"
#include "empathy-chat-window.h"
#include "empathy-ft-manager.h"

#include "extensions/extensions.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#include <gst/gst.h>

static gboolean start_hidden = FALSE;
static gboolean no_connect = FALSE;

static void account_manager_ready_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data);

static void
dispatch_cb (EmpathyDispatcher *dispatcher,
    EmpathyDispatchOperation *operation,
    gpointer user_data)
{
  GQuark channel_type;

  channel_type = empathy_dispatch_operation_get_channel_type_id (operation);

  if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_TEXT)
    {
      EmpathyTpChat *tp_chat;
      EmpathyChat   *chat = NULL;
      const gchar   *id;

      tp_chat = EMPATHY_TP_CHAT
        (empathy_dispatch_operation_get_channel_wrapper (operation));

      id = empathy_tp_chat_get_id (tp_chat);
      if (!EMP_STR_EMPTY (id))
        {
          TpConnection *connection;
          TpAccount *account;

          connection = empathy_tp_chat_get_connection (tp_chat);
          account = empathy_get_account_for_connection (connection);
          chat = empathy_chat_window_find_chat (account, id);
        }

      if (chat)
        {
          empathy_chat_set_tp_chat (chat, tp_chat);
        }
      else
        {
          chat = empathy_chat_new (tp_chat);
          /* empathy_chat_new returns a floating reference as EmpathyChat is
           * a GtkWidget. This reference will be taken by a container
           * (a GtkNotebook) when we'll call empathy_chat_window_present_chat */
        }

      empathy_chat_window_present_chat (chat);

      empathy_dispatch_operation_claim (operation);
    }
  else if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA)
    {
      EmpathyCallFactory *factory;

      factory = empathy_call_factory_get ();
      empathy_call_factory_claim_channel (factory, operation);
    }
  else if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_FILE_TRANSFER)
    {
      EmpathyFTFactory *factory;

      factory = empathy_ft_factory_dup_singleton ();

      /* if the operation is not incoming, don't claim it,
       * as it might have been triggered by another client, and
       * we are observing it.
       */
      if (empathy_dispatch_operation_is_incoming (operation))
        empathy_ft_factory_claim_channel (factory, operation);
    }
}

static void
use_conn_notify_cb (EmpathyConf *conf,
    const gchar *key,
    gpointer     user_data)
{
  EmpathyConnectivity *connectivity = user_data;
  gboolean     use_conn;

  if (empathy_conf_get_bool (conf, key, &use_conn))
    {
      empathy_connectivity_set_use_conn (connectivity, use_conn);
    }
}

static void
migrate_config_to_xdg_dir (void)
{
  gchar *xdg_dir, *old_dir, *xdg_filename, *old_filename;
  int i;
  GFile *xdg_file, *old_file;
  static const gchar* filenames[] = {
    "geometry.ini",
    "irc-networks.xml",
    "chatrooms.xml",
    "contact-groups.xml",
    "status-presets.xml",
    "accels.txt",
    NULL
  };

  xdg_dir = g_build_filename (g_get_user_config_dir (), PACKAGE_NAME, NULL);
  if (g_file_test (xdg_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
    {
      /* xdg config dir already exists */
      g_free (xdg_dir);
      return;
    }

  old_dir = g_build_filename (g_get_home_dir (), ".gnome2",
      PACKAGE_NAME, NULL);
  if (!g_file_test (old_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
    {
      /* old config dir didn't exist */
      g_free (xdg_dir);
      g_free (old_dir);
      return;
    }

  if (g_mkdir_with_parents (xdg_dir, (S_IRUSR | S_IWUSR | S_IXUSR)) == -1)
    {
      DEBUG ("Failed to create configuration directory; aborting migration");
      g_free (xdg_dir);
      g_free (old_dir);
      return;
    }

  for (i = 0; filenames[i]; i++)
    {
      old_filename = g_build_filename (old_dir, filenames[i], NULL);
      if (!g_file_test (old_filename, G_FILE_TEST_EXISTS))
        {
          g_free (old_filename);
          continue;
        }
      xdg_filename = g_build_filename (xdg_dir, filenames[i], NULL);
      old_file = g_file_new_for_path (old_filename);
      xdg_file = g_file_new_for_path (xdg_filename);

      if (!g_file_move (old_file, xdg_file, G_FILE_COPY_NONE,
          NULL, NULL, NULL, NULL))
        DEBUG ("Failed to migrate %s", filenames[i]);

      g_free (old_filename);
      g_free (xdg_filename);
      g_object_unref (old_file);
      g_object_unref (xdg_file);
    }

  g_free (xdg_dir);
  g_free (old_dir);
}

static void
show_accounts_ui (GdkScreen *screen,
    gboolean if_needed)
{
  empathy_accounts_dialog_show_application (screen,
      NULL, if_needed, start_hidden);
}

static UniqueResponse
unique_app_message_cb (UniqueApp *unique_app,
    gint command,
    UniqueMessageData *data,
    guint timestamp,
    gpointer user_data)
{
  GtkWindow *window = user_data;
  TpAccountManager *account_manager;

  DEBUG ("Other instance launched, presenting the main window. "
      "Command=%d, timestamp %u", command, timestamp);

  /* XXX: the standalone app somewhat breaks this case, since
   * communicating it would be a pain */

  /* We're requested to show stuff again, disable the start hidden global
   * in case the accounts wizard wants to pop up.
   */
  start_hidden = FALSE;

  gtk_window_set_screen (GTK_WINDOW (window),
      unique_message_data_get_screen (data));
  gtk_window_set_startup_id (GTK_WINDOW (window),
      unique_message_data_get_startup_id (data));
  gtk_window_present_with_time (GTK_WINDOW (window), timestamp);
  gtk_window_set_skip_taskbar_hint (window, FALSE);

  account_manager = tp_account_manager_dup ();
  tp_account_manager_prepare_async (account_manager, NULL,
      account_manager_ready_cb, NULL);
  g_object_unref (account_manager);

  return UNIQUE_RESPONSE_OK;
}

static gboolean
show_version_cb (const char *option_name,
    const char *value,
    gpointer data,
    GError **error)
{
  g_print ("%s\n", PACKAGE_STRING);

  exit (EXIT_SUCCESS);

  return FALSE;
}

static void
new_incoming_transfer_cb (EmpathyFTFactory *factory,
    EmpathyFTHandler *handler,
    GError *error,
    gpointer user_data)
{
  if (error)
    empathy_ft_manager_display_error (handler, error);
  else
    empathy_receive_file_with_file_chooser (handler);
}

static void
new_ft_handler_cb (EmpathyFTFactory *factory,
    EmpathyFTHandler *handler,
    GError *error,
    gpointer user_data)
{
  if (error)
    empathy_ft_manager_display_error (handler, error);
  else
    empathy_ft_manager_add_handler (handler);

  g_object_unref (handler);
}

static void
new_call_handler_cb (EmpathyCallFactory *factory,
    EmpathyCallHandler *handler,
    gboolean outgoing,
    gpointer user_data)
{
  EmpathyCallWindow *window;

  window = empathy_call_window_new (handler);
  gtk_widget_show (GTK_WIDGET (window));
}

static void
account_manager_ready_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
  GError *error = NULL;
  EmpathyIdle *idle;
  EmpathyConnectivity *connectivity;
  gboolean autoconnect = TRUE;
  TpConnectionPresenceType presence;

  if (!tp_account_manager_prepare_finish (manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  /* Autoconnect */
  idle = empathy_idle_dup_singleton ();
  connectivity = empathy_connectivity_dup_singleton ();

  presence = tp_account_manager_get_most_available_presence (manager, NULL,
      NULL);

  empathy_conf_get_bool (empathy_conf_get (),
      EMPATHY_PREFS_AUTOCONNECT, &autoconnect);
  if (autoconnect && !no_connect &&
      tp_connection_presence_type_cmp_availability
          (presence, TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
            <= 0)
      /* if current state is Offline, then put it online */
      empathy_idle_set_state (idle, TP_CONNECTION_PRESENCE_TYPE_AVAILABLE);

  /* Pop up the accounts dialog if it's needed (either when we don't have any
   * accounts yet or when we haven't imported mc4 accounts yet */
  if (!empathy_accounts_has_accounts (manager)
      || !empathy_import_mc4_has_imported ())
    show_accounts_ui (gdk_screen_get_default (), TRUE);

  g_object_unref (idle);
  g_object_unref (connectivity);
}

static EmpathyDispatcher *
setup_dispatcher (void)
{
  EmpathyDispatcher *d;
  GPtrArray *filters;
  struct {
    const gchar *channeltype;
    TpHandleType handletype;
  } types[] = {
    /* Text channels with handle types none, contact and room */
    { TP_IFACE_CHANNEL_TYPE_TEXT, TP_HANDLE_TYPE_NONE  },
    { TP_IFACE_CHANNEL_TYPE_TEXT, TP_HANDLE_TYPE_CONTACT  },
    { TP_IFACE_CHANNEL_TYPE_TEXT, TP_HANDLE_TYPE_ROOM  },
    /* file transfer to contacts */
    { TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, TP_HANDLE_TYPE_CONTACT  },
    /* stream media to contacts */
    { TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA, TP_HANDLE_TYPE_CONTACT  },
    /* roomlists */
    { TP_IFACE_CHANNEL_TYPE_ROOM_LIST, TP_HANDLE_TYPE_NONE },
  };
  gchar *capabilities[] = {
    "org.freedesktop.Telepathy.Channel.Interface.MediaSignalling/ice-udp",
    "org.freedesktop.Telepathy.Channel.Interface.MediaSignalling/gtalk-p2p",
    NULL };
  GHashTable *asv;
  guint i;

  /* Setup the basic Client.Handler that matches our client filter */
  filters = g_ptr_array_new ();
  asv = tp_asv_new (
        TP_IFACE_CHANNEL ".ChannelType", G_TYPE_STRING,
           TP_IFACE_CHANNEL_TYPE_TEXT,
        TP_IFACE_CHANNEL ".TargetHandleType", G_TYPE_INT,
            TP_HANDLE_TYPE_CONTACT,
        NULL);
  g_ptr_array_add (filters, asv);

  d = empathy_dispatcher_new (PACKAGE_NAME, filters, NULL);

  g_ptr_array_foreach (filters, (GFunc) g_hash_table_destroy, NULL);
  g_ptr_array_free (filters, TRUE);

  /* Setup the an extended Client.Handler that matches everything we can do */
  filters = g_ptr_array_new ();
  for (i = 0 ; i < G_N_ELEMENTS (types); i++)
    {
      asv = tp_asv_new (
        TP_IFACE_CHANNEL ".ChannelType", G_TYPE_STRING, types[i].channeltype,
        TP_IFACE_CHANNEL ".TargetHandleType", G_TYPE_INT, types[i].handletype,
        NULL);

      g_ptr_array_add (filters, asv);
    }

  asv = tp_asv_new (
        TP_IFACE_CHANNEL ".ChannelType",
          G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
        TP_IFACE_CHANNEL ".TargetHandleType",
          G_TYPE_INT, TP_HANDLE_TYPE_CONTACT,
        TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialAudio",
          G_TYPE_BOOLEAN, TRUE,
        NULL);
  g_ptr_array_add (filters, asv);

  asv = tp_asv_new (
        TP_IFACE_CHANNEL ".ChannelType",
          G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
        TP_IFACE_CHANNEL ".TargetHandleType",
          G_TYPE_INT, TP_HANDLE_TYPE_CONTACT,
        TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialVideo",
          G_TYPE_BOOLEAN, TRUE,
        NULL);
  g_ptr_array_add (filters, asv);


  empathy_dispatcher_add_handler (d, PACKAGE_NAME"MoreThanMeetsTheEye",
    filters, capabilities);

  g_ptr_array_foreach (filters, (GFunc) g_hash_table_destroy, NULL);
  g_ptr_array_free (filters, TRUE);

  return d;
}

static void
account_status_changed_cb (TpAccount *account,
    guint old_status,
    guint new_status,
    guint reason,
    gchar *dbus_error_name,
    GHashTable *details,
    EmpathyChatroom *room)
{
  TpConnection *conn;

  conn = tp_account_get_connection (account);
  if (conn == NULL)
    return;

  empathy_dispatcher_join_muc (conn,
      empathy_chatroom_get_room (room), NULL, NULL);
}

static void
account_manager_chatroom_ready_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
  EmpathyChatroomManager *chatroom_manager = user_data;
  GList *accounts, *l;
  GError *error = NULL;

  if (!tp_account_manager_prepare_finish (account_manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  accounts = tp_account_manager_get_valid_accounts (account_manager);

  for (l = accounts; l != NULL; l = g_list_next (l))
    {
      TpAccount *account = TP_ACCOUNT (l->data);
      TpConnection *conn;
      GList *chatrooms, *p;

      conn = tp_account_get_connection (account);

      chatrooms = empathy_chatroom_manager_get_chatrooms (
          chatroom_manager, account);

      for (p = chatrooms; p != NULL; p = p->next)
        {
          EmpathyChatroom *room = EMPATHY_CHATROOM (p->data);

          if (!empathy_chatroom_get_auto_connect (room))
            continue;

          if (conn == NULL)
            {
              g_signal_connect (G_OBJECT (account), "status-changed",
                  G_CALLBACK (account_status_changed_cb), room);
            }
          else
            {
              empathy_dispatcher_join_muc (conn,
                  empathy_chatroom_get_room (room), NULL, NULL);
            }
        }

      g_list_free (chatrooms);
    }

  g_list_free (accounts);
}

static void
chatroom_manager_ready_cb (EmpathyChatroomManager *chatroom_manager,
    GParamSpec *pspec,
    gpointer user_data)
{
  TpAccountManager *account_manager = user_data;

  tp_account_manager_prepare_async (account_manager, NULL,
      account_manager_chatroom_ready_cb, chatroom_manager);
}

int
main (int argc, char *argv[])
{
#if HAVE_GEOCLUE
  EmpathyLocationManager *location_manager = NULL;
#endif
  EmpathyStatusIcon *icon;
  EmpathyDispatcher *dispatcher;
  TpAccountManager *account_manager;
#ifdef ENABLE_TPL
  TplLogManager *log_manager;
#else
  EmpathyLogManager *log_manager;
#endif /* ENABLE_TPL */
  EmpathyChatroomManager *chatroom_manager;
  EmpathyCallFactory *call_factory;
  EmpathyFTFactory  *ft_factory;
  GtkWidget *window;
  EmpathyIdle *idle;
  EmpathyConnectivity *connectivity;
  GError *error = NULL;
  UniqueApp *unique_app;
  gboolean chatroom_manager_ready;

#ifdef ENABLE_DEBUG
  TpDebugSender *debug_sender;
#endif /* ENABLE_TPL */

  GOptionContext *optcontext;
  GOptionEntry options[] = {
      { "no-connect", 'n',
        0, G_OPTION_ARG_NONE, &no_connect,
        N_("Don't connect on startup"),
        NULL },
      { "start-hidden", 'h',
        0, G_OPTION_ARG_NONE, &start_hidden,
        N_("Don't display the contact list or any other dialogs on startup"),
        NULL },
      { "version", 'v',
        G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, show_version_cb,
        NULL, NULL },
      { NULL }
  };

  /* Init */
  g_thread_init (NULL);
  empathy_init ();

  optcontext = g_option_context_new (N_("- Empathy IM Client"));
  g_option_context_add_group (optcontext, gst_init_get_option_group ());
  g_option_context_add_group (optcontext, gtk_get_option_group (TRUE));
#if HAVE_LIBCHAMPLAIN
  g_option_context_add_group (optcontext, clutter_get_option_group ());
#endif
  g_option_context_add_main_entries (optcontext, options, GETTEXT_PACKAGE);

  if (!g_option_context_parse (optcontext, &argc, &argv, &error)) {
    g_print ("%s\nRun '%s --help' to see a full list of available command line options.\n",
        error->message, argv[0]);
    g_warning ("Error in empathy init: %s", error->message);
    return EXIT_FAILURE;
  }

  g_option_context_free (optcontext);

  empathy_gtk_init ();
  g_set_application_name (_(PACKAGE_NAME));
  g_setenv ("PULSE_PROP_media.role", "phone", TRUE);

  gtk_window_set_default_icon_name ("empathy");
  textdomain (GETTEXT_PACKAGE);

#ifdef ENABLE_DEBUG
  /* Set up debug sender */
  debug_sender = tp_debug_sender_dup ();
  g_log_set_default_handler (tp_debug_sender_log_handler, G_LOG_DOMAIN);
#endif

  unique_app = unique_app_new ("org.gnome."PACKAGE_NAME, NULL);

  if (unique_app_is_running (unique_app))
    {
      unique_app_send_message (unique_app, UNIQUE_ACTIVATE, NULL);

      g_object_unref (unique_app);
      return EXIT_SUCCESS;
    }

  notify_init (_(PACKAGE_NAME));

  /* Setting up Idle */
  idle = empathy_idle_dup_singleton ();
  empathy_idle_set_auto_away (idle, TRUE);

  /* Setting up Connectivity */
  connectivity = empathy_connectivity_dup_singleton ();
  use_conn_notify_cb (empathy_conf_get (), EMPATHY_PREFS_USE_CONN,
      connectivity);
  empathy_conf_notify_add (empathy_conf_get (), EMPATHY_PREFS_USE_CONN,
      use_conn_notify_cb, connectivity);

  /* account management */
  account_manager = tp_account_manager_dup ();
  tp_account_manager_prepare_async (account_manager, NULL,
      account_manager_ready_cb, NULL);

  /* Handle channels */
  dispatcher = setup_dispatcher ();
  g_signal_connect (dispatcher, "dispatch", G_CALLBACK (dispatch_cb), NULL);

  migrate_config_to_xdg_dir ();

  /* Setting up UI */
  window = empathy_main_window_show ();
  icon = empathy_status_icon_new (GTK_WINDOW (window), start_hidden);

  g_signal_connect (unique_app, "message-received",
      G_CALLBACK (unique_app_message_cb), window);

  /* Logging */
#ifdef ENABLE_TPL
  log_manager = tpl_log_manager_dup_singleton ();
#else
  log_manager = empathy_log_manager_dup_singleton ();
  empathy_log_manager_observe (log_manager, dispatcher);
#endif

  chatroom_manager = empathy_chatroom_manager_dup_singleton (NULL);
  empathy_chatroom_manager_observe (chatroom_manager, dispatcher);

  g_object_get (chatroom_manager, "ready", &chatroom_manager_ready, NULL);
  if (!chatroom_manager_ready)
    {
      g_signal_connect (G_OBJECT (chatroom_manager), "notify::ready",
          G_CALLBACK (chatroom_manager_ready_cb), account_manager);
    }
  else
    {
      chatroom_manager_ready_cb (chatroom_manager, NULL, account_manager);
    }

  /* Create the call factory */
  call_factory = empathy_call_factory_initialise ();
  g_signal_connect (G_OBJECT (call_factory), "new-call-handler",
      G_CALLBACK (new_call_handler_cb), NULL);
  /* Create the FT factory */
  ft_factory = empathy_ft_factory_dup_singleton ();
  g_signal_connect (ft_factory, "new-ft-handler",
      G_CALLBACK (new_ft_handler_cb), NULL);
  g_signal_connect (ft_factory, "new-incoming-transfer",
      G_CALLBACK (new_incoming_transfer_cb), NULL);

  /* Location mananger */
#if HAVE_GEOCLUE
  location_manager = empathy_location_manager_dup_singleton ();
#endif

  gtk_main ();

  empathy_idle_set_state (idle, TP_CONNECTION_PRESENCE_TYPE_OFFLINE);

#ifdef ENABLE_DEBUG
  g_object_unref (debug_sender);
#endif

  g_object_unref (idle);
  g_object_unref (connectivity);
  g_object_unref (icon);
  g_object_unref (account_manager);
  g_object_unref (log_manager);
  g_object_unref (dispatcher);
  g_object_unref (chatroom_manager);
#if HAVE_GEOCLUE
  g_object_unref (location_manager);
#endif
  g_object_unref (ft_factory);
  g_object_unref (unique_app);

  notify_uninit ();
  xmlCleanupParser ();

  return EXIT_SUCCESS;
}
