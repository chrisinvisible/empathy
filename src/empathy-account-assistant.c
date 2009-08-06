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
#include <telepathy-glib/util.h>

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

enum {
  PAGE_INTRO = 0,
  PAGE_IMPORT = 1,
  PAGE_ENTER_CREATE = 2,
  PAGE_SALUT_ONLY = 3
};

enum {
  PROP_PARENT = 1
};

typedef struct {
  FirstPageResponse first_resp;

  /* enter or create page */
  GtkWidget *enter_or_create_page;
  GtkWidget *current_account_widget;
  EmpathyAccountWidget *current_widget_object;
  GtkWidget *first_label;
  GtkWidget *second_label;
  GtkWidget *chooser;
  EmpathyAccountSettings *settings;

  GtkWindow *parent_window;
} EmpathyAccountAssistantPriv;

static GtkWidget *
account_assistant_build_error_page (EmpathyAccountAssistant *self,
    GError *error, gint page_num)
{
  GtkWidget *main_vbox, *w, *hbox;
  GString *str;
  char *message;
  PangoAttrList *list;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  main_vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_widget_show (main_vbox);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  w = gtk_image_new_from_stock (GTK_STOCK_DIALOG_ERROR,
      GTK_ICON_SIZE_DIALOG);
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  /* translators: this is followed by the "while ..." strings some lines
   * down this file.
   */
  str = g_string_new (_("There has been an error\n"));

  if (page_num == PAGE_IMPORT)
    /* translators: this follows the "There has been an error " string */
    str = g_string_append (str, _("while importing the accounts."));
  else if (page_num == PAGE_ENTER_CREATE &&
      priv->first_resp == RESPONSE_ENTER_ACCOUNT)
    /* translators: this follows the "There has been an error " string */
    str = g_string_append (str, _("while parsing the account details."));
  else if (page_num == PAGE_ENTER_CREATE &&
      priv->first_resp == RESPONSE_CREATE_ACCOUNT)
    /* translators: this follows the "There has been an error " string */
    str = g_string_append (str, _("while creating the account."));

  message = g_string_free (str, FALSE);

  w = gtk_label_new (message);
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
  list = pango_attr_list_new ();
  pango_attr_list_insert (list, pango_attr_scale_new (PANGO_SCALE_LARGE));
  pango_attr_list_insert (list, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes (GTK_LABEL (w), list);
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
  gtk_widget_show (w);

  g_free (message);
  pango_attr_list_unref (list);

  message = g_markup_printf_escaped
    (_("The error message was: <span style=\"italic\">%s</span>"),
        error->message);
  w = gtk_label_new (message);
  gtk_label_set_use_markup (GTK_LABEL (w), TRUE);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
  gtk_widget_show (w);

  w = gtk_label_new (_("You can either go back and try to enter your "
          "accounts' details\nagain or quit this wizard and add accounts "
          "later from the Edit menu."));
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 6);
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
  gtk_widget_show (w);

  return main_vbox;
}

static void
account_assistant_back_button_clicked_cb (GtkButton *button,
    EmpathyAccountAssistant *self)
{
  gint page_num;

  page_num = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
          "page-num"));
  gtk_assistant_remove_action_widget (GTK_ASSISTANT (self),
      GTK_WIDGET (button));
  gtk_assistant_set_current_page (GTK_ASSISTANT (self), page_num);
}

static void
account_assistant_present_error_page (EmpathyAccountAssistant *self,
    GError *error, gint page_num)
{
  GtkWidget *error_page, *back_button;
  gint num;

  error_page = account_assistant_build_error_page (self, error,
      page_num);
  num = gtk_assistant_append_page (GTK_ASSISTANT (self), error_page);
  gtk_assistant_set_page_title (GTK_ASSISTANT (self), error_page,
      _("An error occurred"));
  gtk_assistant_set_page_type (GTK_ASSISTANT (self), error_page,
      GTK_ASSISTANT_PAGE_SUMMARY);

  back_button = gtk_button_new_from_stock (GTK_STOCK_GO_BACK);
  gtk_assistant_add_action_widget (GTK_ASSISTANT (self), back_button);
  g_object_set_data (G_OBJECT (back_button),
      "page-num", GINT_TO_POINTER (page_num));
  g_signal_connect (back_button, "clicked",
      G_CALLBACK (account_assistant_back_button_clicked_cb), self);
  gtk_widget_show (back_button);

  gtk_assistant_set_current_page (GTK_ASSISTANT (self), num);
}

