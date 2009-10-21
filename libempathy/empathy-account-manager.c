/*
 * Copyright (C) 2008 Collabora Ltd.
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
 * Authors: Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 *          Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 */

#include "config.h"

#include <telepathy-glib/util.h>
#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#include "empathy-account-manager.h"
#include "empathy-marshal.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyAccountManager)

#define MC5_BUS_NAME "org.freedesktop.Telepathy.MissionControl5"

typedef struct {
  /* (owned) unique name -> (reffed) EmpathyAccount */
  GHashTable       *accounts;
  int               connected;
  int               connecting;
  gboolean          dispose_run;
  gboolean          ready;
  TpAccountManager *tp_manager;
  TpDBusDaemon *dbus;

  /* global presence */
  EmpathyAccount *global_account;

  TpConnectionPresenceType global_presence;
  gchar *global_status;
  gchar *global_status_message;

  /* requested global presence, could be different
   * from the actual global one.
   */
  TpConnectionPresenceType requested_presence;
  gchar *requested_status;
  gchar *requested_status_message;

  GHashTable *create_results;
} EmpathyAccountManagerPriv;

enum {
  ACCOUNT_CREATED,
  ACCOUNT_DELETED,
  ACCOUNT_ENABLED,
  ACCOUNT_DISABLED,
  ACCOUNT_CHANGED,
  ACCOUNT_CONNECTION_CHANGED,
  GLOBAL_PRESENCE_CHANGED,
  NEW_CONNECTION,
  LAST_SIGNAL
};

enum {
  PROP_READY = 1,
};

static guint signals[LAST_SIGNAL];
static EmpathyAccountManager *manager_singleton = NULL;

G_DEFINE_TYPE (EmpathyAccountManager, empathy_account_manager, G_TYPE_OBJECT);

static void
emp_account_connection_cb (EmpathyAccount *account,
  GParamSpec *spec,
  gpointer manager)
{
  TpConnection *connection = empathy_account_get_connection (account);

  DEBUG ("Signalling connection %p of account %s",
      connection, empathy_account_get_unique_name (account));

  if (connection != NULL)
    g_signal_emit (manager, signals[NEW_CONNECTION], 0, connection);
}

static void
emp_account_enabled_cb (EmpathyAccount *account,
  GParamSpec *spec,
  gpointer manager)
{
  if (empathy_account_is_enabled (account))
    g_signal_emit (manager, signals[ACCOUNT_ENABLED], 0, account);
  else
    g_signal_emit (manager, signals[ACCOUNT_DISABLED], 0, account);
}

static void
emp_account_status_changed_cb (EmpathyAccount *account,
  TpConnectionStatus old,
  TpConnectionStatus new,
  TpConnectionStatusReason reason,
  gpointer user_data)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (user_data);
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);

  switch (old)
    {
      case TP_CONNECTION_STATUS_CONNECTING:
        priv->connecting--;
        break;
      case TP_CONNECTION_STATUS_CONNECTED:
        priv->connected--;
        break;
      default:
        break;
    }

  switch (new)
    {
      case TP_CONNECTION_STATUS_CONNECTING:
        priv->connecting++;
        break;
      case TP_CONNECTION_STATUS_CONNECTED:
        priv->connected++;
        break;
      default:
        break;
    }

  g_signal_emit (manager, signals[ACCOUNT_CONNECTION_CHANGED], 0,
    account, reason, new, old);
}

static void
emp_account_manager_update_global_presence (EmpathyAccountManager *manager)
{
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);
  TpConnectionPresenceType presence = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;
  EmpathyAccount *account = NULL;
  GHashTableIter iter;
  gpointer value;

  /* Make the global presence is equal to the presence of the account with the
   * highest availability */

  g_hash_table_iter_init (&iter, priv->accounts);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      EmpathyAccount *a = EMPATHY_ACCOUNT (value);
      TpConnectionPresenceType p;

      g_object_get (a, "presence", &p, NULL);

      if (tp_connection_presence_type_cmp_availability (p, presence) > 0)
        {
          account = a;
          presence = p;
        }
    }

  priv->global_account = account;
  g_free (priv->global_status);
  g_free (priv->global_status_message);

  if (account == NULL)
    {
      priv->global_presence = presence;
      priv->global_status = NULL;
      priv->global_status_message = NULL;
      return;
    }

  g_object_get (account,
    "presence", &priv->global_presence,
    "status", &priv->global_status,
    "status-message", &priv->global_status_message,
    NULL);

  DEBUG ("Updated global presence to: %s (%d) \"%s\"",
    priv->global_status, priv->global_presence, priv->global_status_message);
}

