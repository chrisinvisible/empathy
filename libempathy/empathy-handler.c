/*
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
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#include <config.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/svc-client.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/interfaces.h>

#include "empathy-handler.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_DISPATCHER
#include <libempathy/empathy-debug.h>

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyHandler)
typedef struct
{
  EmpathyHandlerHandleChannelsFunc *handle_channels;
  gpointer handle_channels_user_data;

  EmpathyHandlerChannelsFunc *channels;
  gpointer channels_user_data;

  gchar *name;
  gchar *busname;

  GPtrArray *filters;
  GStrv *capabilities;

  gboolean dispose_run;
} EmpathyHandlerPriv;

static void empathy_handler_client_handler_iface_init (gpointer g_iface,
    gpointer g_iface_data);

G_DEFINE_TYPE_WITH_CODE (EmpathyHandler,
    empathy_handler,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CLIENT, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CLIENT_HANDLER,
      empathy_handler_client_handler_iface_init);
  );

static const gchar *empathy_handler_interfaces[] = {
  TP_IFACE_CLIENT_HANDLER,
  NULL
};

enum
{
  PROP_INTERFACES = 1,
  PROP_CHANNEL_FILTER,
  PROP_CHANNELS,
  PROP_CAPABILITIES,
  PROP_NAME,
};

static GObject *
handler_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  GObject *obj =
    G_OBJECT_CLASS (empathy_handler_parent_class)->constructor
      (type, n_construct_params, construct_params);
  EmpathyHandler *handler = EMPATHY_HANDLER (obj);
  EmpathyHandlerPriv *priv = GET_PRIV (handler);
  TpDBusDaemon *dbus;
  gchar *object_path;

  priv = GET_PRIV (handler);

  priv->busname = g_strdup_printf (TP_CLIENT_BUS_NAME_BASE"%s", priv->name);
  object_path = g_strdup_printf (TP_CLIENT_OBJECT_PATH_BASE"%s",
    priv->name);

  dbus = tp_dbus_daemon_dup (NULL);

  DEBUG ("Registering at %s, %s", priv->busname, object_path);
  g_assert (tp_dbus_daemon_request_name (dbus,
    priv->busname, TRUE, NULL));
  dbus_g_connection_register_g_object (tp_get_bus (),
    object_path, obj);


  g_free (object_path);
  g_object_unref (dbus);

  return G_OBJECT (handler);
}

static void
handler_dispose (GObject *object)
{
  EmpathyHandlerPriv *priv = GET_PRIV (object);
  TpDBusDaemon *dbus;

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  dbus = tp_dbus_daemon_dup (NULL);

  tp_dbus_daemon_release_name (dbus, priv->busname, NULL);

  g_object_unref (dbus);

  if (G_OBJECT_CLASS (empathy_handler_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (empathy_handler_parent_class)->dispose (object);
}

static void
handler_finalize (GObject *object)
{
  EmpathyHandlerPriv *priv = GET_PRIV (object);

  if (priv->filters != NULL)
    g_boxed_free (TP_ARRAY_TYPE_CHANNEL_CLASS_LIST, priv->filters);

  if (priv->capabilities != NULL)
    g_boxed_free (G_TYPE_STRV, priv->capabilities);

  g_free (priv->name);
  g_free (priv->busname);

  if (G_OBJECT_CLASS (empathy_handler_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (empathy_handler_parent_class)->finalize (object);
}

static void
handler_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyHandler *handler = EMPATHY_HANDLER (object);
  EmpathyHandlerPriv *priv = GET_PRIV (handler);

  switch (property_id)
    {
      case PROP_CHANNEL_FILTER:
        priv->filters = g_value_dup_boxed (value);
        if (priv->filters == NULL)
          priv->filters = g_ptr_array_new ();
        break;
      case PROP_CAPABILITIES:
        priv->capabilities = g_value_dup_boxed (value);
        break;
      case PROP_NAME:
        priv->name = g_value_dup_string (value);
        if (EMP_STR_EMPTY (priv->name))
          {
            TpDBusDaemon *bus;

            bus = tp_dbus_daemon_dup (NULL);
            priv->name = g_strdup_printf ("badger_%s_%p",
                tp_dbus_daemon_get_unique_name (bus), object);
            g_strdelimit (priv->name, ":.", '_');
            g_object_unref (bus);
          }
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
handler_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyHandler *self = EMPATHY_HANDLER (object);
  EmpathyHandlerPriv *priv = GET_PRIV (self);

  switch (property_id)
    {
      case PROP_INTERFACES:
        g_value_set_boxed (value, empathy_handler_interfaces);
        break;
      case PROP_CHANNEL_FILTER:
        g_value_set_boxed (value, priv->filters);
        break;
      case PROP_CAPABILITIES:
        g_value_set_boxed (value, priv->capabilities);
        break;
      case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
      case PROP_CHANNELS:
        {
          GList *l, *channels = NULL;
          GPtrArray *array = g_ptr_array_new ();

          if (priv->channels != NULL)
            channels =  priv->channels (self, priv->channels_user_data);

          for (l = channels ; l != NULL; l = g_list_next (l))
            {
              TpProxy *channel = TP_PROXY (l->data);
              g_ptr_array_add (array,
                (gpointer) tp_proxy_get_object_path (channel));
            }
          g_value_set_boxed (value, array);
          g_ptr_array_free (array, TRUE);
          break;
        }
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
empathy_handler_class_init (EmpathyHandlerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  static TpDBusPropertiesMixinPropImpl client_props[] = {
    { "Interfaces", "interfaces", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinPropImpl client_handler_props[] = {
    { "HandlerChannelFilter", "channel-filter", NULL },
    { "HandledChannels", "channels", NULL },
    { "Capabilities", "capabilities", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
    { TP_IFACE_CLIENT,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      client_props
    },
    { TP_IFACE_CLIENT_HANDLER,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      client_handler_props
    },
    { NULL }
  };

  object_class->finalize = handler_finalize;
  object_class->dispose = handler_dispose;
  object_class->constructor = handler_constructor;

  object_class->get_property = handler_get_property;
  object_class->set_property = handler_set_property;

  param_spec = g_param_spec_boxed ("interfaces", "interfaces",
    "Available D-Bus interfaces",
    G_TYPE_STRV,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_boxed ("channel-filter", "channel-filter",
    "Filter for channels this handles",
    TP_ARRAY_TYPE_CHANNEL_CLASS_LIST,
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class,
    PROP_CHANNEL_FILTER, param_spec);

  param_spec = g_param_spec_boxed ("capabilities", "capabilities",
    "Filter for channels this handles",
    G_TYPE_STRV,
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class,
    PROP_CAPABILITIES, param_spec);

  param_spec = g_param_spec_boxed ("channels", "channels",
    "List of channels we're handling",
    EMPATHY_ARRAY_TYPE_OBJECT,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
    PROP_CHANNELS, param_spec);

  param_spec = g_param_spec_string ("name", "name",
    "The local name of the handler",
    NULL,
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class,
    PROP_NAME, param_spec);

  g_type_class_add_private (object_class, sizeof (EmpathyHandlerPriv));

  klass->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
    G_STRUCT_OFFSET (EmpathyHandlerClass, dbus_props_class));
}

static void
empathy_handler_init (EmpathyHandler *handler)
{
  EmpathyHandlerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (handler,
    EMPATHY_TYPE_HANDLER, EmpathyHandlerPriv);

  handler->priv = priv;
}

EmpathyHandler *
empathy_handler_new (const gchar *name,
    GPtrArray *filters,
    GStrv capabilities)
{
  return EMPATHY_HANDLER (
    g_object_new (EMPATHY_TYPE_HANDLER,
      "name", name,
      "channel-filter", filters,
      "capabilities", capabilities,
      NULL));
}

static void
empathy_handler_handle_channels (TpSvcClientHandler *self,
    const gchar *account_path,
    const gchar *connection_path,
    const GPtrArray *channels,
    const GPtrArray *requests_satisfied,
    guint64 timestamp,
    GHashTable *handler_info,
    DBusGMethodInvocation *context)
{
  EmpathyHandler *handler = EMPATHY_HANDLER (self);
  EmpathyHandlerPriv *priv = GET_PRIV (handler);
  GError *error = NULL;

  if (!priv->handle_channels)
    {
      error = g_error_new_literal (TP_ERRORS,
        TP_ERROR_NOT_AVAILABLE,
        "No handler function setup");
      goto error;
    }

  if (!priv->handle_channels (handler, account_path, connection_path,
      channels, requests_satisfied, timestamp, handler_info,
      priv->handle_channels_user_data, &error))
    goto error;

  tp_svc_client_handler_return_from_handle_channels (context);
  return;

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

const gchar *
empathy_handler_get_busname (EmpathyHandler *handler)
{
  EmpathyHandlerPriv *priv = GET_PRIV (handler);

  return priv->busname;
}

static void
empathy_handler_client_handler_iface_init (gpointer g_iface,
    gpointer g_iface_data)
{
  TpSvcClientHandlerClass *klass = (TpSvcClientHandlerClass *) g_iface;

  tp_svc_client_handler_implement_handle_channels (klass,
    empathy_handler_handle_channels);
}

void
empathy_handler_set_handle_channels_func (EmpathyHandler *handler,
    EmpathyHandlerHandleChannelsFunc *func,
    gpointer user_data)
{
  EmpathyHandlerPriv *priv = GET_PRIV (handler);

  priv->handle_channels = func;
  priv->handle_channels_user_data = user_data;
}

void
empathy_handler_set_channels_func (EmpathyHandler *handler,
    EmpathyHandlerChannelsFunc *func,
    gpointer user_data)
{
  EmpathyHandlerPriv *priv = GET_PRIV (handler);

  priv->channels = func;
  priv->channels_user_data = user_data;
}

