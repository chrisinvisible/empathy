/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006-2007 Imendio AB
 * Copyright (C) 2007-2009 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#include <config.h>

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-account.h>

#include <telepathy-glib/connection-manager.h>
#include <telepathy-glib/util.h>
#include <dbus/dbus-protocol.h>

#include "empathy-account-widget.h"
#include "empathy-account-widget-private.h"
#include "empathy-account-widget-sip.h"
#include "empathy-account-widget-irc.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

G_DEFINE_TYPE (EmpathyAccountWidget, empathy_account_widget, G_TYPE_OBJECT)

typedef struct {
  char *protocol;
  EmpathyAccountSettings *settings;

  GtkWidget *apply_button;
  GtkWidget *entry_password;
  GtkWidget *button_forget;
  GtkWidget *spinbutton_port;

  gboolean simple;

  gboolean dispose_run;
} EmpathyAccountWidgetPriv;

enum {
  PROP_PROTOCOL = 1,
  PROP_SETTINGS,
  PROP_SIMPLE
};

enum {
  HANDLE_APPLY,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyAccountWidget)

static void
account_widget_handle_apply_sensitivity (EmpathyAccountWidget *self)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);
  gboolean is_valid;

  is_valid = empathy_account_settings_is_valid (priv->settings);

  if (!priv->simple)
    gtk_widget_set_sensitive (priv->apply_button, is_valid);

  g_signal_emit (self, signals[HANDLE_APPLY], 0, is_valid);
}

static gboolean
account_widget_entry_focus_cb (GtkWidget *widget,
    GdkEventFocus *event,
    EmpathyAccountWidget *self)
{
  const gchar *str;
  const gchar *param_name;
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  str = gtk_entry_get_text (GTK_ENTRY (widget));
  param_name = g_object_get_data (G_OBJECT (widget), "param_name");

  if (EMP_STR_EMPTY (str))
    {
      const gchar *value = NULL;

      empathy_account_settings_unset (priv->settings, param_name);
      value = empathy_account_settings_get_string (priv->settings, param_name);
      DEBUG ("Unset %s and restore to %s", param_name, value);
      gtk_entry_set_text (GTK_ENTRY (widget), value ? value : "");
    }
  else
    {
      DEBUG ("Setting %s to %s", param_name,
          strstr (param_name, "password") ? "***" : str);
      empathy_account_settings_set_string (priv->settings, param_name, str);
    }

  account_widget_handle_apply_sensitivity (self);

  return FALSE;
}

static void
account_widget_int_changed_cb (GtkWidget *widget,
    EmpathyAccountWidget *self)
{
  const gchar *param_name;
  gint value;
  const gchar *signature;
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  value = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (widget));
  param_name = g_object_get_data (G_OBJECT (widget), "param_name");

  signature = empathy_account_settings_get_dbus_signature (priv->settings,
    param_name);
  g_return_if_fail (signature != NULL);

  DEBUG ("Setting %s to %d", param_name, value);

  switch ((int)*signature)
    {
    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
      empathy_account_settings_set_int32 (priv->settings, param_name, value);
      break;
    case DBUS_TYPE_INT64:
      empathy_account_settings_set_int64 (priv->settings, param_name, value);
      break;
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
      empathy_account_settings_set_uint32 (priv->settings, param_name, value);
      break;
    case DBUS_TYPE_UINT64:
      empathy_account_settings_set_uint64 (priv->settings, param_name, value);
      break;
    default:
      g_return_if_reached ();
    }

  account_widget_handle_apply_sensitivity (self);
}

static void
account_widget_checkbutton_toggled_cb (GtkWidget *widget,
    EmpathyAccountWidget *self)
{
  gboolean     value;
  gboolean     default_value;
  const gchar *param_name;
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  param_name = g_object_get_data (G_OBJECT (widget), "param_name");

  /* FIXME: This is ugly! checkbox don't have a "not-set" value so we
   * always unset the param and set the value if different from the
   * default value. */
  empathy_account_settings_unset (priv->settings, param_name);
  default_value = empathy_account_settings_get_boolean (priv->settings,
      param_name);

  if (default_value == value)
    {
      DEBUG ("Unset %s and restore to %d", param_name, default_value);
    }
  else
    {
      DEBUG ("Setting %s to %d", param_name, value);
      empathy_account_settings_set_boolean (priv->settings, param_name, value);
    }

  account_widget_handle_apply_sensitivity (self);
}

