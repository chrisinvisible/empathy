/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/*
 * Copyright (C) 2009 Collabora Ltd.
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
 * Authors: Jonny Lamb <jonny.lamb@collabora.co.uk>
 */

#include "config.h"
#include "empathy-connectivity.h"

#ifdef HAVE_NM
#include <nm-client.h>
#endif

#include "empathy-utils.h"
#include "empathy-marshal.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CONNECTIVITY
#include "empathy-debug.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyConnectivity)

typedef struct {
#ifdef HAVE_NM
  NMClient *nm_client;
#endif

  gboolean connected;
  gboolean use_conn;
} EmpathyConnectivityPriv;

enum {
  STATE_CHANGE,
  LAST_SIGNAL
};

enum {
  PROP_0,
  PROP_USE_CONN,
};

static guint signals[LAST_SIGNAL];
static EmpathyConnectivity *connectivity_singleton = NULL;

G_DEFINE_TYPE (EmpathyConnectivity, empathy_connectivity, G_TYPE_OBJECT);

#ifdef HAVE_NM
static void
connectivity_nm_state_change_cb (NMClient *client,
    const GParamSpec *pspec,
    EmpathyConnectivity *connectivity)
{
  EmpathyConnectivityPriv *priv;
  gboolean old_nm_connected;
  gboolean new_nm_connected;
  NMState state;

  priv = GET_PRIV (connectivity);

  if (!priv->use_conn)
    return;

  state = nm_client_get_state (priv->nm_client);
  old_nm_connected = priv->connected;
  new_nm_connected = !(state == NM_STATE_CONNECTING
      || state == NM_STATE_DISCONNECTED);

  DEBUG ("New NetworkManager network state %d", state);

  priv->connected = new_nm_connected;

  g_signal_emit (connectivity, signals[STATE_CHANGE], 0,
      old_nm_connected, new_nm_connected);
}
#endif

static void
empathy_connectivity_init (EmpathyConnectivity *connectivity)
{
  EmpathyConnectivityPriv *priv;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (connectivity,
      EMPATHY_TYPE_CONNECTIVITY, EmpathyConnectivityPriv);

  connectivity->priv = priv;

  priv->use_conn = TRUE;

#ifdef HAVE_NM
  priv->nm_client = nm_client_new ();
  if (priv->nm_client != NULL)
    {
      g_signal_connect (priv->nm_client, "notify::" NM_CLIENT_STATE,
          G_CALLBACK (connectivity_nm_state_change_cb), connectivity);

      connectivity_nm_state_change_cb (priv->nm_client, NULL, connectivity);
    }
  else
    {
      DEBUG ("Failed to get NetworkManager proxy");
    }
#else
  priv->connected = TRUE;
#endif
}

static void
connectivity_finalize (GObject *object)
{
#ifdef HAVE_NM
  EmpathyConnectivity *connectivity = EMPATHY_CONNECTIVITY (object);
  EmpathyConnectivityPriv *priv = GET_PRIV (connectivity);

  if (priv->nm_client != NULL)
    {
      g_signal_handlers_disconnect_by_func (priv->nm_client,
          connectivity_nm_state_change_cb, connectivity);
      g_object_unref (priv->nm_client);
      priv->nm_client = NULL;
    }
#endif

  G_OBJECT_CLASS (empathy_connectivity_parent_class)->finalize (object);
}

static void
connectivity_dispose (GObject *object)
{
  G_OBJECT_CLASS (empathy_connectivity_parent_class)->dispose (object);
}

static GObject *
connectivity_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  GObject *retval;

  if (!connectivity_singleton)
    {
      retval = G_OBJECT_CLASS (empathy_connectivity_parent_class)->constructor
        (type, n_construct_params, construct_params);

      connectivity_singleton = EMPATHY_CONNECTIVITY (retval);
      g_object_add_weak_pointer (retval, (gpointer) &connectivity_singleton);
    }
  else
    {
      retval = g_object_ref (connectivity_singleton);
    }

  return retval;
}

static void
connectivity_get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyConnectivity *connectivity = EMPATHY_CONNECTIVITY (object);

  switch (param_id)
    {
    case PROP_USE_CONN:
      g_value_set_boolean (value, empathy_connectivity_get_use_conn (
              connectivity));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    };
}

static void
connectivity_set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyConnectivity *connectivity = EMPATHY_CONNECTIVITY (object);

  switch (param_id)
    {
    case PROP_USE_CONN:
      empathy_connectivity_set_use_conn (connectivity,
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    };
}

static void
empathy_connectivity_class_init (EmpathyConnectivityClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->finalize = connectivity_finalize;
  oclass->dispose = connectivity_dispose;
  oclass->constructor = connectivity_constructor;
  oclass->get_property = connectivity_get_property;
  oclass->set_property = connectivity_set_property;

  signals[STATE_CHANGE] =
    g_signal_new ("state-change",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        _empathy_marshal_VOID__BOOLEAN_BOOLEAN,
        G_TYPE_NONE,
        2, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, NULL);

  g_object_class_install_property (oclass,
      PROP_USE_CONN,
      g_param_spec_boolean ("use-conn",
          "Use connectivity managers",
          "Set presence according to connectivity managers",
          TRUE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  g_type_class_add_private (oclass, sizeof (EmpathyConnectivityPriv));
}

/* public methods */

EmpathyConnectivity *
empathy_connectivity_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_CONNECTIVITY, NULL);
}

gboolean
empathy_connectivity_is_online (EmpathyConnectivity *connectivity)
{
  EmpathyConnectivityPriv *priv = GET_PRIV (connectivity);

  if (priv->use_conn)
    {
      return priv->connected;
    }
  else
    {
      return TRUE;
    }
}

gboolean
empathy_connectivity_get_use_conn (EmpathyConnectivity *connectivity)
{
  EmpathyConnectivityPriv *priv = GET_PRIV (connectivity);

  return priv->use_conn;
}

void
empathy_connectivity_set_use_conn (EmpathyConnectivity *connectivity,
    gboolean use_conn)
{
  EmpathyConnectivityPriv *priv = GET_PRIV (connectivity);

  if (use_conn == priv->use_conn)
    return;

  DEBUG ("use_conn gconf key changed; new value = %s",
      use_conn ? "true" : "false");

  priv->use_conn = use_conn;

#ifdef HAVE_NM
  if (use_conn)
    {
      connectivity_nm_state_change_cb (priv->nm_client, NULL, connectivity);
#else
  if (FALSE)
    {
#endif
    }
  else
    {
      g_signal_emit (connectivity, signals[STATE_CHANGE], 0,
          FALSE, TRUE);
    }

  g_object_notify (G_OBJECT (connectivity), "use-conn");
}
