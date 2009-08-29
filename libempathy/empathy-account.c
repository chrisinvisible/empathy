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
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/defs.h>

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

#include <glib/gi18n-lib.h>

#include "empathy-account.h"
#include "empathy-account-manager.h"
#include "empathy-utils.h"
#include "empathy-marshal.h"

/* signals */
enum {
  STATUS_CHANGED,
  PRESENCE_CHANGED,
  REMOVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

/* properties */
enum {
  PROP_ENABLED = 1,
  PROP_PRESENCE,
  PROP_STATUS,
  PROP_STATUS_MESSAGE,
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

  TpConnectionStatus connection_status;
  TpConnectionStatusReason reason;

  TpConnectionPresenceType presence;
  gchar *status;
  gchar *message;

  gboolean enabled;
  gboolean valid;
  gboolean ready;
  gboolean removed;
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
    const gchar *path);

static void
empathy_account_init (EmpathyAccount *obj)
{
  EmpathyAccountPriv *priv;

  priv =  G_TYPE_INSTANCE_GET_PRIVATE (obj,
    EMPATHY_TYPE_ACCOUNT, EmpathyAccountPriv);

  obj->priv = priv;

  priv->connection_status = TP_CONNECTION_STATUS_DISCONNECTED;
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
        empathy_account_set_enabled_async (account,
            g_value_get_boolean (value), NULL, NULL);
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
        g_value_set_boolean (value, priv->ready);
        break;
      case PROP_PRESENCE:
        g_value_set_uint (value, priv->presence);
        break;
      case PROP_STATUS:
        g_value_set_string (value, priv->status);
        break;
      case PROP_STATUS_MESSAGE:
        g_value_set_string (value, priv->message);
        break;
      case PROP_CONNECTION_STATUS:
        g_value_set_uint (value, priv->connection_status);
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
        g_value_set_object (value, priv->dbus);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
empathy_account_update (EmpathyAccount *account,
    GHashTable *properties)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);
  GValueArray *arr;
  TpConnectionStatus old_s = priv->connection_status;
  gboolean presence_changed = FALSE;

  if (g_hash_table_lookup (properties, "ConnectionStatus") != NULL)
    priv->connection_status =
      tp_asv_get_int32 (properties, "ConnectionStatus", NULL);

  if (g_hash_table_lookup (properties, "ConnectionStatusReason") != NULL)
    priv->reason = tp_asv_get_int32 (properties,
      "ConnectionStatusReason", NULL);

  if (g_hash_table_lookup (properties, "CurrentPresence") != NULL)
    {
      presence_changed = TRUE;
      arr = tp_asv_get_boxed (properties, "CurrentPresence",
        TP_STRUCT_TYPE_SIMPLE_PRESENCE);
      priv->presence = g_value_get_uint (g_value_array_get_nth (arr, 0));

      g_free (priv->status);
      priv->status = g_value_dup_string (g_value_array_get_nth (arr, 1));

      g_free (priv->message);
      priv->message = g_value_dup_string (g_value_array_get_nth (arr, 2));
    }

  if (g_hash_table_lookup (properties, "DisplayName") != NULL)
    {
      g_free (priv->display_name);
      priv->display_name =
        g_strdup (tp_asv_get_string (properties, "DisplayName"));
      g_object_notify (G_OBJECT (account), "display-name");
    }

  if (g_hash_table_lookup (properties, "Icon") != NULL)
    {
      const gchar *icon_name;

      icon_name = tp_asv_get_string (properties, "Icon");

      g_free (priv->icon_name);

      if (EMP_STR_EMPTY (icon_name))
        priv->icon_name = empathy_protocol_icon_name (priv->proto_name);
      else
        priv->icon_name = g_strdup (icon_name);
    }

  if (g_hash_table_lookup (properties, "Enabled") != NULL)
    {
      gboolean enabled = tp_asv_get_boolean (properties, "Enabled", NULL);
      if (priv->enabled != enabled)
        {
          priv->enabled = enabled;
          g_object_notify (G_OBJECT (account), "enabled");
        }
    }

