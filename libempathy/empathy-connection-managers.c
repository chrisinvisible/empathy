/*
 * empathy-connection-managers.c - Source for EmpathyConnectionManagers
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

#include <telepathy-glib/connection-manager.h>
#include <telepathy-glib/util.h>

#include "empathy-connection-managers.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

static GObject *managers = NULL;

G_DEFINE_TYPE(EmpathyConnectionManagers, empathy_connection_managers,
    G_TYPE_OBJECT)

/* signal enum */
enum
{
    UPDATED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum {
  PROP_READY = 1
};

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyConnectionManagers)


/* private structure */
typedef struct _EmpathyConnectionManagersPriv
  EmpathyConnectionManagersPriv;

struct _EmpathyConnectionManagersPriv
{
  gboolean dispose_has_run;
  gboolean ready;

  GList *cms;

  TpDBusDaemon *dbus;
};

static void
empathy_connection_managers_init (EmpathyConnectionManagers *obj)
{
  EmpathyConnectionManagersPriv *priv =
    G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
      EMPATHY_TYPE_CONNECTION_MANAGERS, EmpathyConnectionManagersPriv);

  obj->priv = priv;

  priv->dbus = tp_dbus_daemon_dup (NULL);
  g_assert (priv->dbus != NULL);

  empathy_connection_managers_update (obj);

  /* allocate any data required by the object here */
}

static void empathy_connection_managers_dispose (GObject *object);

static GObject *
empathy_connection_managers_constructor (GType type,
                        guint n_construct_params,
                        GObjectConstructParam *construct_params)
{
  if (managers != NULL)
    return g_object_ref (managers);

  managers =
      G_OBJECT_CLASS (empathy_connection_managers_parent_class)->constructor
          (type, n_construct_params, construct_params);

  g_object_add_weak_pointer (managers, (gpointer) &managers);

  return managers;
}



