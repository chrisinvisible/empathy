/*
 * empathy-account.c - Source for EmpathyAccount
 * Copyright (C) 2009 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/account.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

#include "empathy-account.h"
#include "empathy-utils.h"
#include "empathy-marshal.h"

#define UNIQUE_NAME_PREFIX "/org/freedesktop/Telepathy/Account/"

/* signals */
enum {
  STATUS_CHANGED,
  PRESENCE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

/* properties */
enum {
  PROP_ENABLED = 1,
  PROP_PRESENCE,
  PROP_READY,
  PROP_CONNECTION_STATUS,
  PROP_CONNECTION_STATUS_REASON,
  PROP_CONNECTION,
  PROP_UNIQUE_NAME,
  PROP_DBUS_DAEMON,
  PROP_DISPLAY_NAME
};

G_DEFINE_TYPE(EmpathyAccount, empathy_account, G_TYPE_OBJECT)

/* private structure */
typedef struct _EmpathyAccountPriv EmpathyAccountPriv;

struct _EmpathyAccountPriv
{
  gboolean dispose_has_run;

  TpConnection *connection;
  guint connection_invalidated_id;

  TpConnectionStatus status;
  TpConnectionStatusReason reason;
  TpConnectionPresenceType presence;

  gboolean enabled;
  gboolean valid;
  gboolean ready;
  /* Timestamp when the connection got connected in seconds since the epoch */
  glong connect_time;

  gchar *cm_name;
  gchar *proto_name;
  gchar *icon_name;

  gchar *unique_name;
  gchar *display_name;
  TpDBusDaemon *dbus;

  TpAccount *account;
  GHashTable *parameters;
};

#define GET_PRIV(obj)  EMPATHY_GET_PRIV (obj, EmpathyAccount)

static void _empathy_account_set_connection (EmpathyAccount *account,
    TpConnection *connection);

static void
empathy_account_init (EmpathyAccount *obj)
{
  EmpathyAccountPriv *priv;

  priv =  G_TYPE_INSTANCE_GET_PRIVATE (obj,
    EMPATHY_TYPE_ACCOUNT, EmpathyAccountPriv);

  obj->priv = priv;

  priv->status = TP_CONNECTION_STATUS_DISCONNECTED;
}

static void
empathy_account_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (object);
  EmpathyAccountPriv *priv = GET_PRIV (account);

  switch (prop_id)
    {
      case PROP_ENABLED:
        empathy_account_set_enabled (account, g_value_get_boolean (value));
        break;
      case PROP_UNIQUE_NAME:
        priv->unique_name = g_value_dup_string (value);
        break;
      case PROP_DBUS_DAEMON:
        priv->dbus = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
empathy_account_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (object);
  EmpathyAccountPriv *priv = GET_PRIV (account);

  switch (prop_id)
    {
      case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
      case PROP_READY:
        g_value_set_boolean (value, priv->enabled);
        break;
      case PROP_PRESENCE:
        g_value_set_uint (value, priv->presence);
        break;
      case PROP_CONNECTION_STATUS:
        g_value_set_uint (value, priv->status);
        break;
      case PROP_CONNECTION_STATUS_REASON:
        g_value_set_uint (value, priv->reason);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value,
            empathy_account_get_connection (account));
        break;
      case PROP_UNIQUE_NAME:
        g_value_set_string (value,
            empathy_account_get_unique_name (account));
        break;
      case PROP_DISPLAY_NAME:
        g_value_set_string (value,
            empathy_account_get_display_name (account));
        break;
      case PROP_DBUS_DAEMON:
        g_value_set_string (value,
            empathy_account_get_display_name (account));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
empathy_account_update (EmpathyAccount *account, GHashTable *properties)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);
  const gchar *conn_path;
  GValueArray *arr;
  TpConnectionStatus old_s = priv->status;
  TpConnectionPresenceType old_p = priv->presence;

  if (g_hash_table_lookup (properties, "ConnectionStatus") != NULL)
    priv->status = tp_asv_get_int32 (properties, "ConnectionStatus", NULL);

  if (g_hash_table_lookup (properties, "ConnectionStatusReason") != NULL)
    priv->reason = tp_asv_get_int32 (properties,
      "ConnectionStatusReason", NULL);