  if (g_hash_table_lookup (properties, "Valid") != NULL)
    priv->valid = tp_asv_get_boolean (properties, "Valid", NULL);

  if (g_hash_table_lookup (properties, "Parameters") != NULL)
    {
      GHashTable *parameters;

      parameters = tp_asv_get_boxed (properties, "Parameters",
        TP_HASH_TYPE_STRING_VARIANT_MAP);

      if (priv->parameters != NULL)
        g_hash_table_unref (priv->parameters);

      priv->parameters = g_boxed_copy (TP_HASH_TYPE_STRING_VARIANT_MAP,
        parameters);
    }

  if (!priv->ready)
    {
      priv->ready = TRUE;
      g_object_notify (G_OBJECT (account), "ready");
    }

  if (priv->connection_status != old_s)
    {
      if (priv->connection_status == TP_CONNECTION_STATUS_CONNECTED)
        {
          GTimeVal val;
          g_get_current_time (&val);

          priv->connect_time = val.tv_sec;
        }

      g_signal_emit (account, signals[STATUS_CHANGED], 0,
        old_s, priv->connection_status, priv->reason);

      g_object_notify (G_OBJECT (account), "connection-status");
      g_object_notify (G_OBJECT (account), "connection-status-reason");
    }

  if (presence_changed)
    {
      g_signal_emit (account, signals[PRESENCE_CHANGED], 0,
        priv->presence, priv->status, priv->message);
      g_object_notify (G_OBJECT (account), "presence");
      g_object_notify (G_OBJECT (account), "status");
      g_object_notify (G_OBJECT (account), "status-message");
    }

  if (g_hash_table_lookup (properties, "Connection") != NULL)
    {
      const gchar *conn_path =
        tp_asv_get_object_path (properties, "Connection");

      _empathy_account_set_connection (account, conn_path);
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
empathy_account_removed_cb (TpAccount *proxy,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (weak_object);
  EmpathyAccountPriv *priv = GET_PRIV (account);

  if (priv->removed)
    return;

  priv->removed = TRUE;

  g_signal_emit (account, signals[REMOVED], 0);
}

static void
empathy_account_got_all_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (weak_object);

  DEBUG ("Got whole set of properties for %s",
    empathy_account_get_unique_name (account));

  if (error != NULL)
    {
      DEBUG ("Failed to get the initial set of account properties: %s",
        error->message);
      return;
    }

  empathy_account_update (account, properties);
}

static gchar *
empathy_account_unescape_protocol (const gchar *protocol, gssize len)
{
  gchar  *result, *escape;
  /* Bad implementation might accidentally use tp_escape_as_identifier,
   * which escapes - in the wrong way... */
  if ((escape = g_strstr_len (protocol, len, "_2d")) != NULL)
    {
      GString *str;
      const gchar *input;

      str = g_string_new ("");
      input = protocol;
      do {
        g_string_append_len (str, input, escape - input);
        g_string_append_c (str, '-');

        len -= escape - input + 3;
        input = escape + 3;
      } while ((escape = g_strstr_len (input, len, "_2d")) != NULL);

      g_string_append_len (str, input, len);

      result = g_string_free (str, FALSE);
    }
  else
    {
      result = g_strndup (protocol, len);
    }

  g_strdelimit (result, "_", '-');

  return result;
}

static gboolean
empathy_account_parse_unique_name (const gchar *bus_name,
    gchar **protocol, gchar **manager)
{
  const gchar *proto, *proto_end;
  const gchar *cm, *cm_end;

  g_return_val_if_fail (
    g_str_has_prefix (bus_name, TP_ACCOUNT_OBJECT_PATH_BASE), FALSE);

  cm = bus_name + strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

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
    {
      *protocol = empathy_account_unescape_protocol (proto, proto_end - proto);
    }

  if (manager != NULL)
    *manager = g_strndup (cm, cm_end - cm);

  return TRUE;
}

static void
account_invalidated_cb (TpProxy *proxy, guint domain, gint code,
  gchar *message, gpointer user_data)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (user_data);
  EmpathyAccountPriv *priv = GET_PRIV (account);