static void
account_widget_forget_clicked_cb (GtkWidget *button,
    EmpathyAccountWidget *self)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);
  const gchar *param_name;

  param_name = g_object_get_data (G_OBJECT (priv->entry_password),
      "param_name");

  DEBUG ("Unset %s", param_name);
  empathy_account_settings_unset (priv->settings, param_name);
  gtk_entry_set_text (GTK_ENTRY (priv->entry_password), "");

  account_widget_handle_apply_sensitivity (self);
}

static void
account_widget_password_changed_cb (GtkWidget *entry,
    EmpathyAccountWidget *self)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);
  const gchar *str;

  str = gtk_entry_get_text (GTK_ENTRY (entry));
  gtk_widget_set_sensitive (priv->button_forget, !EMP_STR_EMPTY (str));
}

static void
account_widget_jabber_ssl_toggled_cb (GtkWidget *checkbutton_ssl,
    EmpathyAccountWidget *self)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);
  gboolean   value;
  gint32       port = 0;

  value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton_ssl));
  port = empathy_account_settings_get_uint32 (priv->settings, "port");

  if (value)
    {
      if (port == 5222 || port == 0)
        port = 5223;
    }
  else
    {
      if (port == 5223 || port == 0)
        port = 5222;
    }

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (priv->spinbutton_port), port);
}

static void
account_widget_setup_widget (EmpathyAccountWidget *self,
    GtkWidget *widget,
    const gchar *param_name)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  g_object_set_data_full (G_OBJECT (widget), "param_name",
      g_strdup (param_name), g_free);

  if (GTK_IS_SPIN_BUTTON (widget))
    {
      gint value = 0;
      const gchar *signature;

      signature = empathy_account_settings_get_dbus_signature (priv->settings,
          param_name);
      g_return_if_fail (signature != NULL);

      switch ((int)*signature)
        {
          case DBUS_TYPE_INT16:
          case DBUS_TYPE_INT32:
            value = empathy_account_settings_get_int32 (priv->settings,
              param_name);
            break;
          case DBUS_TYPE_INT64:
            value = empathy_account_settings_get_int64 (priv->settings,
              param_name);
            break;
          case DBUS_TYPE_UINT16:
          case DBUS_TYPE_UINT32:
            value = empathy_account_settings_get_uint32 (priv->settings,
              param_name);
            break;
          case DBUS_TYPE_UINT64:
            value = empathy_account_settings_get_uint64 (priv->settings,
                param_name);
            break;
          default:
            g_return_if_reached ();
        }

      gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), value);

      g_signal_connect (widget, "value-changed",
          G_CALLBACK (account_widget_int_changed_cb),
          self);
    }
  else if (GTK_IS_ENTRY (widget))
    {
      const gchar *str = NULL;

      str = empathy_account_settings_get_string (priv->settings, param_name);
      gtk_entry_set_text (GTK_ENTRY (widget), str ? str : "");

      if (strstr (param_name, "password"))
        {
          gtk_entry_set_visibility (GTK_ENTRY (widget), FALSE);
        }

      g_signal_connect (widget, "focus-out-event",
          G_CALLBACK (account_widget_entry_focus_cb),
          self);
    }
  else if (GTK_IS_TOGGLE_BUTTON (widget))
    {
      gboolean value = FALSE;

      value = empathy_account_settings_get_boolean (priv->settings,
          param_name);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), value);

      g_signal_connect (widget, "toggled",
          G_CALLBACK (account_widget_checkbutton_toggled_cb),
          self);
    }
  else
    {
      DEBUG ("Unknown type of widget for param %s", param_name);
    }
}