  if (g_hash_table_lookup (properties, "CurrentPresence") != NULL)
    {
      arr = tp_asv_get_boxed (properties, "CurrentPresence",
        TP_STRUCT_TYPE_SIMPLE_PRESENCE);
      priv->presence = g_value_get_uint (g_value_array_get_nth (arr, 0));
    }

  if (g_hash_table_lookup (properties, "DisplayName") != NULL)
    priv->display_name =
      g_strdup (tp_asv_get_string (properties, "DisplayName"));

  if (g_hash_table_lookup (properties, "Enabled") != NULL)
    empathy_account_set_enabled (account,
      tp_asv_get_boolean (properties, "Enabled", NULL));

  if (g_hash_table_lookup (properties, "Valid") != NULL)
    priv->valid = tp_asv_get_boolean (properties, "Valid", NULL);

  if (g_hash_table_lookup (properties, "Parameters") != NULL)
    {
      GHashTable *parameters;

      parameters = tp_asv_get_boxed (properties, "Parameters",
        TP_HASH_TYPE_STRING_VARIANT_MAP);

      priv->parameters = g_boxed_copy (TP_HASH_TYPE_STRING_VARIANT_MAP,
        parameters);
    }

  if (!priv->ready)
    {
      priv->ready = TRUE;
      g_object_notify (G_OBJECT (account), "ready");
    }

  if (priv->status != old_s)
    {
      if (priv->status == TP_CONNECTION_STATUS_CONNECTED)
        {
          GTimeVal val;
          g_get_current_time (&val);

          priv->connect_time = val.tv_sec;
        }

      g_signal_emit (account, signals[STATUS_CHANGED], 0,
        old_s, priv->status, priv->reason);

      g_object_notify (G_OBJECT (account), "status");
    }

  if (priv->presence != old_p)
    {
      g_signal_emit (account, signals[PRESENCE_CHANGED], 0,
        old_p, priv->presence);
      g_object_notify (G_OBJECT (account), "presence");
    }

  if (g_hash_table_lookup (properties, "Connection") != NULL)
    {
      conn_path = tp_asv_get_object_path (properties, "Connection");

      if (tp_strdiff (conn_path, "/") && priv->connection == NULL)
        {
          TpConnection *conn;
          GError *error = NULL;
          conn = tp_connection_new (priv->dbus, NULL, conn_path, &error);

          if (conn == NULL)
            {
              DEBUG ("Failed to create a new TpConnection: %s",
                error->message);
              g_error_free (error);
            }

          _empathy_account_set_connection (account, conn);
        }
    }
}

static void
empathy_account_properties_changed (TpAccount *proxy,
    GHashTable *properties,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (weak_object);
  EmpathyAccountPriv *priv = GET_PRIV (account);

  if (!priv->ready)
    return;

  empathy_account_update (account, properties);
}

static void
empathy_account_got_all_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (weak_object);

  if (error != NULL)
    {
      printf ("Unhappy\n");
      return;
    }

  empathy_account_update (account, properties);
}

static gboolean
empathy_account_parse_unique_name (const gchar *bus_name,
  gchar **protocol, gchar **manager)
{
  const gchar *proto, *proto_end;
  const gchar *cm, *cm_end;

  g_return_val_if_fail (
    g_str_has_prefix (bus_name, UNIQUE_NAME_PREFIX), FALSE);

  cm = bus_name + strlen (UNIQUE_NAME_PREFIX);

  for (cm_end = cm; *cm_end != '/' && *cm_end != '\0'; cm_end++)
    /* pass */;

  if (*cm_end == '\0')
    return FALSE;

  if (cm_end == '\0')
    return FALSE;

  proto = cm_end + 1;

  for (proto_end = proto; *proto_end != '/' && *proto_end != '\0'; proto_end++)
    /* pass */;

  if (*proto_end == '\0')
    return FALSE;

  if (protocol != NULL)
    *protocol = g_strndup (proto, proto_end - proto);

  if (manager != NULL)
    *manager = g_strndup (cm, cm_end - cm);

  return TRUE;
}

static void
empathy_account_constructed (GObject *object)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (object);
  EmpathyAccountPriv *priv = GET_PRIV (account);

  priv->account = tp_account_new (priv->dbus, priv->unique_name, NULL);

  empathy_account_parse_unique_name (priv->unique_name,
    &(priv->proto_name), &(priv->cm_name));

  priv->icon_name = g_strdup_printf ("im-%s", priv->proto_name);

  tp_cli_account_connect_to_account_property_changed (priv->account,
    empathy_account_properties_changed,
    NULL, NULL, object, NULL);

  tp_cli_dbus_properties_call_get_all (priv->account, -1,
    TP_IFACE_ACCOUNT,
    empathy_account_got_all_cb,
    NULL,
    NULL,
    G_OBJECT (account));
}