static void
account_assistant_apply_account_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GError *error = NULL;
  EmpathyAccountAssistant *self = user_data;

  empathy_account_settings_apply_finish (EMPATHY_ACCOUNT_SETTINGS (source),
      result, &error);

  if (error != NULL)
    {
      account_assistant_present_error_page (self, error, PAGE_ENTER_CREATE);
      g_error_free (error);
    }
}

static void
account_assistant_apply_account_and_finish (EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  if (priv->settings == NULL)
    return;

  empathy_account_settings_apply_async (priv->settings,
      account_assistant_apply_account_cb, self);
}

static void
account_assistant_apply_cb (GtkAssistant *assistant,
    gpointer user_data)
{
  EmpathyAccountAssistant *self = EMPATHY_ACCOUNT_ASSISTANT (assistant);
  gint current_page;

  current_page = gtk_assistant_get_current_page (assistant);

  if (current_page == RESPONSE_ENTER_ACCOUNT)
    account_assistant_apply_account_and_finish (self);
}

static void
account_assistant_handle_apply_cb (EmpathyAccountWidget *widget_object,
    gboolean is_valid,
    EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  gtk_assistant_set_page_complete (GTK_ASSISTANT (self),
      priv->enter_or_create_page, is_valid);
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

  if (priv->first_resp == RESPONSE_CREATE_ACCOUNT)
    empathy_account_settings_set_boolean (settings, "register", TRUE);

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

  if (priv->settings != NULL)
    g_object_unref (priv->settings);

  priv->settings = settings;

  g_signal_connect (priv->current_widget_object, "handle-apply",
      G_CALLBACK (account_assistant_handle_apply_cb), self);

  gtk_box_pack_start (GTK_BOX (priv->enter_or_create_page), account_widget,
      FALSE, FALSE, 0);
  gtk_widget_show (account_widget);

  g_free (str);
}

static gboolean
account_assistant_chooser_enter_details_filter_func (
    TpConnectionManager *cm,
    TpConnectionManagerProtocol *protocol,
    gpointer user_data)
{
  if (!tp_strdiff (protocol->name, "local-xmpp") ||
      !tp_strdiff (protocol->name, "irc"))
    return FALSE;

  return TRUE;
}

static gboolean
account_assistant_chooser_create_account_filter_func (
    TpConnectionManager *cm,
    TpConnectionManagerProtocol *protocol,
    gpointer user_data)
{
  return tp_connection_manager_protocol_can_register (protocol);
}

static void
account_assistant_finish_enter_or_create_page (EmpathyAccountAssistant *self,
    gboolean is_enter)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  
  if (is_enter)
    {
      gtk_label_set_label (GTK_LABEL (priv->first_label),
          _("What kind of chat account do you have?"));
      gtk_label_set_label (GTK_LABEL (priv->second_label),
          _("If you have other accounts to set up, you can do "
              "that at any time\nfrom the Edit menu."));
      empathy_protocol_chooser_set_visible (
          EMPATHY_PROTOCOL_CHOOSER (priv->chooser),
          account_assistant_chooser_enter_details_filter_func, self);

      gtk_assistant_set_page_title (GTK_ASSISTANT (self),
          priv->enter_or_create_page, _("Enter your account details"));
    }
  else
    {
      gtk_label_set_label (GTK_LABEL (priv->first_label),
          _("What kind of chat account do you want to create?"));
      gtk_label_set_label (GTK_LABEL (priv->second_label),
          _("You can register other accounts, or setup\n"
              "an existing one at any time from the Edit menu."));
      empathy_protocol_chooser_set_visible (
          EMPATHY_PROTOCOL_CHOOSER (priv->chooser),
          account_assistant_chooser_create_account_filter_func, self);

      gtk_assistant_set_page_title (GTK_ASSISTANT (self),
          priv->enter_or_create_page,
          _("Enter the details for the new account"));
    }
    
  g_signal_connect (priv->chooser, "changed",
      G_CALLBACK (account_assistant_protocol_changed_cb), self);
 
  /* trigger show the first account widget */
  account_assistant_protocol_changed_cb (GTK_COMBO_BOX (priv->chooser), self);
}

