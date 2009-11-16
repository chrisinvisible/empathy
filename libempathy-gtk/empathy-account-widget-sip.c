/*
 * Copyright (C) 2007-2008 Guillaume Desmottes
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
 *          Frederic Peters <fpeters@0d.be>
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-utils.h>

#include "empathy-account-widget.h"
#include "empathy-account-widget-private.h"
#include "empathy-account-widget-sip.h"
#include "empathy-ui-utils.h"

typedef struct {
  EmpathyAccountWidget *self;
  GtkWidget *vbox_settings;

  GtkWidget *entry_stun_server;
  GtkWidget *spinbutton_stun_part;
  GtkWidget *checkbutton_discover_stun;
  GtkWidget *combobox_transport;
  GtkWidget *combobox_keep_alive_mechanism;
} EmpathyAccountWidgetSip;

static void
account_widget_sip_destroy_cb (GtkWidget *widget,
                               EmpathyAccountWidgetSip *settings)
{
  g_slice_free (EmpathyAccountWidgetSip, settings);
}

static void
account_widget_sip_discover_stun_toggled_cb (
    GtkWidget *checkbox,
    EmpathyAccountWidgetSip *settings)
{
  gboolean active;

  active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox));
  gtk_widget_set_sensitive (settings->entry_stun_server, !active);
  gtk_widget_set_sensitive (settings->spinbutton_stun_part, !active);
}

void
empathy_account_widget_sip_build (EmpathyAccountWidget *self,
    const char *filename,
    GtkWidget **table_common_settings)
{
  EmpathyAccountWidgetSip *settings;
  GtkWidget *vbox_settings;
  gboolean is_simple;
  GtkWidget *table_advanced;

  g_object_get (self, "simple", &is_simple, NULL);

  if (is_simple)
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_sip_simple", &vbox_settings,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_userid_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_userid_simple");
    }
  else
    {
      settings = g_slice_new0 (EmpathyAccountWidgetSip);
      settings->self = self;

      self->ui_details->gui = empathy_builder_get_file (filename,
          "table_common_settings", table_common_settings,
          "table_advanced_sip_settings", &table_advanced,
          "vbox_sip_settings", &vbox_settings,
          "entry_stun-server", &settings->entry_stun_server,
          "spinbutton_stun-port", &settings->spinbutton_stun_part,
          "checkbutton_discover-stun", &settings->checkbutton_discover_stun,
          NULL);
      settings->vbox_settings = vbox_settings;

      empathy_account_widget_handle_params (self,
          "entry_userid", "account",
          "entry_password", "password",
          "checkbutton_discover-stun", "discover-stun",
          "entry_stun-server", "stun-server",
          "spinbutton_stun-port", "stun-port",
          "entry_auth-user", "auth-user",
          "entry_proxy-host", "proxy-host",
          "spinbutton_port", "port",
          "checkbutton_loose-routing", "loose-routing",
          "checkbutton_discover-binding", "discover-binding",
          "spinbutton_keepalive-interval", "keepalive-interval",
          NULL);

      account_widget_sip_discover_stun_toggled_cb (
          settings->checkbutton_discover_stun,
          settings);

      empathy_builder_connect (self->ui_details->gui, settings,
          "vbox_sip_settings", "destroy", account_widget_sip_destroy_cb,
          "checkbutton_discover-stun", "toggled",
          account_widget_sip_discover_stun_toggled_cb,
          NULL);

      self->ui_details->add_forget = TRUE;
      self->ui_details->default_focus = g_strdup ("entry_userid");

      /* Create the 'transport' combobox as Glade doesn't allow us to create a
       * GtkComboBox using gtk_combo_box_new_text () */
      settings->combobox_transport = gtk_combo_box_new_text ();

      gtk_combo_box_append_text (GTK_COMBO_BOX (settings->combobox_transport),
          "auto");
      gtk_combo_box_append_text (GTK_COMBO_BOX (settings->combobox_transport),
          "udp");
      gtk_combo_box_append_text (GTK_COMBO_BOX (settings->combobox_transport),
          "tcp");
      gtk_combo_box_append_text (GTK_COMBO_BOX (settings->combobox_transport),
          "tls");

      account_widget_setup_widget (self, settings->combobox_transport,
          "transport");

      gtk_table_attach_defaults (GTK_TABLE (table_advanced),
          settings->combobox_transport, 1, 2, 6, 7);

      gtk_widget_show (settings->combobox_transport);

      /* Create the 'keep-alive mechanism' combo box */
      settings->combobox_keep_alive_mechanism = gtk_combo_box_new_text ();

      gtk_combo_box_append_text (
          GTK_COMBO_BOX (settings->combobox_keep_alive_mechanism), "auto");
      gtk_combo_box_append_text (
          GTK_COMBO_BOX (settings->combobox_keep_alive_mechanism), "register");
      gtk_combo_box_append_text (
          GTK_COMBO_BOX (settings->combobox_keep_alive_mechanism), "options");
      gtk_combo_box_append_text (
          GTK_COMBO_BOX (settings->combobox_keep_alive_mechanism), "none");

      account_widget_setup_widget (self,
          settings->combobox_keep_alive_mechanism, "keepalive-mechanism");

      gtk_table_attach_defaults (GTK_TABLE (table_advanced),
          settings->combobox_keep_alive_mechanism, 1, 2, 9, 10);

      gtk_widget_show (settings->combobox_keep_alive_mechanism);
    }

  self->ui_details->widget = vbox_settings;
}
