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

G_DEFINE_TYPE (EmpathyAccountAssistant, empathy_account_assistant,
    GTK_TYPE_ASSISTANT)

static GtkWidget *
account_assistant_build_introduction_page (void)
{
  GtkWidget *main_vbox, *hbox_1, *w;

  main_vbox = gtk_vbox_new (FALSE, 12);
  gtk_widget_show (main_vbox);
  
  hbox_1 = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox_1, TRUE, TRUE, 6);
  gtk_widget_show (hbox_1);

  w = gtk_label_new (_("With Empathy you can chat with people online nearby "
          "and with friends and colleagues who use Google Talk, AIM, "
          "Windows Live and many other chat programs. With a microphone "
          "or a webcam you can also have audio or video calls."));
  gtk_box_pack_start (GTK_BOX (hbox_1), w, TRUE, TRUE, 6);
  gtk_widget_show (w);

  w = gtk_image_new_from_icon_name ("empathy", GTK_ICON_SIZE_DIALOG);
  gtk_box_pack_start (GTK_BOX (hbox_1), w, TRUE, TRUE, 6);
  gtk_widget_show (w);

  return main_vbox;
}

static void
empathy_account_assistant_class_init (EmpathyAccountAssistantClass *klass)
{

}

static void
empathy_account_assistant_init (EmpathyAccountAssistant *self)
{
  GtkWidget *page;

  page = account_assistant_build_introduction_page ();
  gtk_assistant_append_page (GTK_ASSISTANT (self), page);
}

EmpathyAccountAssistant*
empathy_account_assistant_new (void)
{
  return g_object_new (EMPATHY_TYPE_ACCOUNT_ASSISTANT, NULL);
}