static void
emp_account_presence_changed_cb (EmpathyAccount *account,
  TpConnectionPresenceType presence,
  const gchar *status,
  const gchar *status_message,
  gpointer user_data)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (user_data);
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);

  if (tp_connection_presence_type_cmp_availability (presence,
      priv->global_presence) > 0)
    {
      priv->global_account = account;

      priv->global_presence = presence;

      g_free (priv->global_status);
      priv->global_status = g_strdup (status);

      g_free (priv->global_status_message);
      priv->global_status_message = g_strdup (status_message);

      goto signal;
    }
  else if (priv->global_account == account)
    {
      emp_account_manager_update_global_presence (manager);
      goto signal;
    }

  return;
signal:
    g_signal_emit (manager, signals[GLOBAL_PRESENCE_CHANGED], 0,
      priv->global_presence, priv->global_status, priv->global_status_message);
}

static void
emp_account_removed_cb (EmpathyAccount *account, gpointer user_data)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (user_data);
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);

  g_object_ref (account);
  g_hash_table_remove (priv->accounts,
    empathy_account_get_unique_name (account));

  g_signal_emit (manager, signals[ACCOUNT_DELETED], 0, account);
  g_object_unref (account);
}

static void
empathy_account_manager_check_ready (EmpathyAccountManager *manager)
{
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);
  GHashTableIter iter;
  gpointer value;

  if (priv->ready)
    return;

  g_hash_table_iter_init (&iter, priv->accounts);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      EmpathyAccount *account = EMPATHY_ACCOUNT (value);
      gboolean ready;

      g_object_get (account, "ready", &ready, NULL);

      if (!ready)
        return;
    }

  /* Rerequest global presence on the initial set of accounts for cases where a
   * global presence was requested before the manager was ready */
  if (priv->requested_presence != TP_CONNECTION_PRESENCE_TYPE_UNSET)
    empathy_account_manager_request_global_presence (manager,
      priv->requested_presence,
      priv->requested_status,
      priv->requested_status_message);

  priv->ready = TRUE;
  g_object_notify (G_OBJECT (manager), "ready");
}

static void
account_manager_account_ready_cb (GObject *obj,
    GParamSpec *spec,
    gpointer user_data)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (user_data);
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);
  EmpathyAccount *account = EMPATHY_ACCOUNT (obj);
  GSimpleAsyncResult *result;
  gboolean ready;

  g_object_get (account, "ready", &ready, NULL);

  if (!ready)
    return;

  /* see if there's any pending callbacks for this account */
  result = g_hash_table_lookup (priv->create_results, account);
  if (result != NULL)
    {
      g_simple_async_result_set_op_res_gpointer (
          G_SIMPLE_ASYNC_RESULT (result), account, NULL);

      g_simple_async_result_complete (result);

      g_hash_table_remove (priv->create_results, account);
      g_object_unref (result);
    }

  g_signal_emit (manager, signals[ACCOUNT_CREATED], 0, account);

  g_signal_connect (account, "notify::connection",
    G_CALLBACK (emp_account_connection_cb), manager);

  g_signal_connect (account, "notify::enabled",
    G_CALLBACK (emp_account_enabled_cb), manager);

  g_signal_connect (account, "status-changed",
    G_CALLBACK (emp_account_status_changed_cb), manager);

  g_signal_connect (account, "presence-changed",
    G_CALLBACK (emp_account_presence_changed_cb), manager);

  g_signal_connect (account, "removed",
    G_CALLBACK (emp_account_removed_cb), manager);

  empathy_account_manager_check_ready (manager);
}

