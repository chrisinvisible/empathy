/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * Authors: Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#ifndef __EMPATHY_ACCOUNTS_DIALOG_H__
#define __EMPATHY_ACCOUNTS_DIALOG_H__

#include <gtk/gtk.h>

#include <telepathy-glib/account.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_ACCOUNTS_DIALOG empathy_accounts_dialog_get_type()
#define EMPATHY_ACCOUNTS_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_ACCOUNTS_DIALOG, EmpathyAccountsDialog))
#define EMPATHY_ACCOUNTS_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), EMPATHY_TYPE_ACCOUNTS_DIALOG, EmpathyAccountsDialogClass))
#define EMPATHY_IS_ACCOUNTS_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_ACCOUNTS_DIALOG))
#define EMPATHY_IS_ACCOUNTS_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), EMPATHY_TYPE_ACCOUNTS_DIALOG))
#define EMPATHY_ACCOUNTS_DIALOG_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_ACCOUNTS_DIALOG, EmpathyAccountsDialogClass))

typedef struct {
  GtkDialog parent;

  /* private */
  gpointer priv;
} EmpathyAccountsDialog;

typedef struct {
  GtkDialogClass parent_class;
} EmpathyAccountsDialogClass;

GType empathy_accounts_dialog_get_type (void);
GtkWidget *empathy_accounts_dialog_show (GtkWindow *parent,
    TpAccount *selected_account);

void empathy_account_dialog_cancel (EmpathyAccountsDialog *dialog);
gboolean empathy_accounts_dialog_is_creating (EmpathyAccountsDialog *dialog);

void empathy_accounts_dialog_show_application (GdkScreen *screen,
    TpAccount *selected_account,
    gboolean if_needed,
    gboolean hidden);

G_END_DECLS

#endif /* __EMPATHY_ACCOUNTS_DIALOG_H__ */
