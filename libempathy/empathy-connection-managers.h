/*
 * empathy-connection-managers.h - Header for EmpathyConnectionManagers
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

#ifndef __EMPATHY_CONNECTION_MANAGERS_H__
#define __EMPATHY_CONNECTION_MANAGERS_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <telepathy-glib/connection-manager.h>

G_BEGIN_DECLS

typedef struct _EmpathyConnectionManagers EmpathyConnectionManagers;
typedef struct _EmpathyConnectionManagersClass EmpathyConnectionManagersClass;

struct _EmpathyConnectionManagersClass {
    GObjectClass parent_class;
};

struct _EmpathyConnectionManagers {
    GObject parent;
    gpointer priv;
};

GType empathy_connection_managers_get_type (void);

/* TYPE MACROS */
#define EMPATHY_TYPE_CONNECTION_MANAGERS \
  (empathy_connection_managers_get_type ())
#define EMPATHY_CONNECTION_MANAGERS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_CONNECTION_MANAGERS, \
    EmpathyConnectionManagers))
#define EMPATHY_CONNECTION_MANAGERS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_CONNECTION_MANAGERS, \
    EmpathyConnectionManagersClass))
#define EMPATHY_IS_CONNECTION_MANAGERS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_CONNECTION_MANAGERS))
#define EMPATHY_IS_CONNECTION_MANAGERS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_CONNECTION_MANAGERS))
#define EMPATHY_CONNECTION_MANAGERS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_CONNECTION_MANAGERS, \
    EmpathyConnectionManagersClass))

EmpathyConnectionManagers *empathy_connection_managers_dup_singleton (void);
gboolean empathy_connection_managers_is_ready (
    EmpathyConnectionManagers *managers);

void empathy_connection_managers_update (EmpathyConnectionManagers *managers);

GList * empathy_connection_managers_get_cms (
    EmpathyConnectionManagers *managers);
guint empathy_connection_managers_get_cms_num
    (EmpathyConnectionManagers *managers);

TpConnectionManager *empathy_connection_managers_get_cm (
  EmpathyConnectionManagers *managers, const gchar *cm);

void empathy_connection_managers_prepare_async (
    EmpathyConnectionManagers *managers,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean empathy_connection_managers_prepare_finish (
    EmpathyConnectionManagers *managers,
    GAsyncResult *result,
    GError **error);

G_END_DECLS

#endif /* #ifndef __EMPATHY_CONNECTION_MANAGERS_H__*/
