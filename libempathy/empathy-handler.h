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
 * Authors: Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 */

#ifndef __EMPATHY_HANDLER_H__
#define __EMPATHY_HANDLER_H__

#include <glib.h>

#include <telepathy-glib/channel.h>
#include <telepathy-glib/dbus-properties-mixin.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_HANDLER         (empathy_handler_get_type ())
#define EMPATHY_HANDLER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), \
  EMPATHY_TYPE_HANDLER, EmpathyHandler))
#define EMPATHY_HANDLER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), \
  EMPATHY_TYPE_HANDLER, EmpathyHandlerClass))
#define EMPATHY_IS_HANDLER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
  EMPATHY_TYPE_HANDLER))
#define EMPATHY_IS_HANDLER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), \
  EMPATHY_TYPE_HANDLER))
#define EMPATHY_HANDLER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), \
  EMPATHY_TYPE_HANDLER, EmpathyHandlerClass))

GType empathy_handler_get_type (void) G_GNUC_CONST;

typedef struct _EmpathyHandler      EmpathyHandler;
typedef struct _EmpathyHandlerClass EmpathyHandlerClass;

struct _EmpathyHandler
{
  GObject parent;
  gpointer priv;
};

struct _EmpathyHandlerClass
{
  GObjectClass parent_class;
  TpDBusPropertiesMixinClass dbus_props_class;
};


EmpathyHandler * empathy_handler_new (const gchar *name,
    GPtrArray *filters,
    GStrv capabilities);

const gchar *empathy_handler_get_busname (EmpathyHandler *handler);

typedef gboolean (EmpathyHandlerHandleChannelsFunc) (EmpathyHandler *handler,
    const gchar *account_path,
    const gchar *connection_path,
    const GPtrArray *channels,
    const GPtrArray *requests_satisfied,
    guint64 timestamp,
    GHashTable *handler_info,
    gpointer user_data,
    GError **error);

void empathy_handler_set_handle_channels_func (EmpathyHandler *handler,
    EmpathyHandlerHandleChannelsFunc *func,
    gpointer user_data);

typedef GList * (EmpathyHandlerChannelsFunc) (
    EmpathyHandler *handler,
    gpointer user_data);

void empathy_handler_set_channels_func (EmpathyHandler *handler,
    EmpathyHandlerChannelsFunc func,
    gpointer user_data);

G_END_DECLS

#endif /* __EMPATHY_HANDLER_H__ */