static gchar *
account_widget_generic_format_param_name (const gchar *param_name)
{
  gchar *str;
  gchar *p;

  str = g_strdup (param_name);

  if (str && g_ascii_isalpha (str[0]))
    str[0] = g_ascii_toupper (str[0]);

  while ((p = strchr (str, '-')) != NULL)
    {
      if (p[1] != '\0' && g_ascii_isalpha (p[1]))
        {
          p[0] = ' ';
          p[1] = g_ascii_toupper (p[1]);
        }

      p++;
    }

  return str;
}

static void
accounts_widget_generic_setup (EmpathyAccountWidget *self,
    GtkWidget *table_common_settings,
    GtkWidget *table_advanced_settings)
{
  TpConnectionManagerParam *params, *param;
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  params = empathy_account_settings_get_tp_params (priv->settings);

  for (param = params; param != NULL && param->name != NULL; param++)
    {
      GtkWidget       *table_settings;
      guint            n_rows = 0;
      GtkWidget       *widget = NULL;
      gchar           *param_name_formatted;

      if (param->flags & TP_CONN_MGR_PARAM_FLAG_REQUIRED)
        table_settings = table_common_settings;
      else if (priv->simple)
        return;
      else
        table_settings = table_advanced_settings;

      param_name_formatted = account_widget_generic_format_param_name
        (param->name);
      g_object_get (table_settings, "n-rows", &n_rows, NULL);
      gtk_table_resize (GTK_TABLE (table_settings), ++n_rows, 2);

      if (param->dbus_signature[0] == 's')
        {
          gchar *str;

          str = g_strdup_printf (_("%s:"), param_name_formatted);
          widget = gtk_label_new (str);
          gtk_misc_set_alignment (GTK_MISC (widget), 0, 0.5);
          g_free (str);

          gtk_table_attach (GTK_TABLE (table_settings),
              widget,
              0, 1,
              n_rows - 1, n_rows,
              GTK_FILL, 0,
              0, 0);
          gtk_widget_show (widget);

          widget = gtk_entry_new ();
          if (strcmp (param->name, "account") == 0)
            {
              g_signal_connect (widget, "realize",
                  G_CALLBACK (gtk_widget_grab_focus),
                  NULL);
            }
          gtk_table_attach (GTK_TABLE (table_settings),
              widget,
              1, 2,
              n_rows - 1, n_rows,
              GTK_FILL | GTK_EXPAND, 0,
              0, 0);
          gtk_widget_show (widget);
        }
      /* int types: ynqiuxt. double type is 'd' */
      else if (param->dbus_signature[0] == 'y' ||
          param->dbus_signature[0] == 'n' ||
          param->dbus_signature[0] == 'q' ||
          param->dbus_signature[0] == 'i' ||
          param->dbus_signature[0] == 'u' ||
          param->dbus_signature[0] == 'x' ||
          param->dbus_signature[0] == 't' ||
          param->dbus_signature[0] == 'd')
        {
          gchar   *str = NULL;
          gdouble  minint = 0;
          gdouble  maxint = 0;
          gdouble  step = 1;

          switch (param->dbus_signature[0])
            {
            case 'y': minint = G_MININT8;  maxint = G_MAXINT8;   break;
            case 'n': minint = G_MININT16; maxint = G_MAXINT16;  break;
            case 'q': minint = 0;          maxint = G_MAXUINT16; break;
            case 'i': minint = G_MININT32; maxint = G_MAXINT32;  break;
            case 'u': minint = 0;          maxint = G_MAXUINT32; break;
            case 'x': minint = G_MININT64; maxint = G_MAXINT64;  break;
            case 't': minint = 0;          maxint = G_MAXUINT64; break;
            case 'd': minint = G_MININT32; maxint = G_MAXINT32;
              step = 0.1; break;
            }

          str = g_strdup_printf (_("%s:"), param_name_formatted);
          widget = gtk_label_new (str);
          gtk_misc_set_alignment (GTK_MISC (widget), 0, 0.5);
          g_free (str);

          gtk_table_attach (GTK_TABLE (table_settings),
              widget,
              0, 1,
              n_rows - 1, n_rows,
              GTK_FILL, 0,
              0, 0);
          gtk_widget_show (widget);

          widget = gtk_spin_button_new_with_range (minint, maxint, step);
          gtk_table_attach (GTK_TABLE (table_settings),
              widget,
              1, 2,
              n_rows - 1, n_rows,
              GTK_FILL | GTK_EXPAND, 0,
              0, 0);
          gtk_widget_show (widget);
        }
      else if (param->dbus_signature[0] == 'b')
        {
          widget = gtk_check_button_new_with_label (param_name_formatted);
          gtk_table_attach (GTK_TABLE (table_settings),
              widget,
              0, 2,
              n_rows - 1, n_rows,
              GTK_FILL | GTK_EXPAND, 0,
              0, 0);
          gtk_widget_show (widget);
        }
      else
        {
          DEBUG ("Unknown signature for param %s: %s",
              param_name_formatted, param->dbus_signature);
        }

      if (widget)
        account_widget_setup_widget (self, widget, param->name);

      g_free (param_name_formatted);
    }
}

