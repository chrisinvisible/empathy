/*
 * Copyright (C) 2007-2008 Guillaume Desmottes
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
 * Authors: Guillaume Desmottes <gdesmott@gnome.org>
 */

#ifndef __EMPATHY_IRC_NETWORK_CHOOSER_DIALOG_H__
#define __EMPATHY_IRC_NETWORK_CHOOSER_DIALOG_H__

#include <gtk/gtk.h>

#include <libempathy/empathy-account-settings.h>
#include <libempathy/empathy-irc-network.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_IRC_NETWORK_CHOOSER_DIALOG (empathy_irc_network_chooser_dialog_get_type ())
#define EMPATHY_IRC_NETWORK_CHOOSER_DIALOG(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), \
    EMPATHY_TYPE_IRC_NETWORK_CHOOSER_DIALOG, EmpathyIrcNetworkChooserDialog))
#define EMPATHY_IRC_NETWORK_CHOOSER_DIALOG_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), \
    EMPATHY_TYPE_IRC_NETWORK_CHOOSER_DIALOG, EmpathyIrcNetworkChooserDialogClass))
#define EMPATHY_IS_IRC_NETWORK_CHOOSER_DIALOG(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
    EMPATHY_TYPE_IRC_NETWORK_CHOOSER_DIALOG))
#define EMPATHY_IS_IRC_NETWORK_CHOOSER_DIALOG_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), \
    EMPATHY_TYPE_IRC_NETWORK_CHOOSER_DIALOG))
#define EMPATHY_IRC_NETWORK_CHOOSER_DIALOG_GET_CLASS(o) ( \
    G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_IRC_NETWORK_CHOOSER_DIALOG, \
        EmpathyIrcNetworkChooserDialogClass))

typedef struct {
  GtkDialog parent;

  /*<private>*/
  gpointer priv;
} EmpathyIrcNetworkChooserDialog;

typedef struct {
  GtkDialogClass parent_class;
} EmpathyIrcNetworkChooserDialogClass;

GType empathy_irc_network_chooser_dialog_get_type (void) G_GNUC_CONST;

GtkWidget * empathy_irc_network_chooser_dialog_new (
    EmpathyAccountSettings *settings,
    EmpathyIrcNetwork *network);

EmpathyIrcNetwork * empathy_irc_network_chooser_dialog_get_network (
    EmpathyIrcNetworkChooserDialog *self);

gboolean empathy_irc_network_chooser_dialog_get_changed (
    EmpathyIrcNetworkChooserDialog *self);

G_END_DECLS

#endif /* __EMPATHY_IRC_NETWORK_CHOOSER_DIALOG_H__ */
