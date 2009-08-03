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

/* empathy-account-assistant.c */

#include <glib/gi18n.h>

#include "empathy-account-assistant.h"

#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-ui-utils.h>

G_DEFINE_TYPE (EmpathyAccountAssistant, empathy_account_assistant,
    GTK_TYPE_ASSISTANT)

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyAccountAssistant)

typedef enum {
  RESPONSE_IMPORT = 1,
  RESPONSE_ENTER_ACCOUNT = 2,
  RESPONSE_CREATE_ACCOUNT = 3,
  RESPONSE_SALUT_ONLY = 4
} FirstPageResponse;

typedef struct {
  FirstPageResponse first_resp;
} EmpathyAccountAssistantPriv;

static void
account_assistant_radio_choice_toggled_cb (GtkToggleButton *button,
    EmpathyAccountAssistant *self)
{
  FirstPageResponse response;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  response = GPOINTER_TO_INT (g_object_get_data
      (G_OBJECT (button), "response"));

  g_print ("choice %d toggled\n", response);
  priv->first_resp = response;
}

static GtkWidget *
account_assistant_build_introduction_page (EmpathyAccountAssistant *self)
{
  GtkWidget *main_vbox, *hbox_1, *w, *radio, *vbox_1;
  GdkPixbuf *pix;

  main_vbox = gtk_vbox_new (FALSE, 12);
  gtk_widget_show (main_vbox);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);

  hbox_1 = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox_1, TRUE, TRUE, 0);
  gtk_widget_show (hbox_1);

  w = gtk_label_new (_("With Empathy you can chat with people \nonline nearby "
          "and with friends and colleagues \nwho use Google Talk, AIM, "
          "Windows Live \nand many other chat programs. With a microphone \n"
          "or a webcam you can also have audio or video calls."));
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (hbox_1), w, TRUE, TRUE, 0);
  gtk_widget_show (w);

  pix = empathy_pixbuf_from_icon_name_sized ("empathy", 96);
  w = gtk_image_new_from_pixbuf (pix);
  gtk_box_pack_start (GTK_BOX (hbox_1), w, TRUE, TRUE, 6);
  gtk_widget_show (w);

  g_object_unref (pix);

  w = gtk_label_new (_("Do you have an account you've been using with another "
          "chat program?"));
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  w = gtk_alignment_new (0, 0, 0, 0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (w), 0, 0, 12, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, TRUE, TRUE, 0);
  gtk_widget_show (w);

  vbox_1 = gtk_vbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (w), vbox_1);
  gtk_widget_show (vbox_1);

  /* TODO: this will have to be updated when kutio's branch have landed */
  radio = gtk_radio_button_new_with_label (NULL,
      _("Yes, import my account details from "));
  gtk_box_pack_start (GTK_BOX (vbox_1), radio, TRUE, TRUE, 0);
  g_object_set_data (G_OBJECT (radio), "response",
      GINT_TO_POINTER (RESPONSE_IMPORT));
  gtk_widget_show (radio);

  g_signal_connect (radio, "clicked",
      G_CALLBACK (account_assistant_radio_choice_toggled_cb), self);

  w = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (radio),
      _("Yes, I'll enter my account details now"));
  gtk_box_pack_start (GTK_BOX (vbox_1), w, TRUE, TRUE, 0);
  g_object_set_data (G_OBJECT (w), "response",
      GINT_TO_POINTER (RESPONSE_ENTER_ACCOUNT));
  gtk_widget_show (w);

  g_signal_connect (w, "clicked",
      G_CALLBACK (account_assistant_radio_choice_toggled_cb), self);

  w = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (radio),
      _("No, I want a new account"));
  gtk_box_pack_start (GTK_BOX (vbox_1), w, TRUE, TRUE, 0);
  g_object_set_data (G_OBJECT (w), "response",
      GINT_TO_POINTER (RESPONSE_CREATE_ACCOUNT));
  gtk_widget_show (w);

  g_signal_connect (w, "clicked",
      G_CALLBACK (account_assistant_radio_choice_toggled_cb), self);

  w = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (radio),
      _("No, I just want to see people online nearby for now"));
  gtk_box_pack_start (GTK_BOX (vbox_1), w, TRUE, TRUE, 0);
  g_object_set_data (G_OBJECT (w), "response",
      GINT_TO_POINTER (RESPONSE_SALUT_ONLY));
  gtk_widget_show (w);

  g_signal_connect (w, "clicked",
      G_CALLBACK (account_assistant_radio_choice_toggled_cb), self);

  return main_vbox;
}

static void
empathy_account_assistant_class_init (EmpathyAccountAssistantClass *klass)
{
  g_type_class_add_private (klass, sizeof (EmpathyAccountAssistantPriv));
}

static void
empathy_account_assistant_init (EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv;
  GtkWidget *page;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (self, EMPATHY_TYPE_ACCOUNT_ASSISTANT,
      EmpathyAccountAssistantPriv);
  self->priv = priv;

  /* first page */
  page = account_assistant_build_introduction_page (self);
  gtk_assistant_append_page (GTK_ASSISTANT (self), page);
  gtk_assistant_set_page_title (GTK_ASSISTANT (self), page,
      _("Welcome to Empathy"));
  gtk_assistant_set_page_type (GTK_ASSISTANT (self), page,
      GTK_ASSISTANT_PAGE_INTRO);
  gtk_assistant_set_page_complete (GTK_ASSISTANT (self), page, TRUE);
}

GtkWidget *
empathy_account_assistant_new (void)
{
  return g_object_new (EMPATHY_TYPE_ACCOUNT_ASSISTANT, NULL);
}
