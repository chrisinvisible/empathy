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

#include <gtk/gtk.h>

#ifndef __EMPATHY_IMPORT_DIALOG_H__
#define __EMPATHY_IMPORT_DIALOG_H__

G_BEGIN_DECLS

#define EMPATHY_TYPE_IMPORT_DIALOG empathy_import_dialog_get_type()
#define EMPATHY_IMPORT_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_IMPORT_DIALOG,\
      EmpathyImportDialog))
#define EMPATHY_IMPORT_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), EMPATHY_TYPE_IMPORT_DIALOG,\
      EmpathyImportDialogClass))
#define EMPATHY_IS_IMPORT_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_IMPORT_DIALOG))
#define EMPATHY_IS_IMPORT_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), EMPATHY_TYPE_IMPORT_DIALOG))
#define EMPATHY_IMPORT_DIALOG_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_IMPORT_DIALOG,\
      EmpathyImportDialogClass))

typedef struct {
  GtkDialog parent;

  /* private */
  gpointer priv;
} EmpathyImportDialog;

typedef struct {
  GtkDialogClass parent_class;
} EmpathyImportDialogClass;

GType empathy_import_dialog_get_type (void);

GtkWidget* empathy_import_dialog_new (GtkWindow *parent_window,
    gboolean show_warning);

G_END_DECLS

#endif /* __EMPATHY_IMPORT_DIALOG_H__ */