static void empathy_account_dispose (GObject *object);
static void empathy_account_finalize (GObject *object);

static void
empathy_account_class_init (EmpathyAccountClass *empathy_account_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (empathy_account_class);

  g_type_class_add_private (empathy_account_class,
    sizeof (EmpathyAccountPriv));

  object_class->set_property = empathy_account_set_property;
  object_class->get_property = empathy_account_get_property;
  object_class->dispose = empathy_account_dispose;
  object_class->finalize = empathy_account_finalize;
  object_class->constructed = empathy_account_constructed;

  g_object_class_install_property (object_class, PROP_ENABLED,
    g_param_spec_boolean ("enabled",
      "Enabled",
      "Whether this account is enabled or not",
      FALSE,
      G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_READY,
    g_param_spec_boolean ("ready",
      "Ready",
      "Whether this account is ready to be used",
      FALSE,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  g_object_class_install_property (object_class, PROP_PRESENCE,
    g_param_spec_uint ("presence",
      "Presence",
      "The account connections presence type",
      0,
      NUM_TP_CONNECTION_PRESENCE_TYPES,
      TP_CONNECTION_PRESENCE_TYPE_UNSET,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  g_object_class_install_property (object_class, PROP_CONNECTION_STATUS,
    g_param_spec_uint ("status",
      "ConnectionStatus",
      "The accounts connections status type",
      0,
      NUM_TP_CONNECTION_STATUSES,
      TP_CONNECTION_STATUS_DISCONNECTED,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  g_object_class_install_property (object_class, PROP_CONNECTION_STATUS_REASON,
    g_param_spec_uint ("status-reason",
      "ConnectionStatusReason",
      "The account connections status reason",
      0,
      NUM_TP_CONNECTION_STATUS_REASONS,
      TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  g_object_class_install_property (object_class, PROP_CONNECTION,
    g_param_spec_object ("connection",
      "Connection",
      "The accounts connection",
      TP_TYPE_CONNECTION,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  g_object_class_install_property (object_class, PROP_UNIQUE_NAME,
    g_param_spec_string ("unique-name",
      "UniqueName",
      "The accounts unique name",
      NULL,
      G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class, PROP_DBUS_DAEMON,
    g_param_spec_object ("dbus-daemon",
      "dbus-daemon",
      "The Tp Dbus daemon on which this account exists",
      TP_TYPE_DBUS_DAEMON,
      G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class, PROP_DISPLAY_NAME,
    g_param_spec_string ("display-name",
      "DisplayName",
      "The accounts display name",
      NULL,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  signals[STATUS_CHANGED] = g_signal_new ("status-changed",
    G_TYPE_FROM_CLASS (object_class),
    G_SIGNAL_RUN_LAST,
    0, NULL, NULL,
    _empathy_marshal_VOID__UINT_UINT_UINT,
    G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

  signals[PRESENCE_CHANGED] = g_signal_new ("presence-changed",
    G_TYPE_FROM_CLASS (object_class),
    G_SIGNAL_RUN_LAST,
    0, NULL, NULL,
    _empathy_marshal_VOID__UINT_UINT,
    G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
}

void
empathy_account_dispose (GObject *object)
{
  EmpathyAccount *self = EMPATHY_ACCOUNT (object);
  EmpathyAccountPriv *priv = GET_PRIV (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->connection_invalidated_id != 0)
    g_signal_handler_disconnect (priv->connection,
        priv->connection_invalidated_id);
  priv->connection_invalidated_id = 0;

  if (priv->connection != NULL)
    g_object_unref (priv->connection);
  priv->connection = NULL;

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (empathy_account_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (empathy_account_parent_class)->dispose (object);
}

void
empathy_account_finalize (GObject *object)
{
  EmpathyAccountPriv *priv = GET_PRIV (object);

  g_free (priv->cm_name);
  g_free (priv->proto_name);
  g_free (priv->icon_name);
  g_free (priv->display_name);

  /* free any data held directly by the object here */
  if (G_OBJECT_CLASS (empathy_account_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (empathy_account_parent_class)->finalize (object);
}

gboolean
empathy_account_is_just_connected (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);
  GTimeVal val;

  if (priv->status != TP_CONNECTION_STATUS_CONNECTED)
    return FALSE;

  g_get_current_time (&val);

  return (val.tv_sec - priv->connect_time) < 10;
}

/**
 * empathy_account_get_connection:
 * @account: a #EmpathyAccount
 *
 * Get the connection of the account, or NULL if account is offline or the
 * connection is not yet ready. This function does not return a new ref.
 *
 * Returns: the connection of the account.
 **/
TpConnection *
empathy_account_get_connection (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  if (priv->connection != NULL &&
      tp_connection_is_ready (priv->connection))
    return priv->connection;

  return NULL;
}

/**
 * empathy_account_get_unique_name:
 * @account: a #EmpathyAccount
 *
 * Returns: the unique name of the account.
 **/
const gchar *
empathy_account_get_unique_name (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->unique_name;
}

/**
 * empathy_account_get_display_name:
 * @account: a #EmpathyAccount
 *
 * Returns: the display name of the account.
 **/
const gchar *
empathy_account_get_display_name (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->display_name;
}

gboolean
empathy_account_is_valid (EmpathyAccount *account)
{
  //EmpathyAccountPriv *priv = GET_PRIV (account);

  /* FIXME */
  return FALSE;
}

const gchar *
empathy_account_get_connection_manager (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->cm_name;
}

const gchar *
empathy_account_get_protocol (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->proto_name;
}

const gchar *
empathy_account_get_icon_name (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->icon_name;
}

gboolean
empathy_account_is_enabled (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->enabled;
}

void
empathy_account_unset_param (EmpathyAccount *account, const gchar *param)
{
  //EmpathyAccountPriv *priv = GET_PRIV (account);

  //mc_account_unset_param (priv->mc_account, param);
}

const gchar *
empathy_account_get_param_string (EmpathyAccount *account, const gchar *param)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return tp_asv_get_string (priv->parameters, param);
}

gint
empathy_account_get_param_int (EmpathyAccount *account, const gchar *param)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return tp_asv_get_int32 (priv->parameters, param, NULL);
}

gboolean
empathy_account_get_param_boolean (EmpathyAccount *account, const gchar *param)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return tp_asv_get_boolean (priv->parameters, param, NULL);
}

void
empathy_account_set_param_string (EmpathyAccount *account,
  const gchar *param,
  const gchar *value)
{
  //EmpathyAccountPriv *priv = GET_PRIV (account);
  //mc_account_set_param_string (priv->mc_account, param, value);
}

void
empathy_account_set_param_int (EmpathyAccount *account,
  const gchar *param,
  gint value)
{
  //EmpathyAccountPriv *priv = GET_PRIV (account);
  //mc_account_set_param_int (priv->mc_account, param, value);
}

void
empathy_account_set_param_boolean (EmpathyAccount *account,
  const gchar *param,
  gboolean value)
{
  //EmpathyAccountPriv *priv = GET_PRIV (account);
  //mc_account_set_param_boolean (priv->mc_account, param, value);
}

void
empathy_account_set_display_name (EmpathyAccount *account,
    const gchar *display_name)
{
  //EmpathyAccountPriv *priv = GET_PRIV (account);
  //mc_account_set_display_name (priv->mc_account, display_name);
}


EmpathyAccount *
empathy_account_new (TpDBusDaemon *dbus, const gchar *unique_name)
{
  return EMPATHY_ACCOUNT (g_object_new (EMPATHY_TYPE_ACCOUNT,
    "dbus-daemon", dbus,
    "unique-name", unique_name,
    NULL));
}

#if 0
EmpathyAccount *
_empathy_account_new (McAccount *mc_account)
{
  EmpathyAccount *account;
  EmpathyAccountPriv *priv;
  McProfile *profile;
  McProtocol *protocol;


  account = g_object_new (EMPATHY_TYPE_ACCOUNT, NULL);
  priv = GET_PRIV (account);
  priv->mc_account = mc_account;

  profile =  mc_account_get_profile (mc_account);
  protocol = mc_profile_get_protocol (profile);

  if (protocol != NULL)
    {
      McManager *manager = mc_protocol_get_manager (protocol);

      priv->proto_name = g_strdup (mc_protocol_get_name (protocol));
      priv->cm_name = g_strdup (mc_manager_get_unique_name (manager));

      g_object_unref (protocol);
      g_object_unref (manager);
    }
  g_object_unref (profile);

  return account;
}

void
_empathy_account_set_status (EmpathyAccount *account,
    TpConnectionStatus status,
    TpConnectionStatusReason reason,
    TpConnectionPresenceType presence)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);
  TpConnectionStatus old_s = priv->status;
  TpConnectionPresenceType old_p = priv->presence;

  priv->status = status;
  priv->presence = presence;

}
#endif

static void
empathy_account_connection_ready_cb (TpConnection *connection,
    const GError *error,
    gpointer user_data)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (user_data);
  EmpathyAccountPriv *priv = GET_PRIV (account);

  if (error != NULL)
    {
      DEBUG ("(%s) Connection failed to become ready: %s",
        empathy_account_get_unique_name (account), error->message);
      priv->connection = NULL;
    }
  else
    {
      DEBUG ("(%s) Connection ready",
        empathy_account_get_unique_name (account));
      g_object_notify (G_OBJECT (account), "connection");
    }
}

static void
_empathy_account_connection_invalidated_cb (TpProxy *self,
  guint    domain,
  gint     code,
  gchar   *message,
  gpointer user_data)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (user_data);
  EmpathyAccountPriv *priv = GET_PRIV (account);

  if (priv->connection == NULL)
    return;

  DEBUG ("(%s) Connection invalidated",
    empathy_account_get_unique_name (account));

  g_assert (priv->connection == TP_CONNECTION (self));

  g_signal_handler_disconnect (priv->connection,
    priv->connection_invalidated_id);
  priv->connection_invalidated_id = 0;

  g_object_unref (priv->connection);
  priv->connection = NULL;

  g_object_notify (G_OBJECT (account), "connection");
}

static void
_empathy_account_set_connection (EmpathyAccount *account,
    TpConnection *connection)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  if (priv->connection == connection)
    return;

  /* Connection already set, don't set the new one */
  if (connection != NULL && priv->connection != NULL)
    return;

  if (connection == NULL)
    {
      g_signal_handler_disconnect (priv->connection,
        priv->connection_invalidated_id);
      priv->connection_invalidated_id = 0;

      g_object_unref (priv->connection);
      priv->connection = NULL;
      g_object_notify (G_OBJECT (account), "connection");
    }
  else
    {
      priv->connection = g_object_ref (connection);
      priv->connection_invalidated_id = g_signal_connect (priv->connection,
          "invalidated",
          G_CALLBACK (_empathy_account_connection_invalidated_cb),
          account);

      DEBUG ("Readying connection for %s", priv->unique_name);
      /* notify a change in the connection property when it's ready */
      tp_connection_call_when_ready (priv->connection,
        empathy_account_connection_ready_cb, account);
    }
}

void
empathy_account_set_enabled (EmpathyAccount *account,
    gboolean enabled)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  if (priv->enabled == enabled)
    return;

  priv->enabled = enabled;
  g_object_notify (G_OBJECT (account), "enabled");
}

static void
empathy_account_requested_presence_cb (TpProxy *proxy,
  const GError *error,
  gpointer user_data,
  GObject *weak_object)
{
  if (error)
    DEBUG (":( : %s", error->message);
}


void
empathy_account_request_presence (EmpathyAccount *account,
  TpConnectionPresenceType type,
  const gchar *status,
  const gchar *message)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);
  GValue value = {0, };
  GValueArray *arr;

  g_value_init (&value, TP_STRUCT_TYPE_SIMPLE_PRESENCE);
  g_value_take_boxed (&value, dbus_g_type_specialized_construct
    (TP_STRUCT_TYPE_SIMPLE_PRESENCE));
  arr = (GValueArray *) g_value_get_boxed (&value);

  g_value_set_uint (arr->values, type);
  g_value_set_static_string (arr->values + 1, status);
  g_value_set_static_string (arr->values + 2, message);

  tp_cli_dbus_properties_call_set (TP_PROXY (priv->account),
    -1,
    TP_IFACE_ACCOUNT,
    "RequestedPresence",
    &value,
    empathy_account_requested_presence_cb,
    NULL,
    NULL,
    G_OBJECT (account));

  g_value_unset (&value);
}

#if 0
McAccount *
_empathy_account_get_mc_account (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->mc_account;
}
#endif