static void
account_widget_handle_params_valist (EmpathyAccountWidget *self,
    const gchar *first_widget,
    va_list args)
{
  GObject *object;
  const gchar *name;

  for (name = first_widget; name; name = va_arg (args, const gchar *))
    {
      const gchar *param_name;

      param_name = va_arg (args, const gchar *);
      object = gtk_builder_get_object (self->ui_details->gui, name);

      if (!object)
        {
          g_warning ("Builder is missing object '%s'.", name);
          continue;
        }

      account_widget_setup_widget (self, GTK_WIDGET (object), param_name);
    }
}

static void
account_widget_apply_clicked_cb (GtkWidget *button,
    EmpathyAccountWidget *self)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  empathy_account_settings_apply_async (priv->settings, NULL, NULL);
}

static void
account_widget_setup_generic (EmpathyAccountWidget *self)
{
  GtkWidget *table_common_settings;
  GtkWidget *table_advanced_settings;

  table_common_settings = GTK_WIDGET (gtk_builder_get_object
      (self->ui_details->gui, "table_common_settings"));
  table_advanced_settings = GTK_WIDGET (gtk_builder_get_object
      (self->ui_details->gui, "table_advanced_settings"));

  accounts_widget_generic_setup (self, table_common_settings,
      table_advanced_settings);

  g_object_unref (self->ui_details->gui);
}

static void
account_widget_settings_ready_cb (EmpathyAccountSettings *settings,
    GParamSpec *pspec,
    gpointer user_data)
{
  EmpathyAccountWidget *self = user_data;
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  if (empathy_account_settings_is_ready (priv->settings))
    account_widget_setup_generic (self);
}

static void
account_widget_build_generic (EmpathyAccountWidget *self,
    const char *filename)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);
  GtkWidget *expander_advanced;

  self->ui_details->gui = empathy_builder_get_file (filename,
      "vbox_generic_settings", &self->ui_details->widget,
      "expander_advanced_settings", &expander_advanced,
      NULL);

  if (priv->simple)
    gtk_widget_hide (expander_advanced);

  g_object_ref (self->ui_details->gui);

  if (empathy_account_settings_is_ready (priv->settings))
    account_widget_setup_generic (self);
  else
    g_signal_connect (priv->settings, "notify::ready",
        G_CALLBACK (account_widget_settings_ready_cb), self);
}

static void
account_widget_build_salut (EmpathyAccountWidget *self,
    const char *filename)
{
  self->ui_details->gui = empathy_builder_get_file (filename,
      "vbox_salut_settings", &self->ui_details->widget,
      NULL);

  empathy_account_widget_handle_params (self,
      "entry_published", "published-name",
      "entry_nickname", "nickname",
      "entry_first_name", "first-name",
      "entry_last_name", "last-name",
      "entry_email", "email",
      "entry_jid", "jid",
      NULL);

  self->ui_details->default_focus = g_strdup ("entry_nickname");
}

