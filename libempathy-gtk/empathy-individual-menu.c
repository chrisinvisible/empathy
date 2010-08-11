/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008-2010 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <telepathy-glib/util.h>
#include <telepathy-logger/log-manager.h>

#include <libempathy/empathy-call-factory.h>
#include <libempathy/empathy-dispatcher.h>
#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-individual-manager.h>
#include <libempathy/empathy-chatroom-manager.h>
#include <libempathy/empathy-utils.h>

#include "empathy-individual-menu.h"
#include "empathy-images.h"
#include "empathy-log-window.h"
#include "empathy-contact-dialogs.h"
#include "empathy-individual-dialogs.h"
#include "empathy-ui-utils.h"
#include "empathy-share-my-desktop.h"
#include "empathy-linking-dialog.h"

GtkWidget *
empathy_individual_menu_new (FolksIndividual *individual,
    EmpathyIndividualFeatureFlags features)
{
  GtkWidget *menu;
  GtkMenuShell *shell;
  GtkWidget *item;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  if (features == EMPATHY_INDIVIDUAL_FEATURE_NONE)
    return NULL;

  menu = gtk_menu_new ();
  shell = GTK_MENU_SHELL (menu);

  /* Add Contact */
  item = empathy_individual_add_menu_item_new (individual);
  if (item)
    {
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Chat */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_CHAT)
    {
      item = empathy_individual_chat_menu_item_new (individual);
      if (item != NULL)
        {
          gtk_menu_shell_append (shell, item);
          gtk_widget_show (item);
        }
    }

  if (features & EMPATHY_INDIVIDUAL_FEATURE_CALL)
    {
      /* Audio Call */
      item = empathy_individual_audio_call_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);

      /* Video Call */
      item = empathy_individual_video_call_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Log */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_LOG)
    {
      item = empathy_individual_log_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Invite */
  item = empathy_individual_invite_menu_item_new (individual);
  gtk_menu_shell_append (shell, item);
  gtk_widget_show (item);

  /* File transfer */
  item = empathy_individual_file_transfer_menu_item_new (individual);
  gtk_menu_shell_append (shell, item);
  gtk_widget_show (item);

  /* Share my desktop */
  /* FIXME we should add the "Share my desktop" menu item if Vino is
  a registered handler in MC5 */
  item = empathy_individual_share_my_desktop_menu_item_new (individual);
  gtk_menu_shell_append (shell, item);
  gtk_widget_show (item);

  /* Separator */
  if (features & (EMPATHY_INDIVIDUAL_FEATURE_EDIT |
      EMPATHY_INDIVIDUAL_FEATURE_INFO |
      EMPATHY_INDIVIDUAL_FEATURE_FAVOURITE |
      EMPATHY_INDIVIDUAL_FEATURE_LINK))
    {
      item = gtk_separator_menu_item_new ();
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Edit */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_EDIT)
    {
      item = empathy_individual_edit_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Link */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_LINK)
    {
      item = empathy_individual_link_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Info */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_INFO)
    {
      item = empathy_individual_info_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Favorite checkbox */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_FAVOURITE)
    {
      item = empathy_individual_favourite_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  return menu;
}

static void
empathy_individual_add_menu_item_activated (GtkMenuItem *item,
  FolksIndividual *individual)
{
  GtkWidget *toplevel;

  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (item));
  if (!gtk_widget_is_toplevel (toplevel) || !GTK_IS_WINDOW (toplevel))
    toplevel = NULL;

  empathy_new_individual_dialog_show_with_individual (GTK_WINDOW (toplevel),
      individual);
}