  if (priv->removed)
    return;

  priv->removed = TRUE;

  g_signal_emit (account, signals[REMOVED], 0);
}

static void
empathy_account_constructed (GObject *object)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (object);
  EmpathyAccountPriv *priv = GET_PRIV (account);

  priv->account = tp_account_new (priv->dbus, priv->unique_name, NULL);

  g_signal_connect (priv->account, "invalidated",
    G_CALLBACK (account_invalidated_cb), object);

  empathy_account_parse_unique_name (priv->unique_name,
    &(priv->proto_name), &(priv->cm_name));

  priv->icon_name = empathy_protocol_icon_name (priv->proto_name);

  tp_cli_account_connect_to_account_property_changed (priv->account,
    empathy_account_properties_changed,
    NULL, NULL, object, NULL);

  tp_cli_account_connect_to_removed (priv->account,
    empathy_account_removed_cb,
    NULL, NULL, object, NULL);

  empathy_account_refresh_properties (account);
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

  g_object_class_install_property (object_class, PROP_STATUS,
    g_param_spec_string ("status",
      "Status",
      "The Status string of the account",
      NULL,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  g_object_class_install_property (object_class, PROP_STATUS_MESSAGE,
    g_param_spec_string ("status-message",
      "status-message",
      "The Status message string of the account",
      NULL,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  g_object_class_install_property (object_class, PROP_CONNECTION_STATUS,
    g_param_spec_uint ("connection-status",
      "ConnectionStatus",
      "The accounts connections status type",
      0,
      NUM_TP_CONNECTION_STATUSES,
      TP_CONNECTION_STATUS_DISCONNECTED,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  g_object_class_install_property (object_class, PROP_CONNECTION_STATUS_REASON,
    g_param_spec_uint ("connection-status-reason",
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
    _empathy_marshal_VOID__UINT_STRING_STRING,
    G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);

  signals[REMOVED] = g_signal_new ("removed",
    G_TYPE_FROM_CLASS (object_class),
    G_SIGNAL_RUN_LAST,
    0, NULL, NULL,
    g_cclosure_marshal_VOID__VOID,
    G_TYPE_NONE, 0);
}

static void
empathy_account_free_connection (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);
  TpConnection *conn;

  if (priv->connection == NULL)
    return;

  conn = priv->connection;
  priv->connection = NULL;

  if (priv->connection_invalidated_id != 0)
    g_signal_handler_disconnect (conn, priv->connection_invalidated_id);
  priv->connection_invalidated_id = 0;

  g_object_unref (conn);
}

void
empathy_account_dispose (GObject *object)
{
  EmpathyAccount *self = EMPATHY_ACCOUNT (object);
  EmpathyAccountPriv *priv = GET_PRIV (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  empathy_account_free_connection (self);

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (empathy_account_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (empathy_account_parent_class)->dispose (object);
}

void
empathy_account_finalize (GObject *object)
{
  EmpathyAccountPriv *priv = GET_PRIV (object);

  g_free (priv->status);
  g_free (priv->message);

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

  if (priv->connection_status != TP_CONNECTION_STATUS_CONNECTED)
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
 * empathy_account_get_connection_for_path:
 * @account: a #EmpathyAccount
 * @patch: the path to connection object for #EmpathyAccount
 *
 * Get the connection of the account on path. This function does not return a
 * new ref. It is not guaranteed that the returned connection object is ready
 *
 * Returns: the connection of the account.
 **/
TpConnection *
empathy_account_get_connection_for_path (EmpathyAccount *account,
  const gchar *path)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  /* double-check that the object path is valid */
  if (!tp_dbus_check_valid_object_path (path, NULL))
    return NULL;

  /* Should be a full object path, not the special "/" value */
  if (strlen (path) == 1)
    return NULL;

  _empathy_account_set_connection (account, path);

  return priv->connection;
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
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->valid;
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

const GHashTable *
empathy_account_get_parameters (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->parameters;
}

gboolean
empathy_account_is_enabled (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->enabled;
}

gboolean
empathy_account_is_ready (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  return priv->ready;
}


EmpathyAccount *
empathy_account_new (TpDBusDaemon *dbus,
    const gchar *unique_name)
{
  return EMPATHY_ACCOUNT (g_object_new (EMPATHY_TYPE_ACCOUNT,
    "dbus-daemon", dbus,
    "unique-name", unique_name,
    NULL));
}

static void
empathy_account_connection_ready_cb (TpConnection *connection,
    const GError *error,
    gpointer user_data)
{
  EmpathyAccount *account = EMPATHY_ACCOUNT (user_data);

  if (error != NULL)
    {
      DEBUG ("(%s) Connection failed to become ready: %s",
        empathy_account_get_unique_name (account), error->message);
      empathy_account_free_connection (account);
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

  empathy_account_free_connection (account);

  g_object_notify (G_OBJECT (account), "connection");
}

static void
_empathy_account_set_connection (EmpathyAccount *account,
    const gchar *path)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  if (priv->connection != NULL)
    {
      const gchar *current;

      current = tp_proxy_get_object_path (priv->connection);
      if (!tp_strdiff (current, path))
        return;
    }

  empathy_account_free_connection (account);

  if (tp_strdiff ("/", path))
    {
      GError *error = NULL;
      priv->connection = tp_connection_new (priv->dbus, NULL, path, &error);

      if (priv->connection == NULL)
        {
          DEBUG ("Failed to create a new TpConnection: %s",
                error->message);
          g_error_free (error);
        }
      else
        {
          priv->connection_invalidated_id = g_signal_connect (priv->connection,
            "invalidated",
            G_CALLBACK (_empathy_account_connection_invalidated_cb), account);

          DEBUG ("Readying connection for %s", priv->unique_name);
          /* notify a change in the connection property when it's ready */
          tp_connection_call_when_ready (priv->connection,
            empathy_account_connection_ready_cb, account);
        }
    }

   g_object_notify (G_OBJECT (account), "connection");
}

static void
account_enabled_set_cb (TpProxy *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    g_simple_async_result_set_from_error (result, (GError *) error);

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

gboolean
empathy_account_set_enabled_finish (EmpathyAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          empathy_account_set_enabled_finish))
    return FALSE;

  return TRUE;
}

void
empathy_account_set_enabled_async (EmpathyAccount *account,
    gboolean enabled,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);
  EmpathyAccountManager *acc_manager;
  GValue value = {0, };
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, empathy_account_set_enabled_finish);
  char *status = NULL;
  char *status_message = NULL;
  TpConnectionPresenceType presence;

  if (priv->enabled == enabled)
    {
      g_simple_async_result_complete_in_idle (result);
      return;
    }

  if (enabled)
    {
      acc_manager = empathy_account_manager_dup_singleton ();
      presence = empathy_account_manager_get_requested_global_presence
	(acc_manager, &status, &status_message);

      if (presence != TP_CONNECTION_PRESENCE_TYPE_UNSET)
	empathy_account_request_presence (account, presence, status,
            status_message);

      g_object_unref (acc_manager);
      g_free (status);
      g_free (status_message);
    }

  g_value_init (&value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&value, enabled);

  tp_cli_dbus_properties_call_set (TP_PROXY (priv->account),
      -1, TP_IFACE_ACCOUNT, "Enabled", &value,
      account_enabled_set_cb, result, NULL, G_OBJECT (account));
}

static void
account_reconnected_cb (TpAccount *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    g_simple_async_result_set_from_error (result, (GError *) error);

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

gboolean
empathy_account_reconnect_finish (EmpathyAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          empathy_account_reconnect_finish))
    return FALSE;

  return TRUE;
}

void
empathy_account_reconnect_async (EmpathyAccount *account,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);

  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (account),
        callback, user_data, empathy_account_reconnect_finish);

  tp_cli_account_call_reconnect (priv->account,
      -1, account_reconnected_cb, result, NULL, G_OBJECT (account));
}

static void
empathy_account_requested_presence_cb (TpProxy *proxy,
  const GError *error,
  gpointer user_data,
  GObject *weak_object)
{
  if (error)
    DEBUG ("Failed to set the requested presence: %s", error->message);
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

static void
empathy_account_updated_cb (TpAccount *proxy,
    const gchar **reconnect_required,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (result, (GError *) error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (G_OBJECT (result));
}

void
empathy_account_update_settings_async (EmpathyAccount *account,
  GHashTable *parameters, const gchar **unset_parameters,
  GAsyncReadyCallback callback, gpointer user_data)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, empathy_account_update_settings_finish);

  tp_cli_account_call_update_parameters (priv->account,
      -1,
      parameters,
      unset_parameters,
      empathy_account_updated_cb,
      result,
      NULL,
      G_OBJECT (account));
}

gboolean
empathy_account_update_settings_finish (EmpathyAccount *account,
  GAsyncResult *result, GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (account), empathy_account_update_settings_finish), FALSE);

  return TRUE;
}