static void
account_widget_build_msn (EmpathyAccountWidget *self,
    const char *filename)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  if (priv->simple)
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_msn_simple", &self->ui_details->widget,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_id_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id_simple");
    }
  else
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_msn_settings", &self->ui_details->widget,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_id", "account",
          "entry_password", "password",
          "entry_server", "server",
          "spinbutton_port", "port",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id");
      self->ui_details->add_forget = TRUE;
    }
}

static void
account_widget_build_jabber (EmpathyAccountWidget *self,
    const char *filename)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);
  GtkWidget *spinbutton_port;
  GtkWidget *checkbutton_ssl;

  if (priv->simple)
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_jabber_simple", &self->ui_details->widget,
          NULL);
      
      empathy_account_widget_handle_params (self,
          "entry_id_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id_simple");
    }
  else
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_jabber_settings", &self->ui_details->widget,
          "spinbutton_port", &spinbutton_port,
          "checkbutton_ssl", &checkbutton_ssl,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_id", "account",
          "entry_password", "password",
          "entry_resource", "resource",
          "entry_server", "server",
          "spinbutton_port", "port",
          "spinbutton_priority", "priority",
          "checkbutton_ssl", "old-ssl",
          "checkbutton_ignore_ssl_errors", "ignore-ssl-errors",
          "checkbutton_encryption", "require-encryption",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id");
      self->ui_details->add_forget = TRUE;
      priv->spinbutton_port = spinbutton_port;

      g_signal_connect (checkbutton_ssl, "toggled",
          G_CALLBACK (account_widget_jabber_ssl_toggled_cb),
          self);
    }
}

static void
account_widget_build_icq (EmpathyAccountWidget *self,
    const char *filename)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);
  GtkWidget *spinbutton_port;

  if (priv->simple)
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_icq_simple", &self->ui_details->widget,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_uin_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_uin_simple");
    }
  else
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_icq_settings", &self->ui_details->widget,
          "spinbutton_port", &spinbutton_port,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_uin", "account",
          "entry_password", "password",
          "entry_server", "server",
          "spinbutton_port", "port",
          "entry_charset", "charset",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_uin");
      self->ui_details->add_forget = TRUE;
    }
}

static void
account_widget_build_aim (EmpathyAccountWidget *self,
    const char *filename)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);
  GtkWidget *spinbutton_port;

  if (priv->simple)
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_aim_simple", &self->ui_details->widget,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_screenname_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_screenname_simple");
    }
  else
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_aim_settings", &self->ui_details->widget,
          "spinbutton_port", &spinbutton_port,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_screenname", "account",
          "entry_password", "password",
          "entry_server", "server",
          "spinbutton_port", "port",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_screenname");
      self->ui_details->add_forget = TRUE;
    }
}

static void
account_widget_build_yahoo (EmpathyAccountWidget *self,
    const char *filename)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);
  
  if (priv->simple)
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_yahoo_simple", &self->ui_details->widget,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_id_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id_simple");
    }
  else
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_yahoo_settings", &self->ui_details->widget,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_id", "account",
          "entry_password", "password",
          "entry_server", "server",
          "entry_locale", "room-list-locale",
          "entry_charset", "charset",
          "spinbutton_port", "port",
          "checkbutton_yahoojp", "yahoojp",
          "checkbutton_ignore_invites", "ignore-invites",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id");
      self->ui_details->add_forget = TRUE;
    }
}

static void
account_widget_build_groupwise (EmpathyAccountWidget *self,
    const char *filename)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  if (priv->simple)
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_groupwise_simple", &self->ui_details->widget,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_id_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id_simple");
    }
  else
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_groupwise_settings", &self->ui_details->widget,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_id", "account",
          "entry_password", "password",
          "entry_server", "server",
          "spinbutton_port", "port",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id");
      self->ui_details->add_forget = TRUE;
    }
}

static void
account_widget_destroy_cb (GtkWidget *widget,
    EmpathyAccountWidget *self)
{
  g_object_unref (self);
}