GtkWidget *
empathy_individual_add_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  GtkWidget *image;
  EmpathyIndividualManager *manager = NULL;
  EmpathyContact *contact = NULL;
  TpConnection *connection;
  GList *l, *members;
  gboolean found = FALSE;
  EmpathyIndividualManagerFlags flags;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  if (!empathy_individual_manager_initialized ())
    {
      item = NULL;
      goto out;
    }

  manager = empathy_individual_manager_dup_singleton ();
  contact = empathy_contact_dup_from_folks_individual (individual);
  connection = empathy_contact_get_connection (contact);

  flags = empathy_individual_manager_get_flags_for_connection (manager,
      connection);

  if (!(flags & EMPATHY_INDIVIDUAL_MANAGER_CAN_ADD))
    {
      item = NULL;
      goto out;
    }

  members = empathy_individual_manager_get_members (
      EMPATHY_INDIVIDUAL_MANAGER (manager));

  for (l = members; l && !found; l = l->next)
    {
      if (!tp_strdiff (folks_individual_get_id (l->data),
              folks_individual_get_id (individual)))
        {
          found = TRUE;
        }
    }
  g_list_free (members);

  if (found)
    {
      item = NULL;
      goto out;
    }

  item = gtk_image_menu_item_new_with_mnemonic (_("_Add Contact…"));
  image = gtk_image_new_from_icon_name (GTK_STOCK_ADD, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);

  g_signal_connect (item, "activate",
      G_CALLBACK (empathy_individual_add_menu_item_activated),
      individual);

out:
  tp_clear_object (&contact);
  tp_clear_object (&manager);

  return item;
}

static void
empathy_individual_chat_menu_item_activated (GtkMenuItem *item,
  FolksIndividual *individual)
{
  EmpathyContact *contact;

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_if_fail (contact != NULL);

  empathy_dispatcher_chat_with_contact (contact, gtk_get_current_event_time ());

  g_object_unref (contact);
}

GtkWidget *
empathy_individual_chat_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  GtkWidget *image;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);
  g_return_val_if_fail (empathy_folks_individual_contains_contact (individual),
      NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("_Chat"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_MESSAGE,
      GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  g_signal_connect (item, "activate",
      G_CALLBACK (empathy_individual_chat_menu_item_activated), individual);

  return item;
}

static void
empathy_individual_audio_call_menu_item_activated (GtkMenuItem *item,
  FolksIndividual *individual)
{
  EmpathyContact *contact;

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_call_factory_new_call_with_streams (contact, TRUE, FALSE,
      gtk_get_current_event_time (), NULL);
  g_object_unref (contact);
}

GtkWidget *
empathy_individual_audio_call_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  GtkWidget *image;
  EmpathyContact *contact;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  item = gtk_image_menu_item_new_with_mnemonic (C_("menu item", "_Audio Call"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VOIP, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_set_sensitive (item, empathy_contact_can_voip_audio (contact));
  gtk_widget_show (image);

  g_signal_connect (item, "activate",
      G_CALLBACK (empathy_individual_audio_call_menu_item_activated),
      individual);

  g_object_unref (contact);

  return item;
}

static void
empathy_individual_video_call_menu_item_activated (GtkMenuItem *item,
  FolksIndividual *individual)
{
  EmpathyContact *contact;

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_call_factory_new_call_with_streams (contact, TRUE, TRUE,
      gtk_get_current_event_time (), NULL);
  g_object_unref (contact);
}

GtkWidget *
empathy_individual_video_call_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  GtkWidget *image;
  EmpathyContact *contact;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  item = gtk_image_menu_item_new_with_mnemonic (C_("menu item", "_Video Call"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VIDEO_CALL,
      GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_set_sensitive (item, empathy_contact_can_voip_video (contact));
  gtk_widget_show (image);

  g_signal_connect (item, "activate",
      G_CALLBACK (empathy_individual_video_call_menu_item_activated),
      individual);

  g_object_unref (contact);

  return item;
}

static void
individual_log_menu_item_activate_cb (FolksIndividual *individual)
{
  EmpathyContact *contact;

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_log_window_show (empathy_contact_get_account (contact),
      empathy_contact_get_id (contact), FALSE, NULL);

  g_object_unref (contact);
}

GtkWidget *
empathy_individual_log_menu_item_new (FolksIndividual *individual)
{
  TplLogManager *manager;
  gboolean have_log;
  GtkWidget *item;
  GtkWidget *image;
  EmpathyContact *contact;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  manager = tpl_log_manager_dup_singleton ();
  have_log = tpl_log_manager_exists (manager,
      empathy_contact_get_account (contact), empathy_contact_get_id (contact),
      FALSE);
  g_object_unref (manager);

  item = gtk_image_menu_item_new_with_mnemonic (_("_Previous Conversations"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_LOG, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_set_sensitive (item, have_log);
  gtk_widget_show (image);

  g_signal_connect_swapped (item, "activate",
      G_CALLBACK (individual_log_menu_item_activate_cb), individual);

  g_object_unref (contact);

  return item;
}

static void
individual_file_transfer_menu_item_activate_cb (FolksIndividual *individual)
{
  EmpathyContact *contact;

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_send_file_with_file_chooser (contact);

  g_object_unref (contact);
}

GtkWidget *
empathy_individual_file_transfer_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  GtkWidget *image;
  EmpathyContact *contact;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("Send File"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_DOCUMENT_SEND,
                GTK_ICON_SIZE_MENU);
  gtk_widget_set_sensitive (item, empathy_contact_can_send_files (contact));
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  g_signal_connect_swapped (item, "activate",
      G_CALLBACK (individual_file_transfer_menu_item_activate_cb), individual);

  g_object_unref (contact);

  return item;
}

static void
individual_share_my_desktop_menu_item_activate_cb (FolksIndividual *individual)
{
  EmpathyContact *contact;

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_share_my_desktop_share_with_contact (contact);

  g_object_unref (contact);
}

GtkWidget *
empathy_individual_share_my_desktop_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  GtkWidget *image;
  EmpathyContact *contact;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("Share My Desktop"));
  image = gtk_image_new_from_icon_name (GTK_STOCK_NETWORK, GTK_ICON_SIZE_MENU);
  gtk_widget_set_sensitive (item,
      empathy_contact_can_use_rfb_stream_tube (contact));
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  g_signal_connect_swapped (item, "activate",
      G_CALLBACK (individual_share_my_desktop_menu_item_activate_cb),
      individual);

  g_object_unref (contact);

  return item;
}

static void
favourite_menu_item_toggled_cb (GtkCheckMenuItem *item,
  FolksIndividual *individual)
{
  folks_favourite_set_is_favourite (FOLKS_FAVOURITE (individual),
      gtk_check_menu_item_get_active (item));
}

GtkWidget *
empathy_individual_favourite_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;

  item = gtk_check_menu_item_new_with_label (_("Favorite"));

  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
      folks_favourite_get_is_favourite (FOLKS_FAVOURITE (individual)));

  g_signal_connect (item, "toggled",
      G_CALLBACK (favourite_menu_item_toggled_cb), individual);

  return item;
}