EmpathyAccount *
empathy_account_manager_get_account (EmpathyAccountManager *manager,
  const gchar *path)
{
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);

  return g_hash_table_lookup (priv->accounts, path);
}

EmpathyAccount *
empathy_account_manager_ensure_account (EmpathyAccountManager *manager,
  const gchar *path)
{
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);
  EmpathyAccount *account;

  account = g_hash_table_lookup (priv->accounts, path);
  if (account != NULL)
    return account;

  account = empathy_account_new (priv->dbus, path);
  g_hash_table_insert (priv->accounts, g_strdup (path), account);

  g_signal_connect (account, "notify::ready",
    G_CALLBACK (account_manager_account_ready_cb), manager);

  return account;
}


static void
account_manager_ensure_all_accounts (EmpathyAccountManager *manager,
    GPtrArray *accounts)
{
  guint i, missing_accounts;
  GHashTableIter iter;
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);
  gpointer value;
  EmpathyAccount *account;
  gboolean found = FALSE;
  const gchar *name;

  /* ensure all accounts coming from MC5 first */
  for (i = 0; i < accounts->len; i++)
    {
      name = g_ptr_array_index (accounts, i);

      account = empathy_account_manager_ensure_account (manager, name);
      empathy_account_refresh_properties (account);
    }

  missing_accounts = empathy_account_manager_get_count (manager) -
    accounts->len;

  if (missing_accounts > 0)
    {
      /* look for accounts we have and the Tp AccountManager doesn't,
       * and remove them from our cache.
       */

      DEBUG ("%d missing accounts", missing_accounts);

      g_hash_table_iter_init (&iter, priv->accounts);

      while (g_hash_table_iter_next (&iter, NULL, &value) &&
	     missing_accounts > 0)
        {
          account = value;

          /* look for this account in the AccountManager provided array */
          for (i = 0; i < accounts->len; i++)
            {
              name = g_ptr_array_index (accounts, i);

              if (!tp_strdiff
                  (name, empathy_account_get_unique_name (account)))
                {
                  found = TRUE;
                  break;
                }
            }

          if (!found)
            {
              DEBUG ("Account %s was not found, remove it from the cache",
		     empathy_account_get_unique_name (account));

	      g_object_ref (account);
	      g_hash_table_iter_remove (&iter);
	      g_signal_emit (manager, signals[ACCOUNT_DELETED], 0, account);
	      g_object_unref (account);

              missing_accounts--;
            }

          found = FALSE;
        }
    }
}

static void
account_manager_got_all_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (weak_object);
  GPtrArray *accounts;

  if (error != NULL)
    {
      DEBUG ("Failed to get account manager properties: %s", error->message);
      return;
    }

  accounts = tp_asv_get_boxed (properties, "ValidAccounts",
    EMPATHY_ARRAY_TYPE_OBJECT);

  if (accounts != NULL)
    account_manager_ensure_all_accounts (manager, accounts);

  empathy_account_manager_check_ready (manager);
}

static void
account_validity_changed_cb (TpAccountManager *proxy,
    const gchar *path,
    gboolean valid,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (weak_object);

  if (!valid)
    return;

  empathy_account_manager_ensure_account (manager, path);
}

static void
account_manager_start_mc5 (TpDBusDaemon *bus)
{
  TpProxy *mc5_proxy;

  /* trigger MC5 starting */
  mc5_proxy = g_object_new (TP_TYPE_PROXY,
    "dbus-daemon", bus,
    "dbus-connection", tp_proxy_get_dbus_connection (TP_PROXY (bus)),
    "bus-name", MC5_BUS_NAME,
    "object-path", "/",
    NULL);

  tp_cli_dbus_peer_call_ping (mc5_proxy, -1, NULL, NULL, NULL, NULL);

  g_object_unref (mc5_proxy);
}

