/*
 * © 2009, Collabora Ltd
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
 * Authors: Arnaud Maillet <arnaud.maillet@collabora.co.uk>
 */

#include <gtk/gtk.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/contact.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/interfaces.h>
#define DEBUG_FLAG EMPATHY_DEBUG_SHARE_DESKTOP
#include <libempathy/empathy-debug.h>

#include "empathy-share-my-desktop.h"

#define DBUS_SERVICE "org.gnome.Vino"
#define DBUS_INTERFACE "org.gnome.VinoScreen"

typedef struct  {
  TpContact *contact;
  TpChannel *channel;
  gulong signal_invalidated_id;
} EmpathyShareMyDesktopPrivate;


static void
empathy_share_my_desktop_tube_invalidated (TpProxy *channel,
    guint domain,
    gint code,
    gchar *message,
    gpointer object)
{
  EmpathyShareMyDesktopPrivate *data = (EmpathyShareMyDesktopPrivate *) object;

  DEBUG ("Tube is invalidated");

  g_signal_handler_disconnect (G_OBJECT (data->channel),
               data->signal_invalidated_id);

  if (data->channel != NULL)
    {
      g_object_unref (data->channel);
      data->channel = NULL;
    }

  g_slice_free (EmpathyShareMyDesktopPrivate, data);
}

static void
empathy_share_my_desktop_channel_ready (TpChannel *channel,
    const GError *error_failed,
    gpointer object)
{
  EmpathyShareMyDesktopPrivate *data = (EmpathyShareMyDesktopPrivate *) object;
  TpConnection *connection;
  gchar * connection_path;
  gchar * tube_path;
  DBusGConnection *dbus_g_connection;
  GHashTable *channel_properties;
  DBusGProxy *proxy;
  GError *error = NULL;
  GdkScreen *screen;
  gchar *obj_path;
  GtkWidget *window;

  if (channel == NULL)
  {
      DEBUG ("The channel is not ready: %s", error_failed->message);
      return;
  }

  data->channel = channel;

  data->signal_invalidated_id = g_signal_connect (G_OBJECT (channel),
      "invalidated", G_CALLBACK (empathy_share_my_desktop_tube_invalidated),
      data);

  dbus_g_connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);

  if (dbus_g_connection == NULL)
    {
      DEBUG ("Failed to open connection to bus: %s", error->message);
      g_clear_error (&error);
      return;
    }

  screen = gdk_screen_get_default ();
  obj_path = g_strdup_printf ("/org/gnome/vino/screens/%d",
      gdk_screen_get_number (screen));

  proxy = dbus_g_proxy_new_for_name (dbus_g_connection, DBUS_SERVICE,
      obj_path, DBUS_INTERFACE);

  connection = tp_channel_borrow_connection (channel);

  g_object_get (connection, "object-path", &connection_path, NULL);

  DEBUG ("connection path : %s", connection_path);

  g_object_get (channel, "object-path", &tube_path, "channel-properties",
      &channel_properties, NULL);

  DEBUG ("tube path : %s", tube_path);

  if (!dbus_g_proxy_call (proxy, "ShareWithTube", &error,
      DBUS_TYPE_G_OBJECT_PATH, connection_path,
      DBUS_TYPE_G_OBJECT_PATH, tube_path,
      dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
      channel_properties,
      G_TYPE_INVALID, G_TYPE_INVALID))
    {
      window = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
          GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
          "Vino doesn't support telepathy");
      gtk_dialog_run (GTK_DIALOG (window));
      gtk_widget_destroy (window);
      DEBUG ("Failed to request name: %s",
          error ? error->message : "No error given");
      g_clear_error (&error);
    }

  g_hash_table_unref (channel_properties);
  g_free (connection_path);
  g_free (tube_path);
  g_free (obj_path);
  g_object_unref (proxy);
}

static void
empathy_share_my_desktop_create_channel_cb (TpConnection *connection,
    const gchar *object_path,
    GHashTable *channel_properties,
    const GError *error_failed,
    gpointer user_data,
    GObject *object)
{
  EmpathyShareMyDesktopPrivate *data = (EmpathyShareMyDesktopPrivate *)
      user_data;

  TpChannel *channel;
  GError *error = NULL;

  if (object_path == NULL)
  {
      DEBUG ("CreateChannel failed: %s", error_failed->message);
      return;
  }

  DEBUG ("Offering a new stream tube");

  channel = tp_channel_new_from_properties (connection, object_path,
      channel_properties, &error);

  if (channel == NULL)
    {
      DEBUG ("Error requesting channel: %s", error->message);
      g_clear_error (&error);
      return;
    }

  tp_channel_call_when_ready (channel,
      empathy_share_my_desktop_channel_ready, data);
}

static void
empathy_share_my_desktop_connection_ready (TpConnection *connection,
    const GError *error,
    gpointer object)
{
  EmpathyShareMyDesktopPrivate *data = (EmpathyShareMyDesktopPrivate *) object;
  GHashTable *request;
  GValue *value;

  if (connection == NULL)
    {
      DEBUG ("The connection is not ready: %s", error->message);
      return;
    }

  request = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  /* org.freedesktop.Telepathy.Channel.ChannelType */
  value = tp_g_value_slice_new_static_string
      (TP_IFACE_CHANNEL_TYPE_STREAM_TUBE);
  g_hash_table_insert (request, TP_IFACE_CHANNEL ".ChannelType", value);

  /* org.freedesktop.Telepathy.Channel.TargetHandleType */
  value = tp_g_value_slice_new_uint (TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (request, TP_IFACE_CHANNEL ".TargetHandleType", value);

  /* org.freedesktop.Telepathy.Channel.TargetHandleType */
  value = tp_g_value_slice_new_uint (tp_contact_get_handle
      (data->contact));
  g_hash_table_insert (request, TP_IFACE_CHANNEL ".TargetHandle", value);

  /* org.freedesktop.Telepathy.Channel.Type.StreamTube.Service */
  value = tp_g_value_slice_new_static_string ("rfb");
  g_hash_table_insert (request,
      TP_IFACE_CHANNEL_TYPE_STREAM_TUBE  ".Service",
      value);

  tp_cli_connection_interface_requests_call_create_channel
      (connection, -1, request, empathy_share_my_desktop_create_channel_cb,
      data, NULL, NULL);

  g_hash_table_destroy (request);
}

void
empathy_share_my_desktop_share_with_contact (EmpathyContact *contact)
{
  TpConnection *connection;
  EmpathyShareMyDesktopPrivate *data;
  data = g_slice_new (EmpathyShareMyDesktopPrivate);
  data->contact = empathy_contact_get_tp_contact (contact);

  DEBUG ("Creation of ShareMyDesktop");

  if (!TP_IS_CONTACT (data->contact))
    {
      DEBUG ("It's not a tp contact");
      return;
    }

  connection = tp_contact_get_connection (data->contact);

  tp_connection_call_when_ready (connection,
      empathy_share_my_desktop_connection_ready, data);
}