static void
individual_info_menu_item_activate_cb (FolksIndividual *individual)
{
  EmpathyContact *contact;

  contact = empathy_contact_dup_from_folks_individual (individual);
  empathy_contact_information_dialog_show (contact, NULL);

  tp_clear_object (&contact);
}

GtkWidget *
empathy_individual_info_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  GtkWidget *image;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);
  g_return_val_if_fail (empathy_folks_individual_contains_contact (individual),
      NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("Infor_mation"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_CONTACT_INFORMATION,
                GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  g_signal_connect_swapped (item, "activate",
          G_CALLBACK (individual_info_menu_item_activate_cb),
          individual);

  return item;
}

static void
individual_edit_menu_item_activate_cb (FolksIndividual *individual)
{
  EmpathyContact *contact;

  contact = empathy_contact_dup_from_folks_individual (individual);
  empathy_contact_edit_dialog_show (contact, NULL);

  tp_clear_object (&contact);
}

GtkWidget *
empathy_individual_edit_menu_item_new (FolksIndividual *individual)
{
  EmpathyIndividualManager *manager;
  GtkWidget *item;
  GtkWidget *image;
  gboolean enable = FALSE;
  EmpathyContact *contact;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  if (empathy_individual_manager_initialized ())
    {
      TpConnection *connection;
      EmpathyIndividualManagerFlags flags;

      manager = empathy_individual_manager_dup_singleton ();
      connection = empathy_contact_get_connection (contact);
      flags = empathy_individual_manager_get_flags_for_connection (
          manager, connection);

      enable = (flags & EMPATHY_INDIVIDUAL_MANAGER_CAN_ALIAS ||
                flags & EMPATHY_INDIVIDUAL_MANAGER_CAN_GROUP);

      g_object_unref (manager);
    }

  item = gtk_image_menu_item_new_with_mnemonic (
      C_("Edit individual (contextual menu)", "_Edit"));
  image = gtk_image_new_from_icon_name (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  gtk_widget_set_sensitive (item, enable);

  g_signal_connect_swapped (item, "activate",
      G_CALLBACK (individual_edit_menu_item_activate_cb), individual);

  g_object_unref (contact);

  return item;
}

static void
individual_link_menu_item_activate_cb (FolksIndividual *individual)
{
  empathy_linking_dialog_show (individual, NULL);
}

GtkWidget *
empathy_individual_link_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  /*GtkWidget *image;*/

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  item = gtk_image_menu_item_new_with_mnemonic (
      C_("Link individual (contextual menu)", "_Link…"));
  /* TODO */
  /*image = gtk_image_new_from_icon_name (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);*/

  g_signal_connect_swapped (item, "activate",
      G_CALLBACK (individual_link_menu_item_activate_cb), individual);

  return item;
}

typedef struct
{
  FolksIndividual *individual;
  EmpathyChatroom *chatroom;
} RoomSubMenuData;

static RoomSubMenuData *
room_sub_menu_data_new (FolksIndividual *individual,
      EmpathyChatroom *chatroom)
{
  RoomSubMenuData *data;

  data = g_slice_new (RoomSubMenuData);
  data->individual = g_object_ref (individual);
  data->chatroom = g_object_ref (chatroom);
  return data;
}

static void
room_sub_menu_data_free (RoomSubMenuData *data)
{
  g_object_unref (data->individual);
  g_object_unref (data->chatroom);
  g_slice_free (RoomSubMenuData, data);
}

static void
room_sub_menu_activate_cb (GtkWidget *item,
         RoomSubMenuData *data)
{
  EmpathyTpChat *chat;
  EmpathyContact *contact;

  chat = empathy_chatroom_get_tp_chat (data->chatroom);
  if (chat == NULL)
    {
      /* channel was invalidated. Ignoring */
      return;
    }

  contact = empathy_contact_dup_from_folks_individual (data->individual);

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  /* send invitation */
  empathy_contact_list_add (EMPATHY_CONTACT_LIST (chat),
      contact, _("Inviting you to this room"));

  g_object_unref (contact);
}

static GtkWidget *
create_room_sub_menu (FolksIndividual *individual,
                      EmpathyChatroom *chatroom)
{
  GtkWidget *item;
  RoomSubMenuData *data;

  item = gtk_menu_item_new_with_label (empathy_chatroom_get_name (chatroom));
  data = room_sub_menu_data_new (individual, chatroom);
  g_signal_connect_data (item, "activate",
      G_CALLBACK (room_sub_menu_activate_cb), data,
      (GClosureNotify) room_sub_menu_data_free, 0);

  return item;
}

GtkWidget *
empathy_individual_invite_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  GtkWidget *image;
  GtkWidget *room_item;
  EmpathyChatroomManager *mgr;
  GList *rooms, *l;
  GtkWidget *submenu = NULL;
  EmpathyContact *contact;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("_Invite to Chat Room"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_GROUP_MESSAGE,
      GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);

  mgr = empathy_chatroom_manager_dup_singleton (NULL);
  rooms = empathy_chatroom_manager_get_chatrooms (mgr,
      empathy_contact_get_account (contact));

  for (l = rooms; l != NULL; l = g_list_next (l))
    {
      EmpathyChatroom *chatroom = l->data;

      if (empathy_chatroom_get_tp_chat (chatroom) != NULL)
        {
          if (G_UNLIKELY (submenu == NULL))
            submenu = gtk_menu_new ();

          room_item = create_room_sub_menu (individual, chatroom);
          gtk_menu_shell_append ((GtkMenuShell *) submenu, room_item);
          gtk_widget_show (room_item);
        }
    }

  if (submenu)
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  else
    gtk_widget_set_sensitive (item, FALSE);

  gtk_widget_show (image);

  g_object_unref (contact);
  g_object_unref (mgr);
  g_list_free (rooms);

  return item;
}