static void
account_manager_name_owner_cb (TpDBusDaemon *proxy,
    const gchar *name,
    const gchar *new_owner,
    gpointer user_data)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (user_data);
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);

  DEBUG ("Name owner changed for %s, new name: %s", name, new_owner);

  if (EMP_STR_EMPTY (new_owner))
    {
      /* MC5 quit or crashed for some reason, let's start it again */
      account_manager_start_mc5 (priv->dbus);

      if (priv->tp_manager != NULL)
        g_object_unref (priv->tp_manager);

      priv->tp_manager = NULL;
      return;
    }

  if (priv->tp_manager == NULL)
    {
      priv->tp_manager = tp_account_manager_new (priv->dbus);

      tp_cli_account_manager_connect_to_account_validity_changed (
          priv->tp_manager,
          account_validity_changed_cb,
          NULL,
          NULL,
          G_OBJECT (manager),
          NULL);

      tp_cli_dbus_properties_call_get_all (priv->tp_manager, -1,
          TP_IFACE_ACCOUNT_MANAGER,
          account_manager_got_all_cb,
          NULL,
          NULL,
          G_OBJECT (manager));
    }
}

static void
empathy_account_manager_init (EmpathyAccountManager *manager)
{
  EmpathyAccountManagerPriv *priv;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
      EMPATHY_TYPE_ACCOUNT_MANAGER, EmpathyAccountManagerPriv);

  manager->priv = priv;
  priv->connected = priv->connecting = 0;
  priv->global_presence = TP_CONNECTION_PRESENCE_TYPE_UNSET;

  priv->accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) g_object_unref);

  priv->create_results = g_hash_table_new (g_direct_hash, g_direct_equal);

  priv->dbus = tp_dbus_daemon_dup (NULL);

  tp_dbus_daemon_watch_name_owner (priv->dbus,
      TP_ACCOUNT_MANAGER_BUS_NAME,
      account_manager_name_owner_cb,
      manager,
      NULL);

  account_manager_start_mc5 (priv->dbus);
}

static void
do_finalize (GObject *obj)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (obj);
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);

  g_hash_table_destroy (priv->create_results);
  g_hash_table_destroy (priv->accounts);

  g_free (priv->global_status);
  g_free (priv->global_status_message);

  g_free (priv->requested_status);
  g_free (priv->requested_status_message);

  G_OBJECT_CLASS (empathy_account_manager_parent_class)->finalize (obj);
}

static void
do_dispose (GObject *obj)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (obj);
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);
  GHashTableIter iter;
  GSimpleAsyncResult *result;

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  /* the manager is being destroyed while there are account creation
   * processes pending; this should not happen, but emit the callbacks
   * with an error anyway.
   */
  g_hash_table_iter_init (&iter, priv->create_results);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &result))
    {
      g_simple_async_result_set_error (result, G_IO_ERROR,
          G_IO_ERROR_CANCELLED, "The account manager was disposed while "
          "creating the account");
      g_simple_async_result_complete (result);
      g_object_unref (result);
    }
  g_hash_table_remove_all (priv->create_results);

  if (priv->dbus != NULL)
    {
      tp_dbus_daemon_cancel_name_owner_watch (priv->dbus,
        TP_ACCOUNT_MANAGER_BUS_NAME, account_manager_name_owner_cb, manager);

      g_object_unref (priv->dbus);
      priv->dbus = NULL;
    }

  G_OBJECT_CLASS (empathy_account_manager_parent_class)->dispose (obj);
}

static GObject *
do_constructor (GType type,
                guint n_construct_params,
                GObjectConstructParam *construct_params)
{
  GObject *retval;

  if (!manager_singleton)
    {
      retval = G_OBJECT_CLASS
        (empathy_account_manager_parent_class)->constructor (type,
            n_construct_params, construct_params);
      manager_singleton = EMPATHY_ACCOUNT_MANAGER (retval);
      g_object_add_weak_pointer (retval, (gpointer) &manager_singleton);
    }
  else
    {
      retval = g_object_ref (manager_singleton);
    }

  return retval;
}