static void
account_display_name_set_cb (TpProxy *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    g_simple_async_result_set_from_error (result, (GError *) error);

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

void
empathy_account_set_display_name_async (EmpathyAccount *account,
    const char *display_name,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;
  GValue value = {0, };
  EmpathyAccountPriv *priv = GET_PRIV (account);

  if (display_name == NULL)
    {
      g_simple_async_report_error_in_idle (G_OBJECT (account),
          callback, user_data, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
          _("Can't set an empty display name"));
      return;
    }

  result = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, empathy_account_set_display_name_finish);

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_string (&value, display_name);

  tp_cli_dbus_properties_call_set (priv->account, -1, TP_IFACE_ACCOUNT,
      "DisplayName", &value, account_display_name_set_cb, result, NULL,
      G_OBJECT (account));
}

gboolean
empathy_account_set_display_name_finish (EmpathyAccount *account,
    GAsyncResult *result, GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          empathy_account_set_display_name_finish))
    return FALSE;

  return TRUE;
}

static void
account_icon_name_set_cb (TpProxy *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    g_simple_async_result_set_from_error (result, (GError *) error);

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

void
empathy_account_set_icon_name_async (EmpathyAccount *account,
    const char *icon_name,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;
  GValue value = {0, };
  EmpathyAccountPriv *priv = GET_PRIV (account);
  const char *icon_name_set;

  if (icon_name == NULL)
    /* settings an empty icon name is allowed */
    icon_name_set = "";
  else
    icon_name_set = icon_name;

  result = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, empathy_account_set_icon_name_finish);

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_string (&value, icon_name_set);

  tp_cli_dbus_properties_call_set (priv->account, -1, TP_IFACE_ACCOUNT,
      "Icon", &value, account_icon_name_set_cb, result, NULL,
      G_OBJECT (account));
}

