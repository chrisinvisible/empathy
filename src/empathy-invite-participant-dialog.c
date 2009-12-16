/*
 * empathy-invite-participant-dialog.c
 *
 * EmpathyInviteParticipantDialog
 *
 * (c) 2009, Collabora Ltd.
 *
 * Authors:
 *    Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <glib/gi18n.h>

#include <libempathy/empathy-contact-manager.h>

#include <libempathy-gtk/empathy-contact-selector.h>

#include "empathy-invite-participant-dialog.h"

#define GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG, EmpathyInviteParticipantDialogPrivate))

G_DEFINE_TYPE (EmpathyInviteParticipantDialog, empathy_invite_participant_dialog, GTK_TYPE_DIALOG);

typedef struct _EmpathyInviteParticipantDialogPrivate EmpathyInviteParticipantDialogPrivate;
struct _EmpathyInviteParticipantDialogPrivate
{
  GtkWidget *selector;
};

static void
invite_participant_enable_join (GtkComboBox *selector,
                                GtkWidget   *button)
{
  gtk_widget_set_sensitive (button, TRUE);
}

static void
empathy_invite_participant_dialog_class_init (EmpathyInviteParticipantDialogClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (gobject_class, sizeof (EmpathyInviteParticipantDialogPrivate));
}

static void
empathy_invite_participant_dialog_init (EmpathyInviteParticipantDialog *self)
{
  EmpathyInviteParticipantDialogPrivate *priv = GET_PRIVATE (self);
  EmpathyContactManager *manager = empathy_contact_manager_dup_singleton ();
  GtkWidget *vbox = gtk_vbox_new (FALSE, 6);
  GtkWidget *label = gtk_label_new (NULL);
  GtkWidget *join_button;
  char *str;

  str = g_strdup_printf (
      "<span size=\"x-large\" weight=\"bold\">%s</span>\n\n%s",
      _("Invite Participant"),
      _("Choose a contact to invite into the conversation:"));
  gtk_label_set_markup (GTK_LABEL (label), str);
  g_free (str);

  priv->selector = empathy_contact_selector_new (
      EMPATHY_CONTACT_LIST (manager));

  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), priv->selector, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (self))),
        vbox, TRUE, TRUE, 6);

  gtk_widget_show_all (vbox);

  gtk_dialog_add_button (GTK_DIALOG (self),
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  join_button = gtk_dialog_add_button (GTK_DIALOG (self),
      "Invite", GTK_RESPONSE_OK);

  gtk_dialog_set_has_separator (GTK_DIALOG (self), FALSE);

  gtk_widget_set_sensitive (join_button, FALSE);
  g_signal_connect (priv->selector, "changed",
      G_CALLBACK (invite_participant_enable_join), join_button);

  g_object_unref (manager);
}

EmpathyContact *
empathy_invite_participant_dialog_dup_selected_contact (
    EmpathyInviteParticipantDialog *self)
{
  EmpathyInviteParticipantDialogPrivate *priv;

  g_return_val_if_fail (EMPATHY_IS_INVITE_PARTICIPANT_DIALOG (self), NULL);

  priv = GET_PRIVATE (self);

  return empathy_contact_selector_dup_selected (
      EMPATHY_CONTACT_SELECTOR (priv->selector));
}

GtkWidget *
empathy_invite_participant_dialog_new (GtkWindow *parent)
{
  GtkWidget *self = g_object_new (EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG, NULL);

  if (parent != NULL)
    {
      gtk_window_set_transient_for (GTK_WINDOW (self), parent);
    }

  return self;
}
