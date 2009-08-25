/*
 * Copyright (C) 2008-2009 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * Authors: Jonny Lamb <jonny.lamb@collabora.co.uk>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

/* empathy-import-widget.h */

#ifndef __EMPATHY_IMPORT_WIDGET_H__
#define __EMPATHY_IMPORT_WIDGET_H__

#include <glib-object.h>

#include "empathy-import-utils.h"

G_BEGIN_DECLS

#define EMPATHY_TYPE_IMPORT_WIDGET empathy_import_widget_get_type()
#define EMPATHY_IMPORT_WIDGET(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_IMPORT_WIDGET,\
      EmpathyImportWidget))
#define EMPATHY_IMPORT_WIDGET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), EMPATHY_TYPE_IMPORT_WIDGET,\
      EmpathyImportWidgetClass))
#define EMPATHY_IS_IMPORT_WIDGET(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_IMPORT_WIDGET))
#define EMPATHY_IS_IMPORT_WIDGET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), EMPATHY_TYPE_IMPORT_WIDGET))
#define EMPATHY_IMPORT_WIDGET_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_IMPORT_WIDGET,\
      EmpathyImportWidgetClass))

typedef struct {
  GObject parent;

  /* private */
  gpointer priv;
} EmpathyImportWidget;

typedef struct {
  GObjectClass parent_class;
} EmpathyImportWidgetClass;

GType empathy_import_widget_get_type (void);

EmpathyImportWidget* empathy_import_widget_new (EmpathyImportApplication id);

GtkWidget * empathy_import_widget_get_widget (EmpathyImportWidget *self);

void empathy_import_widget_add_selected_accounts (EmpathyImportWidget *self);

G_END_DECLS

#endif /* __EMPATHY_IMPORT_WIDGET_H__ */
