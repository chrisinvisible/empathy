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

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyConnectivity)

typedef struct {
  gboolean dispose_run;
} EmpathyConnectivityPriv;

enum {
  STATE_CHANGE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];
static EmpathyConnectivity *connectivity_singleton = NULL;

G_DEFINE_TYPE (EmpathyConnectivity, empathy_connectivity, G_TYPE_OBJECT);

static void
empathy_connectivity_init (EmpathyConnectivity *connectivity)
{
  EmpathyConnectivityPriv *priv;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (connectivity,
      EMPATHY_TYPE_CONNECTIVITY, EmpathyConnectivityPriv);

  connectivity->priv = priv;
  priv->dispose_run = FALSE;
}

static void
connectivity_finalize (GObject *object)
{
  EmpathyConnectivity *manager = EMPATHY_CONNECTIVITY (obj);
  EmpathyConnectivityPriv *priv = GET_PRIV (manager);

  G_OBJECT_CLASS (empathy_connectivity_parent_class)->finalize (obj);
}

static void
connectivity_dispose (GObject *object)
{
  EmpathyConnectivity *manager = EMPATHY_CONNECTIVITY (obj);
  EmpathyConnectivityPriv *priv = GET_PRIV (manager);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  G_OBJECT_CLASS (empathy_connectivity_parent_class)->dispose (obj);
}

static GObject *
connectivity_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  GObject *retval;

  if (!manager_singleton)
    {
      retval = G_OBJECT_CLASS (empathy_connectivity_parent_class)->constructor
        (type, n_construct_params, construct_params);

      manager_singleton = EMPATHY_CONNECTIVITY (retval);
      g_object_add_weak_pointer (retval, (gpointer) &manager_singleton);
    }
  else
    {
      retval = g_object_ref (manager_singleton);
    }

  return retval;
}

static void
empathy_connectivity_class_init (EmpathyConnectivityClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->finalize = connectivity_finalize;
  oclass->dispose = connectivity_dispose;
  oclass->constructor = connectivity_constructor;

  signals[STATE_CHANGE] =
    g_signal_new ("state-change",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__OBJECT, /* TODO */
        G_TYPE_NONE,
        0, NULL);

  g_type_class_add_private (oclass, sizeof (EmpathyConnectivityPriv));
}

/* public methods */

EmpathyConnectivity *
empathy_connectivity_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_CONNECTIVITY, NULL);
}