static void
do_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (object);
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);

  switch (prop_id)
    {
      case PROP_READY:
        g_value_set_boolean (value, priv->ready);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
empathy_account_manager_class_init (EmpathyAccountManagerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->finalize = do_finalize;
  oclass->dispose = do_dispose;
  oclass->constructor = do_constructor;
  oclass->get_property = do_get_property;

  g_object_class_install_property (oclass, PROP_READY,
    g_param_spec_boolean ("ready",
      "Ready",
      "Whether the initial state dump from the account manager is finished",
      FALSE,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  signals[ACCOUNT_CREATED] =
    g_signal_new ("account-created",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1, EMPATHY_TYPE_ACCOUNT);

  signals[ACCOUNT_DELETED] =
    g_signal_new ("account-deleted",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1, EMPATHY_TYPE_ACCOUNT);

  signals[ACCOUNT_ENABLED] =
    g_signal_new ("account-enabled",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1, EMPATHY_TYPE_ACCOUNT);

  signals[ACCOUNT_DISABLED] =
    g_signal_new ("account-disabled",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1, EMPATHY_TYPE_ACCOUNT);

  signals[ACCOUNT_CHANGED] =
    g_signal_new ("account-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1, EMPATHY_TYPE_ACCOUNT);

  signals[ACCOUNT_CONNECTION_CHANGED] =
    g_signal_new ("account-connection-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  _empathy_marshal_VOID__OBJECT_INT_UINT_UINT,
                  G_TYPE_NONE,
                  4, EMPATHY_TYPE_ACCOUNT,
                  G_TYPE_INT,   /* reason */
                  G_TYPE_UINT,  /* actual connection */
                  G_TYPE_UINT); /* previous connection */

  signals[GLOBAL_PRESENCE_CHANGED] =
    g_signal_new ("global-presence-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  _empathy_marshal_VOID__UINT_STRING_STRING,
                  G_TYPE_NONE,
                  3, G_TYPE_UINT, /* Presence type */
                  G_TYPE_STRING,  /* status */
                  G_TYPE_STRING); /* stauts message*/

  signals[NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1, TP_TYPE_CONNECTION);

  g_type_class_add_private (oclass, sizeof (EmpathyAccountManagerPriv));
}

/* public methods */

EmpathyAccountManager *
empathy_account_manager_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_ACCOUNT_MANAGER, NULL);
}

gboolean
empathy_account_manager_is_ready (EmpathyAccountManager *manager)
{
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);

  return priv->ready;
}

int
empathy_account_manager_get_connected_accounts (EmpathyAccountManager *manager)
{
  EmpathyAccountManagerPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_MANAGER (manager), 0);

  priv = GET_PRIV (manager);

  return priv->connected;
}

int
empathy_account_manager_get_connecting_accounts (
    EmpathyAccountManager *manager)
{
  EmpathyAccountManagerPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_MANAGER (manager), 0);

  priv = GET_PRIV (manager);

  return priv->connecting;
}

/**
 * empathy_account_manager_get_count:
 * @manager: a #EmpathyAccountManager
 *
 * Get the number of accounts.
 *
 * Returns: the number of accounts.
 **/
int
empathy_account_manager_get_count (EmpathyAccountManager *manager)
{
  EmpathyAccountManagerPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_MANAGER (manager), 0);

  priv = GET_PRIV (manager);

  return g_hash_table_size (priv->accounts);
}

EmpathyAccount *
empathy_account_manager_get_account_for_connection (
    EmpathyAccountManager *manager,
    TpConnection          *connection)
{
  EmpathyAccountManagerPriv *priv;
  GHashTableIter iter;
  gpointer value;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_MANAGER (manager), 0);

  priv = GET_PRIV (manager);

  g_hash_table_iter_init (&iter, priv->accounts);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      EmpathyAccount *account = EMPATHY_ACCOUNT (value);

      if (connection == empathy_account_get_connection (account))
          return account;
    }

  return NULL;
}

GList *
empathy_account_manager_dup_accounts (EmpathyAccountManager *manager)
{
  EmpathyAccountManagerPriv *priv;
  GList *ret;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_MANAGER (manager), NULL);

  priv = GET_PRIV (manager);

  ret = g_hash_table_get_values (priv->accounts);
  g_list_foreach (ret, (GFunc) g_object_ref, NULL);

  return ret;
}

/**
 * empathy_account_manager_dup_connections:
 * @manager: a #EmpathyAccountManager
 *
 * Get a #GList of all ready #TpConnection. The list must be freed with
 * g_list_free, and its elements must be unreffed.
 *
 * Returns: the list of connections
 **/