static void
do_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
    case PROP_PROTOCOL:
      priv->protocol = g_value_dup_string (value);
      break;
    case PROP_SETTINGS:
      priv->settings = g_value_dup_object (value);
      break;
    case PROP_SIMPLE:
      priv->simple = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
do_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountWidgetPriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
    case PROP_PROTOCOL:
      g_value_set_string (value, priv->protocol);
      break;
    case PROP_SETTINGS:
      g_value_set_object (value, priv->settings);
      break;
    case PROP_SIMPLE:
      g_value_set_boolean (value, priv->simple);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
do_constructed (GObject *obj)
{
  EmpathyAccountWidget *self = EMPATHY_ACCOUNT_WIDGET (obj);
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);
  char *uiname, *filename;

  uiname = g_strconcat ("empathy-account-widget-", priv->protocol,
      ".ui", NULL);
  filename = empathy_file_lookup (uiname, "libempathy-gtk");

  if (!tp_strdiff (priv->protocol, "local-xmpp"))
    account_widget_build_salut (self, filename);
  else if (!tp_strdiff (priv->protocol, "msn"))
    account_widget_build_msn (self, filename);
  else if (!tp_strdiff (priv->protocol, "jabber"))
    account_widget_build_jabber (self, filename);
  else if (!tp_strdiff (priv->protocol, "icq"))
    account_widget_build_icq (self, filename);
  else if (!tp_strdiff (priv->protocol, "aim"))
    account_widget_build_aim (self, filename);
  else if (!tp_strdiff (priv->protocol, "yahoo"))
    account_widget_build_yahoo (self, filename);
  else if (!tp_strdiff (priv->protocol, "groupwise"))
    account_widget_build_groupwise (self, filename);
  else if (!tp_strdiff (priv->protocol, "irc"))
    empathy_account_widget_irc_build (self, filename);
  else if (!tp_strdiff (priv->protocol, "sip"))
    empathy_account_widget_sip_build (self, filename);
  else
    {
      g_free (filename);

      filename = empathy_file_lookup (
          "empathy-account-widget-generic.ui", "libempathy-gtk");
      account_widget_build_generic (self, filename);
    }

  g_free (uiname);
  g_free (filename);

  /* handle default focus */
  if (self->ui_details->default_focus != NULL)
    {
      GObject *default_focus_entry;

      default_focus_entry = gtk_builder_get_object
        (self->ui_details->gui, self->ui_details->default_focus);
      g_signal_connect (default_focus_entry, "realize",
          G_CALLBACK (gtk_widget_grab_focus),
          NULL);
    }

  /* handle forget button */
  if (self->ui_details->add_forget)
    {
      const gchar *password = NULL;

      priv->button_forget = GTK_WIDGET (gtk_builder_get_object
          (self->ui_details->gui, "button_forget"));
      priv->entry_password = GTK_WIDGET (gtk_builder_get_object
          (self->ui_details->gui, "entry_password"));

      password = empathy_account_settings_get_string (priv->settings,
          "password");
      gtk_widget_set_sensitive (priv->button_forget,
          !EMP_STR_EMPTY (password));

      g_signal_connect (priv->button_forget, "clicked",
          G_CALLBACK (account_widget_forget_clicked_cb),
          self);
      g_signal_connect (priv->entry_password, "changed",
          G_CALLBACK (account_widget_password_changed_cb),
          self);
    }

  /* handle apply button */
  if (!priv->simple)
    {
      priv->apply_button = gtk_button_new_from_stock (GTK_STOCK_APPLY);
      gtk_box_pack_end (GTK_BOX (self->ui_details->widget), priv->apply_button,
          FALSE, FALSE, 3);

      g_signal_connect (priv->apply_button, "clicked",
          G_CALLBACK (account_widget_apply_clicked_cb),
          self);
      account_widget_handle_apply_sensitivity (self);
      gtk_widget_show (priv->apply_button);
    }

  /* hook up to widget destruction to unref ourselves */
  g_signal_connect (self->ui_details->widget, "destroy",
      G_CALLBACK (account_widget_destroy_cb), self);

  empathy_builder_unref_and_keep_widget (self->ui_details->gui,
      self->ui_details->widget);
  self->ui_details->gui = NULL;
}

