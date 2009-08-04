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

#include <libempathy/empathy-account-settings.h>
#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-account-widget.h>
#include <libempathy-gtk/empathy-protocol-chooser.h>
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

  GtkWidget *add_existing_page;
  GtkWidget *current_account_widget;
  EmpathyAccountWidget *current_widget_object;
} EmpathyAccountAssistantPriv;

static gint
account_assistant_page_forward_func (gint current_page,
    gpointer user_data)
{
  EmpathyAccountAssistant *self = user_data;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  gint retval;

  retval = current_page;

  if (current_page == 0)
    retval = priv->first_resp;

  g_print ("retval = %d\n", retval);
  return retval;
}

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

  w = gtk_label_new (
      _("With Empathy you can chat with people\n"
        "online nearby and with friends and colleagues\n"
        "who use Google Talk, AIM, Windows Live\n"
        "and many other chat programs. With a microphone\n"
        "or a webcam you can also have audio or video calls."));
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (hbox_1), w, TRUE, TRUE, 0);
  gtk_widget_show (w);

  pix = empathy_pixbuf_from_icon_name_sized ("empathy", 80);
  w = gtk_image_new_from_pixbuf (pix);
  gtk_box_pack_start (GTK_BOX (hbox_1), w, TRUE, TRUE, 6);
  gtk_widget_show (w);

  g_object_unref (pix);

  w = gtk_label_new (_("Do you have an account you've been using "
          "with another\nchat program?"));
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

static GtkWidget *
account_assistant_build_import_page (EmpathyAccountAssistant *self)
{
  /* TODO: import page */
  GtkWidget *main_vbox, *w;

  main_vbox = gtk_vbox_new (FALSE, 12);
  w = gtk_label_new ("Import your accounts!");
  gtk_widget_show (w);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 6);

  gtk_widget_show (main_vbox);

  return main_vbox;
}

static gboolean
account_assistant_chooser_enter_details_filter_func (
    TpConnectionManager *cm,
    TpConnectionManagerProtocol *protocol,
    gpointer user_data)
{
  /* TODO */
  return TRUE;
}

static void
account_assistant_handle_apply_cb (EmpathyAccountWidget *widget_object,
    gboolean is_valid,
    EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  gtk_assistant_set_page_complete (GTK_ASSISTANT (self),
      priv->add_existing_page, is_valid);
}

static void
account_assistant_protocol_changed_cb (GtkComboBox *chooser,
    EmpathyAccountAssistant *self)
{
  TpConnectionManager *cm;
  TpConnectionManagerProtocol *proto;
  EmpathyAccountSettings *settings;
  EmpathyAccountAssistantPriv *priv;
  char *str;
  GtkWidget *account_widget;
  EmpathyAccountWidget *widget_object = NULL;

  priv = GET_PRIV (self);

  cm = empathy_protocol_chooser_dup_selected (
      EMPATHY_PROTOCOL_CHOOSER (chooser), &proto);

  if (cm == NULL || proto == NULL)
    /* we are not ready yet */
    return;

  /* Create account */
  /* To translator: %s is the protocol name */
  str = g_strdup_printf (_("New %s account"), proto->name);

  settings = empathy_account_settings_new (cm->name, proto->name, str);
  account_widget = empathy_account_widget_simple_new_for_protocol
    (proto->name, settings, &widget_object);

  if (priv->current_account_widget != NULL)
    {
      g_signal_handlers_disconnect_by_func (priv->current_widget_object,
          account_assistant_handle_apply_cb, self);
      gtk_widget_destroy (priv->current_account_widget);
    }

  priv->current_account_widget = account_widget;
  priv->current_widget_object = widget_object;

  g_signal_connect (priv->current_widget_object, "handle-apply",
      G_CALLBACK (account_assistant_handle_apply_cb), self);

  gtk_box_pack_start (GTK_BOX (priv->add_existing_page), account_widget, FALSE,
      FALSE, 0);
  gtk_widget_show (account_widget);

  g_free (str);
}

