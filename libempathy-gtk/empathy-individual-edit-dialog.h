/*
 * Copyright (C) 2010 Collabora Ltd.
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
 * Authors: Philip Withnall <philip.withnall@collabora.co.uk>
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifndef __EMPATHY_INDIVIDUAL_EDIT_DIALOG_H__
#define __EMPATHY_INDIVIDUAL_EDIT_DIALOG_H__

#include <gtk/gtk.h>

#include <folks/folks.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_INDIVIDUAL_EDIT_DIALOG         (empathy_individual_edit_dialog_get_type ())
#define EMPATHY_INDIVIDUAL_EDIT_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_INDIVIDUAL_EDIT_DIALOG, EmpathyIndividualEditDialog))
#define EMPATHY_INDIVIDUAL_EDIT_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), EMPATHY_TYPE_INDIVIDUAL_EDIT_DIALOG, EmpathyIndividualEditDialogClass))
#define EMPATHY_IS_INDIVIDUAL_EDIT_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_INDIVIDUAL_EDIT_DIALOG))
#define EMPATHY_IS_INDIVIDUAL_EDIT_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_INDIVIDUAL_EDIT_DIALOG))
#define EMPATHY_INDIVIDUAL_EDIT_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_INDIVIDUAL_EDIT_DIALOG, EmpathyIndividualEditDialogClass))

typedef struct _EmpathyIndividualEditDialog      EmpathyIndividualEditDialog;
typedef struct _EmpathyIndividualEditDialogClass EmpathyIndividualEditDialogClass;

struct _EmpathyIndividualEditDialog {
  GtkDialog parent;

  /*<private>*/
  gpointer priv;
};

struct _EmpathyIndividualEditDialogClass {
  GtkDialogClass parent_class;
};

GType empathy_individual_edit_dialog_get_type (void) G_GNUC_CONST;

void empathy_individual_edit_dialog_show (FolksIndividual *individual,
    GtkWindow *parent);

G_END_DECLS

#endif /*  __EMPATHY_INDIVIDUAL_EDIT_DIALOG_H__ */