static void
empathy_connection_managers_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyConnectionManagers *self = EMPATHY_CONNECTION_MANAGERS (object);
  EmpathyConnectionManagersPriv *priv = GET_PRIV (self);

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
empathy_connection_managers_class_init (
    EmpathyConnectionManagersClass *empathy_connection_managers_class)
{
  GObjectClass *object_class =
      G_OBJECT_CLASS (empathy_connection_managers_class);

  g_type_class_add_private (empathy_connection_managers_class, sizeof
      (EmpathyConnectionManagersPriv));

  object_class->constructor = empathy_connection_managers_constructor;
  object_class->dispose = empathy_connection_managers_dispose;
  object_class->get_property = empathy_connection_managers_get_property;

  g_object_class_install_property (object_class, PROP_READY,
    g_param_spec_boolean ("ready",
      "Ready",
      "Whether the connection manager information is ready to be used",
      FALSE,
      G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  signals[UPDATED] = g_signal_new ("updated",
    G_TYPE_FROM_CLASS (object_class),
    G_SIGNAL_RUN_LAST,
    0, NULL, NULL,
    g_cclosure_marshal_VOID__VOID,
    G_TYPE_NONE, 0);
}

static void
empathy_connection_managers_free_cm_list (EmpathyConnectionManagers *self)
{
  EmpathyConnectionManagersPriv *priv = GET_PRIV (self);
  GList *l;

  for (l = priv->cms ; l != NULL ; l = g_list_next (l))
    {
      g_object_unref (l->data);
    }
  g_list_free (priv->cms);

  priv->cms = NULL;
}

static void
empathy_connection_managers_dispose (GObject *object)
{
  EmpathyConnectionManagers *self = EMPATHY_CONNECTION_MANAGERS (object);
  EmpathyConnectionManagersPriv *priv = GET_PRIV (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->dbus != NULL)
    g_object_unref (priv->dbus);
  priv->dbus = NULL;

  empathy_connection_managers_free_cm_list (self);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (empathy_connection_managers_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_connection_managers_parent_class)->dispose (object);
}

EmpathyConnectionManagers *
empathy_connection_managers_dup_singleton (void)
{
  return EMPATHY_CONNECTION_MANAGERS (
      g_object_new (EMPATHY_TYPE_CONNECTION_MANAGERS, NULL));
}

gboolean
empathy_connection_managers_is_ready (EmpathyConnectionManagers *self)
{
  EmpathyConnectionManagersPriv *priv = GET_PRIV (self);
  return priv->ready;
}

static void
empathy_connection_managers_listed_cb (TpConnectionManager * const *cms,
    gsize n_cms,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyConnectionManagers *self =
    EMPATHY_CONNECTION_MANAGERS (weak_object);
  EmpathyConnectionManagersPriv *priv = GET_PRIV (self);
  TpConnectionManager * const *iter;

  empathy_connection_managers_free_cm_list (self);

  if (error != NULL)
    {
      DEBUG ("Failed to get connection managers: %s", error->message);
      goto out;
    }

  for (iter = cms ; iter != NULL && *iter != NULL; iter++)
    {
      /* only list cms that didn't hit errors */
      if (tp_connection_manager_is_ready (*iter))
        priv->cms = g_list_prepend (priv->cms, g_object_ref (*iter));
    }

out:
  g_object_ref (weak_object);
  if (!priv->ready)
    {
      priv->ready = TRUE;
      g_object_notify (weak_object, "ready");
    }
  g_signal_emit (weak_object, signals[UPDATED], 0);
  g_object_unref (weak_object);
}

void
empathy_connection_managers_update (EmpathyConnectionManagers *self)
{
  EmpathyConnectionManagersPriv *priv = GET_PRIV (self);

  tp_list_connection_managers (priv->dbus,
    empathy_connection_managers_listed_cb,
    NULL, NULL, G_OBJECT (self));
}

GList *
empathy_connection_managers_get_cms (EmpathyConnectionManagers *self)
{
  EmpathyConnectionManagersPriv *priv = GET_PRIV (self);

  return priv->cms;
}

TpConnectionManager *
empathy_connection_managers_get_cm (EmpathyConnectionManagers *self,
  const gchar *cm)
{
  EmpathyConnectionManagersPriv *priv = GET_PRIV (self);
  GList *l;

  for (l = priv->cms ; l != NULL; l = g_list_next (l))
    {
      TpConnectionManager *c = TP_CONNECTION_MANAGER (l->data);

      if (!tp_strdiff (c->name, cm))
        return c;
    }

  return NULL;
}

guint
empathy_connection_managers_get_cms_num (EmpathyConnectionManagers *self)
{
  EmpathyConnectionManagersPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CONNECTION_MANAGERS (self), 0);

  priv = GET_PRIV (self);

  return g_list_length (priv->cms);
}

typedef struct
{
  EmpathyConnectionManagers *self;
  EmpathyConnectionManagersWhenReadyCb callback;
  gpointer user_data;
  guint ready_id;
} CallWhenReadyContext;

static void
call_when_ready_ctx_free (CallWhenReadyContext *ctx)
{
  g_signal_handler_disconnect (ctx->self, ctx->ready_id);
  g_object_unref (ctx->self);
  g_slice_free (CallWhenReadyContext, ctx);
}

static void
cwr_ready (EmpathyConnectionManagers *self,
    GParamSpec *spec,
    CallWhenReadyContext *ctx)
{
  g_assert (ctx->callback != NULL);

  ctx->callback (self, NULL, ctx->user_data);

  call_when_ready_ctx_free (ctx);
}

static CallWhenReadyContext *
call_when_ready_ctx_new (EmpathyConnectionManagers *self,
    EmpathyConnectionManagersWhenReadyCb callback,
    gpointer user_data)
{
  CallWhenReadyContext *ctx = g_slice_new (CallWhenReadyContext);

  ctx->self = g_object_ref (self);
  ctx->callback = callback;
  ctx->user_data = user_data;

  ctx->ready_id = g_signal_connect (self, "notify::ready",
      G_CALLBACK (cwr_ready), ctx);

  return ctx;
}

void
empathy_connection_managers_call_when_ready (
    EmpathyConnectionManagers *self,
    EmpathyConnectionManagersWhenReadyCb callback,
    gpointer user_data)
{
  EmpathyConnectionManagersPriv *priv = GET_PRIV (self);

  if (priv->ready)
    {
      callback (self, NULL, user_data);
    }
  else
    {
      call_when_ready_ctx_new (self, callback, user_data);
    }
}