static void
do_dispose (GObject *obj)
{
  EmpathyAccountWidget *self = EMPATHY_ACCOUNT_WIDGET (obj);
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  if (priv->settings != NULL)
    {
      g_object_unref (priv->settings);
      priv->settings = NULL;
    }

  if (G_OBJECT_CLASS (empathy_account_widget_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (empathy_account_widget_parent_class)->dispose (obj);
}

static void
do_finalize (GObject *obj)
{
  EmpathyAccountWidget *self = EMPATHY_ACCOUNT_WIDGET (obj);
  EmpathyAccountWidgetPriv *priv = GET_PRIV (self);

  g_free (self->ui_details->default_focus);
  g_slice_free (EmpathyAccountWidgetUIDetails, self->ui_details);

  g_free (priv->protocol);

  if (G_OBJECT_CLASS (empathy_account_widget_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (empathy_account_widget_parent_class)->finalize (obj);
}

static void
empathy_account_widget_class_init (EmpathyAccountWidgetClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  oclass->get_property = do_get_property;
  oclass->set_property = do_set_property;
  oclass->constructed = do_constructed;
  oclass->dispose = do_dispose;
  oclass->finalize = do_finalize;

  param_spec = g_param_spec_string ("protocol",
      "protocol", "The protocol of the account",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_PROTOCOL, param_spec);

  param_spec = g_param_spec_object ("settings",
      "settings", "The settings of the account",
      EMPATHY_TYPE_ACCOUNT_SETTINGS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_SETTINGS, param_spec);

  param_spec = g_param_spec_boolean ("simple",
      "simple", "Whether the account widget is a simple or an advanced one",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_SIMPLE, param_spec);

  signals[HANDLE_APPLY] =
    g_signal_new ("handle-apply", G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST, 0, NULL, NULL,
        g_cclosure_marshal_VOID__BOOLEAN,
        G_TYPE_NONE,
        1, G_TYPE_BOOLEAN);

  g_type_class_add_private (klass, sizeof (EmpathyAccountWidgetPriv));
}

static void
empathy_account_widget_init (EmpathyAccountWidget *self)
{
  EmpathyAccountWidgetPriv *priv =
    G_TYPE_INSTANCE_GET_PRIVATE ((self), EMPATHY_TYPE_ACCOUNT_WIDGET,
        EmpathyAccountWidgetPriv);

  self->priv = priv;
  priv->dispose_run = FALSE;

  self->ui_details = g_slice_new0 (EmpathyAccountWidgetUIDetails);
}

/* public methods */

void
empathy_account_widget_handle_params (EmpathyAccountWidget *self,
    const gchar *first_widget,
    ...)
{
  va_list args;

  va_start (args, first_widget);
  account_widget_handle_params_valist (self, first_widget, args);
  va_end (args);
}

GtkWidget *
empathy_account_widget_new_for_protocol (const char *protocol,
    EmpathyAccountSettings *settings)
{
  EmpathyAccountWidget *self;
  EmpathyAccountWidgetPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_SETTINGS (settings), NULL);
  g_return_val_if_fail (settings != NULL, NULL);

  self = g_object_new
    (EMPATHY_TYPE_ACCOUNT_WIDGET, "protocol", protocol,
        "settings", settings, NULL);
  priv = GET_PRIV (self);

  return self->ui_details->widget;
}

GtkWidget *
empathy_account_widget_simple_new_for_protocol (const char *protocol,
    EmpathyAccountSettings *settings, EmpathyAccountWidget **object)
{
  EmpathyAccountWidget *self;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_SETTINGS (settings), NULL);
  g_return_val_if_fail (protocol != NULL, NULL);

  self = g_object_new
    (EMPATHY_TYPE_ACCOUNT_WIDGET, "protocol", protocol,
        "settings", settings, "simple", TRUE, NULL);

  *object = self;

  return self->ui_details->widget;
}