static void
account_assistant_prepare_cb (GtkAssistant *assistant,
    GtkWidget *current_page,
    gpointer user_data)
{
  EmpathyAccountAssistant *self = EMPATHY_ACCOUNT_ASSISTANT (assistant);
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  gint current_idx;

  current_idx = gtk_assistant_get_current_page (assistant);

  g_print ("prepare, current idx = %d\n", current_idx);

  if (current_idx == PAGE_ENTER_CREATE)
    {
      account_assistant_finish_enter_or_create_page (self,
          priv->first_resp == RESPONSE_ENTER_ACCOUNT ?
          TRUE : FALSE);
    }
}

static gint
account_assistant_page_forward_func (gint current_page,
    gpointer user_data)
{
  EmpathyAccountAssistant *self = user_data;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  gint retval;

  retval = current_page;

  if (current_page == 0)
    {
      if (priv->first_resp == RESPONSE_ENTER_ACCOUNT ||
          priv->first_resp == RESPONSE_CREATE_ACCOUNT)
        retval = PAGE_ENTER_CREATE;
    }

  if (current_page == PAGE_ENTER_CREATE)
    {
      /* don't forward anymore */
      retval = -1;
    }

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

static GtkWidget *
account_assistant_build_enter_or_create_page (EmpathyAccountAssistant *self,
    gboolean is_enter)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  GtkWidget *main_vbox, *w, *chooser, *hbox;
  PangoAttrList *list;

  main_vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_widget_show (main_vbox);

  w = gtk_label_new (NULL);
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);
  priv->first_label = w;

  w = gtk_alignment_new (0, 0, 0, 0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (w), 0, 0, 12, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  chooser = empathy_protocol_chooser_new ();
  gtk_container_add (GTK_CONTAINER (w), chooser);
  gtk_widget_show (chooser);
  priv->chooser = chooser;

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_end (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  w = gtk_image_new_from_icon_name ("gtk-dialog-info", GTK_ICON_SIZE_BUTTON);
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  w = gtk_label_new (NULL);
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
  list = pango_attr_list_new ();
  pango_attr_list_insert (list, pango_attr_scale_new (PANGO_SCALE_SMALL));
  gtk_label_set_attributes (GTK_LABEL (w), list);
  gtk_widget_show (w);
  priv->second_label = w;
  pango_attr_list_unref (list);

  return main_vbox;
}

static void
do_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_PARENT:
      g_value_set_object (value, priv->parent_window);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
do_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_PARENT:
      priv->parent_window = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
do_constructed (GObject *object)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (object);

  /* set us as transient for the parent window if any */
  if (priv->parent_window)
    gtk_window_set_transient_for (GTK_WINDOW (object),
        priv->parent_window);

  gtk_window_set_type_hint (GTK_WINDOW (object), GDK_WINDOW_TYPE_HINT_DIALOG);
}

static void
empathy_account_assistant_class_init (EmpathyAccountAssistantClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  oclass->get_property = do_get_property;
  oclass->set_property = do_set_property;
  oclass->constructed = do_constructed;

  param_spec = g_param_spec_object ("parent-window",
      "parent-window", "The parent window",
      GTK_TYPE_WINDOW,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_PARENT, param_spec);

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

  g_signal_connect (self, "apply",
      G_CALLBACK (account_assistant_apply_cb), NULL);
  g_signal_connect (self, "prepare",
      G_CALLBACK (account_assistant_prepare_cb), NULL);

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
  page = account_assistant_build_enter_or_create_page (self, TRUE);
  gtk_assistant_append_page (assistant, page);
  gtk_assistant_set_page_type (assistant, page, GTK_ASSISTANT_PAGE_CONFIRM);
  priv->enter_or_create_page = page;
}

GtkWidget *
empathy_account_assistant_new (GtkWindow *window)
{
  return g_object_new (EMPATHY_TYPE_ACCOUNT_ASSISTANT, "parent-window",
      window, NULL);
}
