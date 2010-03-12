/*
 * empathy-invite-participant-dialog.h
 *
 * EmpathyInviteParticipantDialog
 *
 * (c) 2009, Collabora Ltd.
 *
 * Authors:
 *    Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#ifndef __EMPATHY_INVITE_PARTICIPANT_DIALOG_H__
#define __EMPATHY_INVITE_PARTICIPANT_DIALOG_H__

#include <gtk/gtk.h>

#include <libempathy-gtk/empathy-contact-selector-dialog.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG	(empathy_invite_participant_dialog_get_type ())
#define EMPATHY_INVITE_PARTICIPANT_DIALOG(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG, EmpathyInviteParticipantDialog))
#define EMPATHY_INVITE_PARTICIPANT_DIALOG_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG, EmpathyInviteParticipantDialogClass))
#define EMPATHY_IS_INVITE_PARTICIPANT_DIALOG(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG))
#define EMPATHY_IS_INVITE_PARTICIPANT_DIALOG_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG))
#define EMPATHY_INVITE_PARTICIPANT_DIALOG_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG, EmpathyInviteParticipantDialogClass))

typedef struct _EmpathyInviteParticipantDialog EmpathyInviteParticipantDialog;
typedef struct _EmpathyInviteParticipantDialogClass EmpathyInviteParticipantDialogClass;

struct _EmpathyInviteParticipantDialog
{
  EmpathyContactSelectorDialog parent;
};

struct _EmpathyInviteParticipantDialogClass
{
  EmpathyContactSelectorDialogClass parent_class;
};

GType empathy_invite_participant_dialog_get_type (void);
GtkWidget *empathy_invite_participant_dialog_new (GtkWindow *parent,
    TpAccount *account);

G_END_DECLS

#endif
