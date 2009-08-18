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

#ifndef __EMPATHY_CONNECTIVITY_H__
#define __EMPATHY_CONNECTIVITY_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_CONNECTIVITY (empathy_connectivity_get_type ())
#define EMPATHY_CONNECTIVITY(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_CONNECTIVITY, \
      EmpathyConnectivity))
#define EMPATHY_CONNECTIVITY_CLASS(k) \
  (G_TYPE_CHECK_CLASS_CAST ((k), EMPATHY_TYPE_CONNECTIVITY, \
      EmpathyConnectivityClass))
#define EMPATHY_IS_CONNECTIVITY(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_CONNECTIVITY))
#define EMPATHY_IS_CONNECTIVITY_CLASS(k) \
  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_CONNECTIVITY))
#define EMPATHY_CONNECTIVITY_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_CONNECTIVITY, \
      EmpathyConnectivityClass))

typedef struct _EmpathyConnectivity EmpathyConnectivity;
typedef struct _EmpathyConnectivityClass EmpathyConnectivityClass;

struct _EmpathyConnectivity {
  GObject parent;
  gpointer priv;
};

struct _EmpathyConnectivityClass {
  GObjectClass parent_class;
};

GType empathy_connectivity_get_type (void);

/* public methods */

EmpathyConnectivity * empathy_connectivity_dup_singleton (void);

gboolean empathy_connectivity_is_online (EmpathyConnectivity *connectivity);

gboolean empathy_connectivity_get_use_conn (EmpathyConnectivity *connectivity);
void empathy_connectivity_set_use_conn (EmpathyConnectivity *connectivity,
    gboolean use_conn);

G_END_DECLS

#endif /* __EMPATHY_CONNECTIVITY_H__ */