static GtkWidget *
account_assistant_build_enter_details_page (EmpathyAccountAssistant *self)
{
  GtkWidget *main_vbox, *w, *chooser, *hbox;
  PangoAttrList *list;

  main_vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_widget_show (main_vbox);

  w = gtk_label_new (_("What kind of chat account do you have?"));
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);

  w = gtk_alignment_new (0, 0, 0, 0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (w), 0, 0, 12, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  chooser = empathy_protocol_chooser_new ();
  empathy_protocol_chooser_set_visible (EMPATHY_PROTOCOL_CHOOSER (chooser),
      account_assistant_chooser_enter_details_filter_func, self);
  gtk_container_add (GTK_CONTAINER (w), chooser);
  gtk_widget_show (chooser);

  g_signal_connect (chooser, "changed",
      G_CALLBACK (account_assistant_protocol_changed_cb), self);

  /* trigger show the first account widget */
  account_assistant_protocol_changed_cb (GTK_COMBO_BOX (chooser), self);

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_end (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  w = gtk_image_new_from_icon_name ("gtk-dialog-info", GTK_ICON_SIZE_BUTTON);
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  w = gtk_label_new (_("If you have other accounts to set up, you can do "
          "that at any time\nfrom the Edit menu."));
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
  list = pango_attr_list_new ();
  pango_attr_list_insert (list, pango_attr_scale_new (PANGO_SCALE_SMALL));
  gtk_label_set_attributes (GTK_LABEL (w), list);
  gtk_widget_show (w);
  pango_attr_list_unref (list);

  return main_vbox;
}

static GtkWidget *
account_assistant_build_create_account_page (EmpathyAccountAssistant *self)
{
  GtkWidget *main_vbox, *w;

  main_vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_widget_show (main_vbox);

  w = gtk_label_new (_("What kind of chat account do you want to create?"));
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

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
  GtkAssistant *assistant = GTK_ASSISTANT (self);
  GtkWidget *page;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (self, EMPATHY_TYPE_ACCOUNT_ASSISTANT,
      EmpathyAccountAssistantPriv);
  self->priv = priv;

  gtk_assistant_set_forward_page_func (assistant,
      account_assistant_page_forward_func, self, NULL);

  /* first page (introduction) */
  page = account_assistant_build_introduction_page (self);
  gtk_assistant_append_page (assistant, page);
  gtk_assistant_set_page_title (assistant, page,
      _("Welcome to Empathy"));
  gtk_assistant_set_page_type (assistant, page,
      GTK_ASSISTANT_PAGE_INTRO);
  gtk_assistant_set_page_complete (assistant, page, TRUE);

  /* set a default answer */
  priv->first_resp = RESPONSE_IMPORT;

  /* second page (import accounts) */
  page = account_assistant_build_import_page (self);
  gtk_assistant_append_page (assistant, page);
  gtk_assistant_set_page_title (assistant, page,
      _("Import your existing accounts"));
  gtk_assistant_set_page_complete (assistant, page, TRUE);

  /* third page (enter account details) */
  page = account_assistant_build_enter_details_page (self);
  gtk_assistant_append_page (assistant, page);
  gtk_assistant_set_page_title (assistant, page,
      _("Enter your account details"));
  gtk_assistant_set_page_type (assistant, page, GTK_ASSISTANT_PAGE_CONFIRM);
  priv->add_existing_page = page;

  /* fourth page (create a new account) */
  page = account_assistant_build_create_account_page (self);
  gtk_assistant_append_page (assistant, page);
  gtk_assistant_set_page_title (assistant, page,
      _("Enter the details of the new account"));  
}

GtkWidget *
empathy_account_assistant_new (void)
{
  return g_object_new (EMPATHY_TYPE_ACCOUNT_ASSISTANT, NULL);
}