GList *
empathy_account_manager_dup_connections (EmpathyAccountManager *manager)
{
  EmpathyAccountManagerPriv *priv;
  GHashTableIter iter;
  gpointer value;
  GList *ret = NULL;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_MANAGER (manager), NULL);

  priv = GET_PRIV (manager);

  g_hash_table_iter_init (&iter, priv->accounts);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      EmpathyAccount *account = EMPATHY_ACCOUNT (value);
      TpConnection *connection;

      connection = empathy_account_get_connection (account);
      if (connection != NULL)
        ret = g_list_prepend (ret, g_object_ref (connection));
    }

  return ret;
}

void
empathy_account_manager_request_global_presence (
  EmpathyAccountManager *manager,
  TpConnectionPresenceType type,
  const gchar *status,
  const gchar *message)
{
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);
  GHashTableIter iter;
  gpointer value;

  DEBUG ("request global presence, type: %d, status: %s, message: %s",
         type, status, message);

  g_hash_table_iter_init (&iter, priv->accounts);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      EmpathyAccount *account = EMPATHY_ACCOUNT (value);
      gboolean ready;

      g_object_get (account, "ready", &ready, NULL);

      if (ready)
        empathy_account_request_presence (account, type, status, message);
    }

  /* save the requested global presence, to use it in case we create
   * new accounts or some accounts become ready.
   */
  priv->requested_presence = type;

  if (tp_strdiff (priv->requested_status, status))
    {
      g_free (priv->requested_status);
      priv->requested_status = g_strdup (status);
    }

  if (tp_strdiff (priv->requested_status_message, message))
    {
      g_free (priv->requested_status_message);
      priv->requested_status_message = g_strdup (message);
    }
}

TpConnectionPresenceType
empathy_account_manager_get_requested_global_presence (
  EmpathyAccountManager *manager,
  gchar **status,
  gchar **message)
{
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);

  if (status != NULL)
    *status = g_strdup (priv->requested_status);
  if (message != NULL)
    *message = g_strdup (priv->requested_status_message);

  return priv->requested_presence;
}

TpConnectionPresenceType
empathy_account_manager_get_global_presence (
  EmpathyAccountManager *manager,
  gchar **status,
  gchar **message)
{
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);

  if (status != NULL)
    *status = g_strdup (priv->global_status);
  if (message != NULL)
    *message = g_strdup (priv->global_status_message);

  return priv->global_presence;
}

static void
empathy_account_manager_created_cb (TpAccountManager *proxy,
    const gchar *account_path,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyAccountManager *manager = EMPATHY_ACCOUNT_MANAGER (weak_object);
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);
  GSimpleAsyncResult *my_res = user_data;
  EmpathyAccount *account;

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (my_res,
          (GError *) error);
      g_simple_async_result_complete (my_res);
      g_object_unref (my_res);

      return;
    }

  account = empathy_account_manager_ensure_account (manager, account_path);

  g_hash_table_insert (priv->create_results, account, my_res);
}

void
empathy_account_manager_create_account_async (EmpathyAccountManager *manager,
    const gchar *connection_manager,
    const gchar *protocol,
    const gchar *display_name,
    GHashTable *parameters,
    GHashTable *properties,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  EmpathyAccountManagerPriv *priv = GET_PRIV (manager);
  GSimpleAsyncResult *res;

  res = g_simple_async_result_new
    (G_OBJECT (manager), callback, user_data,
     empathy_account_manager_create_account_finish);

  tp_cli_account_manager_call_create_account (priv->tp_manager,
      -1,
      connection_manager,
      protocol,
      display_name,
      parameters,
      properties,
      empathy_account_manager_created_cb,
      res,
      NULL,
      G_OBJECT (manager));
}

EmpathyAccount *
empathy_account_manager_create_account_finish (
  EmpathyAccountManager *manager, GAsyncResult *result, GError **error)
{
  EmpathyAccount *retval;

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (manager), empathy_account_manager_create_account_finish), NULL);

  retval = EMPATHY_ACCOUNT (g_simple_async_result_get_op_res_gpointer (
    G_SIMPLE_ASYNC_RESULT (result)));

  return retval;
}