gboolean
empathy_account_set_icon_name_finish (EmpathyAccount *account,
    GAsyncResult *result, GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          empathy_account_set_icon_name_finish))
    return FALSE;

  return TRUE;
}

static void
empathy_account_remove_cb (TpAccount *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (result, (GError *) error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (G_OBJECT (result));
}

void
empathy_account_remove_async (EmpathyAccount *account,
  GAsyncReadyCallback callback, gpointer user_data)
{
  EmpathyAccountPriv *priv = GET_PRIV (account);
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, empathy_account_remove_finish);

  tp_cli_account_call_remove (priv->account,
      -1,
      empathy_account_remove_cb,
      result,
      NULL,
      G_OBJECT (account));
}

gboolean
empathy_account_remove_finish (EmpathyAccount *account,
  GAsyncResult *result, GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (account), empathy_account_update_settings_finish), FALSE);

  return TRUE;
}

void
empathy_account_refresh_properties (EmpathyAccount *account)
{
  EmpathyAccountPriv *priv;

  g_return_if_fail (EMPATHY_IS_ACCOUNT (account));

  priv = GET_PRIV (account);

  tp_cli_dbus_properties_call_get_all (priv->account, -1,
    TP_IFACE_ACCOUNT,
    empathy_account_got_all_cb,
    NULL,
    NULL,
    G_OBJECT (account));
}

