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
 * Authors: Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

/* empathy-account-assistant.h */

#ifndef __EMPATHY_ACCOUNT_ASSISTANT_H__
#define __EMPATHY_ACCOUNT_ASSISTANT_H__

#include <glib-object.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-connection-managers.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_ACCOUNT_ASSISTANT empathy_account_assistant_get_type()
#define EMPATHY_ACCOUNT_ASSISTANT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_ACCOUNT_ASSISTANT,\
      EmpathyAccountAssistant))
#define EMPATHY_ACCOUNT_ASSISTANT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), EMPATHY_TYPE_ACCOUNT_ASSISTANT,\
      EmpathyAccountAssistantClass))
#define EMPATHY_IS_ACCOUNT_ASSISTANT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_ACCOUNT_ASSISTANT))
#define EMPATHY_IS_ACCOUNT_ASSISTANT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), EMPATHY_TYPE_ACCOUNT_ASSISTANT))
#define EMPATHY_ACCOUNT_ASSISTANT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_ACCOUNT_ASSISTANT,\
      EmpathyAccountAssistantClass))

typedef struct {
  GtkAssistant parent;

  /* private */
  gpointer priv;
} EmpathyAccountAssistant;

typedef struct {
  GtkAssistantClass parent_class;
} EmpathyAccountAssistantClass;

GType empathy_account_assistant_get_type (void);

GtkWidget *empathy_account_assistant_show (GtkWindow *parent,
    EmpathyConnectionManagers *connection_mgrs);

G_END_DECLS

#endif /* __EMPATHY_ACCOUNT_ASSISTANT_H__ */
