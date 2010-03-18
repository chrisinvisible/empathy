/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002-2007 Imendio AB
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Geert-Jan Van den Bogaerde <geertjan@gnome.org>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gdk/gdkkeysyms.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>
#ifdef ENABLE_TPL
#include <telepathy-logger/log-manager.h>
#else

#include <libempathy/empathy-log-manager.h>
#endif /* ENABLE_TPL */
#include <libempathy/empathy-contact-list.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-dispatcher.h>

#include "empathy-chat.h"
#include "empathy-conf.h"
#include "empathy-spell.h"
#include "empathy-contact-list-store.h"
#include "empathy-contact-list-view.h"
#include "empathy-contact-menu.h"
#include "empathy-search-bar.h"
#include "empathy-theme-manager.h"
#include "empathy-smiley-manager.h"
#include "empathy-ui-utils.h"
#include "empathy-string-parser.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CHAT
#include <libempathy/empathy-debug.h>

#define CHAT_DIR_CREATE_MODE  (S_IRUSR | S_IWUSR | S_IXUSR)
#define CHAT_FILE_CREATE_MODE (S_IRUSR | S_IWUSR)
#define IS_ENTER(v) (v == GDK_Return || v == GDK_ISO_Enter || v == GDK_KP_Enter)
#define MAX_INPUT_HEIGHT 150
#define COMPOSING_STOP_TIMEOUT 5

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyChat)
typedef struct {
	EmpathyTpChat     *tp_chat;
	TpAccount         *account;
	gchar             *id;
	gchar             *name;
	gchar             *subject;
	EmpathyContact    *remote_contact;
	gboolean           show_contacts;

#ifdef ENABLE_TPL
	TplLogManager     *log_manager;
#else
	EmpathyLogManager *log_manager;
#endif /* ENABLE_TPL */
	TpAccountManager  *account_manager;
	GList             *input_history;
	GList             *input_history_current;
	GList             *compositors;
	GCompletion       *completion;
	guint              composing_stop_timeout_id;
	guint              block_events_timeout_id;
	TpHandleType       handle_type;
	gint               contacts_width;
	gboolean           has_input_vscroll;

	GtkWidget         *widget;
	GtkWidget         *hpaned;
	GtkWidget         *vbox_left;
	GtkWidget         *scrolled_window_chat;
	GtkWidget         *scrolled_window_input;
	GtkWidget         *scrolled_window_contacts;
	GtkWidget         *hbox_topic;
	GtkWidget         *label_topic;
	GtkWidget         *contact_list_view;
	GtkWidget         *info_bar_vbox;
	GtkWidget         *search_bar;

	guint              unread_messages;
	/* TRUE if the pending messages can be displayed. This is to avoid to show
	 * pending messages *before* messages from logs. (#603980) */
	gboolean           can_show_pending;

	/* FIXME: retrieving_backlogs flag is a workaround for Bug#610994 and should
	 * be differently handled since it introduces another race condition, which
	 * is really hard to occur, but still possible.
	 *
	 * With the current workaround (which has the race above), we need to be
	 * sure to ACK any pending messages only when the retrieval of backlogs is
	 * finished, that's why using retrieving_backlogs flag.
	 * empathy_chat_messages_read () will check this variable and not ACK
	 * anything when TRUE. It will be set TRUE at chat_constructed () and set
	 * back to FALSE when the backlog has been retrieved and the pending
	 * messages actually showed to the user.
	 *
	 * Race condition introduced with this workaround:
	 * Scenario: a message is pending, the user is notified and selects the tab.
	 * the tab with a pending message is focused before the messages are properly
	 * shown (since the preparation of the window is slower AND async WRT the
	 * tab showing), which means the user won't see any new messages (rare but
	 * possible), if he/she will change tab focus before the messages are
	 * properly shown, the tab will be set as 'seen' and the user won't be
	 * notified again about the already notified pending messages when the
	 * messages in tab will be properly shown */
	gboolean           retrieving_backlogs;
} EmpathyChatPriv;

typedef struct {
	gchar *text; /* Original message that was specified
	              * upon entry creation. */
	gchar *modified_text; /* Message that was modified by user.
	                       * When no modifications were made, it is NULL */
} InputHistoryEntry;

enum {
	COMPOSING,
	NEW_MESSAGE,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_TP_CHAT,
	PROP_ACCOUNT,
	PROP_ID,
	PROP_NAME,
	PROP_SUBJECT,
	PROP_REMOTE_CONTACT,
	PROP_SHOW_CONTACTS,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (EmpathyChat, empathy_chat, GTK_TYPE_BIN);

static void
chat_get_property (GObject    *object,
		   guint       param_id,
		   GValue     *value,
		   GParamSpec *pspec)
{
	EmpathyChat *chat = EMPATHY_CHAT (object);
	EmpathyChatPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_TP_CHAT:
		g_value_set_object (value, priv->tp_chat);
		break;
	case PROP_ACCOUNT:
		g_value_set_object (value, priv->account);
		break;
	case PROP_NAME:
		g_value_set_string (value, empathy_chat_get_name (chat));
		break;
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case PROP_SUBJECT:
		g_value_set_string (value, priv->subject);
		break;
	case PROP_REMOTE_CONTACT:
		g_value_set_object (value, priv->remote_contact);
		break;
	case PROP_SHOW_CONTACTS:
		g_value_set_boolean (value, priv->show_contacts);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
chat_set_property (GObject      *object,
		   guint         param_id,
		   const GValue *value,
		   GParamSpec   *pspec)
{
	EmpathyChat *chat = EMPATHY_CHAT (object);

	switch (param_id) {
	case PROP_TP_CHAT:
		empathy_chat_set_tp_chat (chat, EMPATHY_TP_CHAT (g_value_get_object (value)));
		break;
	case PROP_SHOW_CONTACTS:
		empathy_chat_set_show_contacts (chat, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
chat_connect_channel_reconnected (EmpathyDispatchOperation *dispatch,
				  const GError             *error,
				  gpointer                  user_data)
{
	EmpathyChat *chat = EMPATHY_CHAT (user_data);
	EmpathyTpChat *tpchat;

	if (error != NULL) {
		empathy_chat_view_append_event (chat->view,
			_("Failed to reconnect this chat"));
		return;
	}

	tpchat = EMPATHY_TP_CHAT (
		empathy_dispatch_operation_get_channel_wrapper (dispatch));

	if (empathy_dispatch_operation_claim (dispatch)) {
		empathy_chat_set_tp_chat (chat, tpchat);
	}
}

static void
reconnected_connection_ready_cb (TpConnection *connection,
			const GError *error,
			gpointer user_data)
{
	EmpathyChat *chat = user_data;
	EmpathyChatPriv *priv = GET_PRIV (chat);

	if (error != NULL) {
		DEBUG ("connection is not ready: %s", error->message);
		goto out;
	}

	DEBUG ("Account reconnected, request a new Text channel");

	switch (priv->handle_type) {
		case TP_HANDLE_TYPE_CONTACT:
			empathy_dispatcher_chat_with_contact_id (
				connection, priv->id,
				chat_connect_channel_reconnected,
				chat);
			break;
		case TP_HANDLE_TYPE_ROOM:
			empathy_dispatcher_join_muc (connection,
				priv->id,
				chat_connect_channel_reconnected,
				chat);
			break;
		default:
			g_assert_not_reached ();
			break;
	}

out:
	g_object_unref (chat);
}

static void
chat_new_connection_cb (TpAccount   *account,
			guint        old_status,
			guint        new_status,
			guint        reason,
			gchar       *dbus_error_name,
			GHashTable  *details,
			EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	TpConnection *connection;

	if (new_status != TP_CONNECTION_STATUS_CONNECTED)
		return;

	connection = tp_account_get_connection (account);

	if (priv->tp_chat != NULL || account != priv->account ||
	    priv->handle_type == TP_HANDLE_TYPE_NONE ||
	    EMP_STR_EMPTY (priv->id))
		return;

	g_object_ref (chat);
	tp_connection_call_when_ready (connection, reconnected_connection_ready_cb,
				   chat);
}

static void
chat_composing_remove_timeout (EmpathyChat *chat)
{
	EmpathyChatPriv *priv;

	priv = GET_PRIV (chat);

	if (priv->composing_stop_timeout_id) {
		g_source_remove (priv->composing_stop_timeout_id);
		priv->composing_stop_timeout_id = 0;
	}
}

static gboolean
chat_composing_stop_timeout_cb (EmpathyChat *chat)
{
	EmpathyChatPriv *priv;

	priv = GET_PRIV (chat);

	priv->composing_stop_timeout_id = 0;
	empathy_tp_chat_set_state (priv->tp_chat,
				   TP_CHANNEL_CHAT_STATE_PAUSED);

	return FALSE;
}

static void
chat_composing_start (EmpathyChat *chat)
{
	EmpathyChatPriv *priv;

	priv = GET_PRIV (chat);

	if (priv->composing_stop_timeout_id) {
		/* Just restart the timeout */
		chat_composing_remove_timeout (chat);
	} else {
		empathy_tp_chat_set_state (priv->tp_chat,
					   TP_CHANNEL_CHAT_STATE_COMPOSING);
	}

	priv->composing_stop_timeout_id = g_timeout_add_seconds (
		COMPOSING_STOP_TIMEOUT,
		(GSourceFunc) chat_composing_stop_timeout_cb,
		chat);
}

static void
chat_composing_stop (EmpathyChat *chat)
{
	EmpathyChatPriv *priv;

	priv = GET_PRIV (chat);

	chat_composing_remove_timeout (chat);
	empathy_tp_chat_set_state (priv->tp_chat,
				   TP_CHANNEL_CHAT_STATE_ACTIVE);
}

static gint
chat_input_history_entry_cmp (InputHistoryEntry *entry,
                              const gchar *text)
{
	if (!tp_strdiff (entry->text, text)) {
		if (entry->modified_text != NULL) {
			/* Modified entry and single string cannot be equal. */
			return 1;
		}
		return 0;
	}
	return 1;
}

static InputHistoryEntry *
chat_input_history_entry_new_with_text (const gchar *text)
{
	InputHistoryEntry *entry;
	entry = g_slice_new0 (InputHistoryEntry);
	entry->text = g_strdup (text);

	return entry;
}

static void
chat_input_history_entry_free (InputHistoryEntry *entry)
{
	g_free (entry->text);
	g_free (entry->modified_text);
	g_slice_free (InputHistoryEntry, entry);
}

static void
chat_input_history_entry_revert (InputHistoryEntry *entry)
{
	g_free (entry->modified_text);
	entry->modified_text = NULL;
}

static void
chat_input_history_entry_update_text (InputHistoryEntry *entry,
                                      const gchar *text)
{
	gchar *old;

	if (!tp_strdiff (text, entry->text)) {
		g_free (entry->modified_text);
		entry->modified_text = NULL;
		return;
	}

	old = entry->modified_text;
	entry->modified_text = g_strdup (text);
	g_free (old);
}

static const gchar *
chat_input_history_entry_get_text (InputHistoryEntry *entry)
{
	if (entry == NULL) {
		return NULL;
	}

	if (entry->modified_text != NULL) {
		return entry->modified_text;
	}
	return entry->text;
}

static GList *
chat_input_history_remove_item (GList *list,
                                GList *item)
{
	list = g_list_remove_link (list, item);
	chat_input_history_entry_free (item->data);
	g_list_free_1 (item);
	return list;
}

static void
chat_input_history_revert (EmpathyChat *chat)
{
	EmpathyChatPriv   *priv;
	GList             *list;
	GList             *item1;
	GList             *item2;
	InputHistoryEntry *entry;

	priv = GET_PRIV (chat);
	list = priv->input_history;

	if (list == NULL) {
		DEBUG ("No input history");
		return;
	}

	/* Delete temporary entry */
	if (priv->input_history_current != NULL) {
		item1 = list;
		list = chat_input_history_remove_item (list, item1);
		if (priv->input_history_current == item1) {
			/* Removed temporary entry was current entry */
			priv->input_history = list;
			priv->input_history_current = NULL;
			return;
		}
	}
	else {
		/* There is no entry to revert */
		return;
	}

	/* Restore the current history entry to original value */
	item1 = priv->input_history_current;
	entry = item1->data;
	chat_input_history_entry_revert (entry);

	/* Remove restored entry if there is other occurance before this entry */
	item2 = g_list_find_custom (list, chat_input_history_entry_get_text (entry),
	                            (GCompareFunc) chat_input_history_entry_cmp);
	if (item2 != item1) {
		list = chat_input_history_remove_item (list, item1);
	}
	else {
		/* Remove other occurance of the restored entry */
		item2 = g_list_find_custom (item1->next,
		                            chat_input_history_entry_get_text (entry),
		                            (GCompareFunc) chat_input_history_entry_cmp);
		if (item2 != NULL) {
			list = chat_input_history_remove_item (list, item2);
		}
	}

	priv->input_history_current = NULL;
	priv->input_history = list;
}

static void
chat_input_history_add (EmpathyChat  *chat,
                        const gchar *str,
                        gboolean temporary)
{
	EmpathyChatPriv   *priv;
	GList             *list;
	GList             *item;
	InputHistoryEntry *entry;

	priv = GET_PRIV (chat);

	list = priv->input_history;

	/* Remove any other occurances of this entry, if not temporary */
	if (!temporary) {
		while ((item = g_list_find_custom (list, str,
		    (GCompareFunc) chat_input_history_entry_cmp)) != NULL) {
			list = chat_input_history_remove_item (list, item);
		}

		/* Trim the list to the last 10 items */
		while (g_list_length (list) > 10) {
			item = g_list_last (list);
			if (item != NULL) {
				list = chat_input_history_remove_item (list, item);
			}
		}
	}



	/* Add new entry */
	entry = chat_input_history_entry_new_with_text (str);
	list = g_list_prepend (list, entry);

	/* Set the list and the current item pointer */
	priv->input_history = list;
	if (temporary) {
		priv->input_history_current = list;
	}
	else {
		priv->input_history_current = NULL;
	}
}

static const gchar *
chat_input_history_get_next (EmpathyChat *chat)
{
	EmpathyChatPriv *priv;
	GList           *item;
	const gchar     *msg;

	priv = GET_PRIV (chat);

	if (priv->input_history == NULL) {
		DEBUG ("No input history, next entry is NULL");
		return NULL;
	}
	g_assert (priv->input_history_current != NULL);

	if ((item = g_list_next (priv->input_history_current)) == NULL)
	{
		item = priv->input_history_current;
	}

	msg = chat_input_history_entry_get_text (item->data);

	DEBUG ("Returning next entry: '%s'", msg);

	priv->input_history_current = item;

	return msg;
}

static const gchar *
chat_input_history_get_prev (EmpathyChat *chat)
{
	EmpathyChatPriv *priv;
	GList           *item;
	const gchar     *msg;

	g_return_val_if_fail (EMPATHY_IS_CHAT (chat), NULL);

	priv = GET_PRIV (chat);

	if (priv->input_history == NULL) {
		DEBUG ("No input history, previous entry is NULL");
		return NULL;
	}

	if (priv->input_history_current == NULL)
	{
		return NULL;
	}
	else if ((item = g_list_previous (priv->input_history_current)) == NULL)
	{
		item = priv->input_history_current;
	}

	msg = chat_input_history_entry_get_text (item->data);

	DEBUG ("Returning previous entry: '%s'", msg);

	priv->input_history_current = item;

	return msg;
}

static void
chat_input_history_update (EmpathyChat *chat,
                           GtkTextBuffer *buffer)
{
	EmpathyChatPriv      *priv;
	GtkTextIter           start, end;
	gchar                *text;
	InputHistoryEntry    *entry;

	priv = GET_PRIV (chat);

	gtk_text_buffer_get_bounds (buffer, &start, &end);
	text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

	if (priv->input_history_current == NULL) {
		/* Add the current text temporarily to the history */
		chat_input_history_add (chat, text, TRUE);
		g_free (text);
		return;
	}

	/* Save the changes in the history */
	entry = priv->input_history_current->data;
	if (tp_strdiff (chat_input_history_entry_get_text (entry), text)) {
		chat_input_history_entry_update_text (entry, text);
	}

	g_free (text);
}

static void
chat_command_join_cb (EmpathyDispatchOperation *dispatch,
		      const GError             *error,
		      gpointer                  user_data)
{
	EmpathyChat *chat = user_data;

	if (error != NULL) {
		DEBUG ("Error: %s", error->message);
		empathy_chat_view_append_event (chat->view,
			_("Failed to join chat room"));
	}
}

typedef struct {
	EmpathyChat *chat;
	gchar *message;
} ChatCommandMsgData;

static void
chat_command_msg_cb (EmpathyDispatchOperation *dispatch,
			      const GError             *error,
			      gpointer                  user_data)
{
	ChatCommandMsgData *data = user_data;

	if (error != NULL) {
		empathy_chat_view_append_event (data->chat->view,
			_("Failed to open private chat"));
		goto OUT;
	}

	if (!EMP_STR_EMPTY (data->message)) {
		EmpathyTpChat *tpchat;
		EmpathyMessage *message;

		tpchat = EMPATHY_TP_CHAT (
			empathy_dispatch_operation_get_channel_wrapper (dispatch));

		message = empathy_message_new (data->message);
		empathy_tp_chat_send (tpchat, message);
		g_object_unref (message);
	}

OUT:
	g_free (data->message);
	g_slice_free (ChatCommandMsgData, data);
}

static void
chat_command_clear (EmpathyChat *chat,
		    GStrv        strv)
{
	empathy_chat_view_clear (chat->view);
}

static void
chat_command_topic (EmpathyChat *chat,
		    GStrv        strv)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	EmpathyTpChatProperty *property;
	GValue value = {0, };

	property = empathy_tp_chat_get_property (priv->tp_chat, "subject");
	if (property == NULL) {
		empathy_chat_view_append_event (chat->view,
			_("Topic not supported on this conversation"));
		return;
	}

	if (!(property->flags & TP_PROPERTY_FLAG_WRITE)) {
		empathy_chat_view_append_event (chat->view,
			_("You are not allowed to change the topic"));
		return;
	}

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, strv[1]);
	empathy_tp_chat_set_property (priv->tp_chat, "subject", &value);
	g_value_unset (&value);
}

static void
chat_command_join (EmpathyChat *chat,
		   GStrv        strv)
{
	guint i = 0;
	EmpathyChatPriv *priv = GET_PRIV (chat);

	GStrv rooms = g_strsplit_set (strv[1], ", ", -1);

	while (rooms[i] != NULL) {
		/* ignore empty strings */
		if (!EMP_STR_EMPTY (rooms[i])) {
			TpConnection *connection;

			connection = empathy_tp_chat_get_connection (priv->tp_chat);
			empathy_dispatcher_join_muc (connection, rooms[i],
						     chat_command_join_cb,
						     chat);
		}
		i++;
	}
	g_strfreev (rooms);
}

static void
chat_command_msg_internal (EmpathyChat *chat,
			   const gchar *contact_id,
			   const gchar *message)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	TpConnection *connection;
	ChatCommandMsgData *data;

	/* FIXME: We should probably search in members alias. But this
	 * is enough for IRC */
	data = g_slice_new (ChatCommandMsgData);
	data->chat = chat;
	data->message = g_strdup (message);
	connection = empathy_tp_chat_get_connection (priv->tp_chat);
	empathy_dispatcher_chat_with_contact_id (connection, contact_id,
						 chat_command_msg_cb,
						 data);
}

static void
chat_command_query (EmpathyChat *chat,
		    GStrv        strv)
{
	/* If <message> part is not defined,
	 * strv[2] will be the terminal NULL */
	chat_command_msg_internal (chat, strv[1], strv[2]);
}

static void
chat_command_msg (EmpathyChat *chat,
		  GStrv        strv)
{
	chat_command_msg_internal (chat, strv[1], strv[2]);
}

static void
chat_command_nick (EmpathyChat *chat,
		   GStrv        strv)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	TpConnection *connection;
	GHashTable *new_alias;
	TpHandle handle;

	connection = tp_account_get_connection (priv->account);
	handle = tp_connection_get_self_handle (connection);
	new_alias = g_hash_table_new (g_direct_hash, g_direct_equal);
	g_hash_table_insert (new_alias, GUINT_TO_POINTER (handle), strv[1]);

	tp_cli_connection_interface_aliasing_call_set_aliases (connection, -1,
		new_alias, NULL, NULL, NULL, NULL);

	g_hash_table_destroy (new_alias);
}

static void
chat_command_me (EmpathyChat *chat,
		  GStrv        strv)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	EmpathyMessage *message;

	message = empathy_message_new (strv[1]);
	empathy_message_set_tptype (message, TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION);
	empathy_tp_chat_send (priv->tp_chat, message);
	g_object_unref (message);
}

static void
chat_command_say (EmpathyChat *chat,
		  GStrv        strv)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	EmpathyMessage *message;

	message = empathy_message_new (strv[1]);
	empathy_tp_chat_send (priv->tp_chat, message);
	g_object_unref (message);
}

static void chat_command_help (EmpathyChat *chat, GStrv strv);

typedef void (*ChatCommandFunc) (EmpathyChat *chat, GStrv strv);

typedef struct {
	const gchar *prefix;
	guint min_parts;
	guint max_parts;
	ChatCommandFunc func;
	const gchar *help;
} ChatCommandItem;

static ChatCommandItem commands[] = {
	{"clear", 1, 1, chat_command_clear,
	 N_("/clear: clear all messages from the current conversation")},

	{"topic", 2, 2, chat_command_topic,
	 N_("/topic <topic>: set the topic of the current conversation")},

	{"join", 2, 2, chat_command_join,
	 N_("/join <chat room ID>: join a new chat room")},

	{"j", 2, 2, chat_command_join,
	 N_("/j <chat room ID>: join a new chat room")},

	{"query", 2, 3, chat_command_query,
	 N_("/query <contact ID> [<message>]: open a private chat")},

	{"msg", 3, 3, chat_command_msg,
	 N_("/msg <contact ID> <message>: open a private chat")},

	{"nick", 2, 2, chat_command_nick,
	 N_("/nick <nickname>: change your nickname on the current server")},

	{"me", 2, 2, chat_command_me,
	 N_("/me <message>: send an ACTION message to the current conversation")},

	{"say", 2, 2, chat_command_say,
	 N_("/say <message>: send <message> to the current conversation. "
	    "This is used to send a message starting with a '/'. For example: "
	    "\"/say /join is used to join a new chat room\"")},

	{"help", 1, 2, chat_command_help,
	 N_("/help [<command>]: show all supported commands. "
	    "If <command> is defined, show its usage.")},
};

static void
chat_command_show_help (EmpathyChat     *chat,
			ChatCommandItem *item)
{
	gchar *str;

	str = g_strdup_printf (_("Usage: %s"), _(item->help));
	empathy_chat_view_append_event (chat->view, str);
	g_free (str);
}

static void
chat_command_help (EmpathyChat *chat,
		   GStrv        strv)
{
	guint i;

	/* If <command> part is not defined,
	 * strv[1] will be the terminal NULL */
	if (strv[1] == NULL) {
		for (i = 0; i < G_N_ELEMENTS (commands); i++) {
			empathy_chat_view_append_event (chat->view,
				_(commands[i].help));
		}
		return;
	}

	for (i = 0; i < G_N_ELEMENTS (commands); i++) {
		if (g_ascii_strcasecmp (strv[1], commands[i].prefix) == 0) {
			chat_command_show_help (chat, &commands[i]);
			return;
		}
	}

	empathy_chat_view_append_event (chat->view,
		_("Unknown command"));
}

static GStrv
chat_command_parse (const gchar *text, guint max_parts)
{
	GPtrArray *array;
	gchar *item;

	DEBUG ("Parse command, parts=%d text=\"%s\":", max_parts, text);

	array = g_ptr_array_sized_new (max_parts + 1);
	while (max_parts > 1) {
		const gchar *end;

		/* Skip white spaces */
		while (g_ascii_isspace (*text)) {
			text++;
		}

		/* Search the end of this part, until first space. */
		for (end = text; *end != '\0' && !g_ascii_isspace (*end); end++)
			/* Do nothing */;
		if (*end == '\0') {
			break;
		}

		item = g_strndup (text, end - text);
		g_ptr_array_add (array, item);
		DEBUG ("\tITEM: \"%s\"", item);

		text = end;
		max_parts--;
	}

	/* Append last part if not empty */
	item = g_strstrip (g_strdup (text));
	if (!EMP_STR_EMPTY (item)) {
		g_ptr_array_add (array, item);
		DEBUG ("\tITEM: \"%s\"", item);
	} else {
		g_free (item);
	}

	/* Make the array NULL-terminated */
	g_ptr_array_add (array, NULL);

	return (GStrv) g_ptr_array_free (array, FALSE);
}

static gboolean
has_prefix_case (const gchar *s,
		  const gchar *prefix)
{
	return g_ascii_strncasecmp (s, prefix, strlen (prefix)) == 0;
}

static void
chat_send (EmpathyChat  *chat,
	   const gchar *msg)
{
	EmpathyChatPriv *priv;
	EmpathyMessage  *message;
	guint            i;

	if (EMP_STR_EMPTY (msg)) {
		return;
	}

	priv = GET_PRIV (chat);

	chat_input_history_add (chat, msg, FALSE);

	if (msg[0] == '/') {
		gboolean second_slash = FALSE;
		const gchar *iter = msg + 1;

		for (i = 0; i < G_N_ELEMENTS (commands); i++) {
			GStrv strv;
			guint strv_len;
			gchar c;

			if (!has_prefix_case (msg + 1, commands[i].prefix)) {
				continue;
			}
			c = *(msg + 1 + strlen (commands[i].prefix));
			if (c != '\0' && !g_ascii_isspace (c)) {
				continue;
			}

			/* We can't use g_strsplit here because it does
			 * not deal correctly if we have more than one space
			 * between args */
			strv = chat_command_parse (msg + 1, commands[i].max_parts);

			strv_len = g_strv_length (strv);
			if (strv_len < commands[i].min_parts ||
			    strv_len > commands[i].max_parts) {
				chat_command_show_help (chat, &commands[i]);
				g_strfreev (strv);
				return;
			}

			commands[i].func (chat, strv);
			g_strfreev (strv);
			return;
		}

		/* Also allow messages with two slashes before the
		 * first space, so it is possible to send a /unix/path.
		 * This heuristic is kind of crap. */
		while (*iter != '\0' && !g_ascii_isspace (*iter)) {
			if (*iter == '/') {
				second_slash = TRUE;
				break;
			}
			iter++;
		}

		if (!second_slash) {
			empathy_chat_view_append_event (chat->view,
				_("Unknown command; see /help for the available"
				  " commands"));
			return;
		}
	}

	message = empathy_message_new (msg);
	empathy_tp_chat_send (priv->tp_chat, message);
	g_object_unref (message);
}

static void
chat_input_text_view_send (EmpathyChat *chat)
{
	EmpathyChatPriv *priv;
	GtkTextBuffer  *buffer;
	GtkTextIter     start, end;
	gchar	       *msg;

	priv = GET_PRIV (chat);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));

	gtk_text_buffer_get_bounds (buffer, &start, &end);
	msg = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

	/* clear the input field */
	gtk_text_buffer_set_text (buffer, "", -1);
	/* delete input history modifications */
	chat_input_history_revert (chat);

	chat_send (chat, msg);
	g_free (msg);
}

static void
chat_state_changed_cb (EmpathyTpChat      *tp_chat,
		       EmpathyContact     *contact,
		       TpChannelChatState  state,
		       EmpathyChat        *chat)
{
	EmpathyChatPriv *priv;
	GList          *l;
	gboolean        was_composing;

	priv = GET_PRIV (chat);

	if (empathy_contact_is_user (contact)) {
		/* We don't care about our own chat state */
		return;
	}

	was_composing = (priv->compositors != NULL);

	/* Find the contact in the list. After that l is the list elem or NULL */
	for (l = priv->compositors; l; l = l->next) {
		if (contact == l->data) {
			break;
		}
	}

	switch (state) {
	case TP_CHANNEL_CHAT_STATE_GONE:
	case TP_CHANNEL_CHAT_STATE_INACTIVE:
	case TP_CHANNEL_CHAT_STATE_PAUSED:
	case TP_CHANNEL_CHAT_STATE_ACTIVE:
		/* Contact is not composing */
		if (l) {
			priv->compositors = g_list_remove_link (priv->compositors, l);
			g_object_unref (l->data);
			g_list_free1 (l);
		}
		break;
	case TP_CHANNEL_CHAT_STATE_COMPOSING:
		/* Contact is composing */
		if (!l) {
			priv->compositors = g_list_prepend (priv->compositors,
							    g_object_ref (contact));
		}
		break;
	default:
		g_assert_not_reached ();
	}

	DEBUG ("Was composing: %s now composing: %s",
		was_composing ? "yes" : "no",
		priv->compositors ? "yes" : "no");

	if ((was_composing && !priv->compositors) ||
	    (!was_composing && priv->compositors)) {
		/* Composing state changed */
		g_signal_emit (chat, signals[COMPOSING], 0,
			       priv->compositors != NULL);
	}
}

static void
chat_message_received (EmpathyChat *chat, EmpathyMessage *message)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	EmpathyContact  *sender;

	sender = empathy_message_get_sender (message);

	DEBUG ("Appending new message from %s (%d)",
		empathy_contact_get_name (sender),
		empathy_contact_get_handle (sender));

	empathy_chat_view_append_message (chat->view, message);

	/* We received a message so the contact is no longer composing */
	chat_state_changed_cb (priv->tp_chat, sender,
			       TP_CHANNEL_CHAT_STATE_ACTIVE,
			       chat);

	priv->unread_messages++;
	g_signal_emit (chat, signals[NEW_MESSAGE], 0, message);
}

static void
chat_message_received_cb (EmpathyTpChat  *tp_chat,
			  EmpathyMessage *message,
			  EmpathyChat    *chat)
{
	chat_message_received (chat, message);
}

static void
chat_send_error_cb (EmpathyTpChat          *tp_chat,
		    const gchar            *message_body,
		    TpChannelTextSendError  error_code,
		    EmpathyChat            *chat)
{
	const gchar *error;
	gchar       *str;

	switch (error_code) {
	case TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE:
		error = _("offline");
		break;
	case TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT:
		error = _("invalid contact");
		break;
	case TP_CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED:
		error = _("permission denied");
		break;
	case TP_CHANNEL_TEXT_SEND_ERROR_TOO_LONG:
		error = _("too long message");
		break;
	case TP_CHANNEL_TEXT_SEND_ERROR_NOT_IMPLEMENTED:
		error = _("not implemented");
		break;
	default:
		error = _("unknown");
		break;
	}

	str = g_strdup_printf (_("Error sending message '%s': %s"),
			       message_body,
			       error);
	empathy_chat_view_append_event (chat->view, str);
	g_free (str);
}

static void
chat_property_changed_cb (EmpathyTpChat *tp_chat,
			  const gchar   *name,
			  GValue        *value,
			  EmpathyChat   *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);

	if (!tp_strdiff (name, "subject")) {
		g_free (priv->subject);
		priv->subject = g_value_dup_string (value);
		g_object_notify (G_OBJECT (chat), "subject");

		if (EMP_STR_EMPTY (priv->subject)) {
			gtk_widget_hide (priv->hbox_topic);
		} else {
			gchar *markup_text;

			markup_text = empathy_add_link_markup (priv->subject);
			gtk_label_set_markup (GTK_LABEL (priv->label_topic), markup_text);
			g_free (markup_text);

			gtk_widget_show (priv->hbox_topic);
		}
		if (priv->block_events_timeout_id == 0) {
			gchar *str;

			if (!EMP_STR_EMPTY (priv->subject)) {
				str = g_strdup_printf (_("Topic set to: %s"), priv->subject);
			} else {
				str = g_strdup (_("No topic defined"));
			}
			empathy_chat_view_append_event (EMPATHY_CHAT (chat)->view, str);
			g_free (str);
		}
	}
	else if (!tp_strdiff (name, "name")) {
		g_free (priv->name);
		priv->name = g_value_dup_string (value);
		g_object_notify (G_OBJECT (chat), "name");
	}
}

static void
chat_input_text_buffer_changed_cb (GtkTextBuffer *buffer,
                                   EmpathyChat    *chat)
{
	GtkTextIter     start, end;
	gchar          *str;
	gboolean        spell_checker = FALSE;

	if (gtk_text_buffer_get_char_count (buffer) == 0) {
		chat_composing_stop (chat);
	} else {
		chat_composing_start (chat);
	}

	empathy_conf_get_bool (empathy_conf_get (),
                           EMPATHY_PREFS_CHAT_SPELL_CHECKER_ENABLED,
                           &spell_checker);

	gtk_text_buffer_get_start_iter (buffer, &start);

	if (!spell_checker) {
		gtk_text_buffer_get_end_iter (buffer, &end);
		gtk_text_buffer_remove_tag_by_name (buffer, "misspelled", &start, &end);
		return;
	}

	if (!empathy_spell_supported ()) {
		return;
	}

	/* NOTE: this is really inefficient, we shouldn't have to
	   reiterate the whole buffer each time and check each work
	   every time. */
	while (TRUE) {
		gboolean correct = FALSE;

		/* if at start */
		if (gtk_text_iter_is_start (&start)) {
			end = start;

			if (!gtk_text_iter_forward_word_end (&end)) {
				/* no whole word yet */
				break;
			}
		} else {
			if (!gtk_text_iter_forward_word_end (&end)) {
				/* must be the end of the buffer */
				break;
			}

			start = end;
			gtk_text_iter_backward_word_start (&start);
		}

		str = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

		/* spell check string if not a command */
		if (str[0] != '/') {
			correct = empathy_spell_check (str);
		} else {
			correct = TRUE;
		}

		if (!correct) {
			gtk_text_buffer_apply_tag_by_name (buffer, "misspelled", &start, &end);
		} else {
			gtk_text_buffer_remove_tag_by_name (buffer, "misspelled", &start, &end);
		}

		g_free (str);

		/* set start iter to the end iters position */
		start = end;
	}
}

static gboolean
empathy_isspace_cb (gunichar c,
		 gpointer data)
{
	return g_unichar_isspace (c);
}

static gboolean
chat_input_key_press_event_cb (GtkWidget   *widget,
			       GdkEventKey *event,
			       EmpathyChat *chat)
{
	EmpathyChatPriv *priv;
	GtkAdjustment  *adj;
	gdouble         val;
	GtkWidget      *text_view_sw;

	priv = GET_PRIV (chat);

	/* Catch ctrl+up/down so we can traverse messages we sent */
	if ((event->state & GDK_CONTROL_MASK) &&
	    (event->keyval == GDK_Up ||
	     event->keyval == GDK_Down)) {
		GtkTextBuffer *buffer;
		const gchar   *str;

		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));
		chat_input_history_update (chat, buffer);

		if (event->keyval == GDK_Up) {
			str = chat_input_history_get_next (chat);
		} else {
			str = chat_input_history_get_prev (chat);
		}

		g_signal_handlers_block_by_func (buffer,
						 chat_input_text_buffer_changed_cb,
						 chat);
		gtk_text_buffer_set_text (buffer, str ? str : "", -1);
		g_signal_handlers_unblock_by_func (buffer,
						   chat_input_text_buffer_changed_cb,
						   chat);

		return TRUE;
	}

	/* Catch enter but not ctrl/shift-enter */
	if (IS_ENTER (event->keyval) &&
	    !(event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))) {
		GtkTextView *view;

		/* This is to make sure that kinput2 gets the enter. And if
		 * it's handled there we shouldn't send on it. This is because
		 * kinput2 uses Enter to commit letters. See:
		 * http://bugzilla.redhat.com/bugzilla/show_bug.cgi?id=104299
		 */

		view = GTK_TEXT_VIEW (chat->input_text_view);
		if (gtk_im_context_filter_keypress (view->im_context, event)) {
			GTK_TEXT_VIEW (chat->input_text_view)->need_im_reset = TRUE;
			return TRUE;
		}

		chat_input_text_view_send (chat);
		return TRUE;
	}

	text_view_sw = gtk_widget_get_parent (GTK_WIDGET (chat->view));

	if (IS_ENTER (event->keyval) &&
	    (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))) {
		/* Newline for shift/control-enter. */
		return FALSE;
	}
	if (!(event->state & GDK_CONTROL_MASK) &&
	    event->keyval == GDK_Page_Up) {
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (text_view_sw));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_value (adj) - gtk_adjustment_get_page_size (adj));
		return TRUE;
	}
	if ((event->state & GDK_CONTROL_MASK) != GDK_CONTROL_MASK &&
	    event->keyval == GDK_Page_Down) {
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (text_view_sw));
		val = MIN (gtk_adjustment_get_value (adj) + gtk_adjustment_get_page_size (adj),
			   gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj));
		gtk_adjustment_set_value (adj, val);
		return TRUE;
	}
	if (event->keyval == GDK_Escape) {
		empathy_search_bar_hide (EMPATHY_SEARCH_BAR (priv->search_bar));
	}
	if (!(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) &&
	    event->keyval == GDK_Tab) {
		GtkTextBuffer *buffer;
		GtkTextIter    start, current;
		gchar         *nick, *completed;
		GList         *list, *completed_list;
		gboolean       is_start_of_buffer;

		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (EMPATHY_CHAT (chat)->input_text_view));
		gtk_text_buffer_get_iter_at_mark (buffer, &current, gtk_text_buffer_get_insert (buffer));

		/* Get the start of the nick to complete. */
		gtk_text_buffer_get_iter_at_mark (buffer, &start, gtk_text_buffer_get_insert (buffer));
		if (gtk_text_iter_backward_find_char (&start, &empathy_isspace_cb, NULL, NULL)) {
			gtk_text_iter_set_offset (&start, gtk_text_iter_get_offset (&start) + 1);
		}
		is_start_of_buffer = gtk_text_iter_is_start (&start);

		list = empathy_contact_list_get_members (EMPATHY_CONTACT_LIST (priv->tp_chat));
		g_completion_add_items (priv->completion, list);

		nick = gtk_text_buffer_get_text (buffer, &start, &current, FALSE);
		completed_list = g_completion_complete (priv->completion,
							nick,
							&completed);

		g_free (nick);

		if (completed) {
			guint        len;
			const gchar *text;
			gchar       *complete_char = NULL;
			GString     *message = NULL;
			GList       *l;

			gtk_text_buffer_delete (buffer, &start, &current);

			len = g_list_length (completed_list);

			if (len == 1) {
				/* If we only have one hit, use that text
				 * instead of the text in completed since the
				 * completed text will use the typed string
				 * which might be cased all wrong.
				 * Fixes #120876
				 * */
				text = empathy_contact_get_name (completed_list->data);
			} else {
				text = completed;

				/* Print all hits to the scrollback view, so the
				 * user knows what possibilities he has.
				 * Fixes #599779
				 * */
				 message = g_string_new ("");
				 for (l = completed_list; l != NULL; l = l->next) {
					g_string_append (message, empathy_contact_get_name (l->data));
					g_string_append (message, " - ");
				 }
				 empathy_chat_view_append_event (chat->view, message->str);
				 g_string_free (message, TRUE);
			}

			gtk_text_buffer_insert_at_cursor (buffer, text, strlen (text));

			if (len == 1 && is_start_of_buffer &&
			    empathy_conf_get_string (empathy_conf_get (),
						     EMPATHY_PREFS_CHAT_NICK_COMPLETION_CHAR,
						     &complete_char) &&
			    complete_char != NULL) {
				gtk_text_buffer_insert_at_cursor (buffer,
								  complete_char,
								  strlen (complete_char));
				gtk_text_buffer_insert_at_cursor (buffer, " ", 1);
				g_free (complete_char);
			}

			g_free (completed);
		}

		g_completion_clear_items (priv->completion);

		g_list_foreach (list, (GFunc) g_object_unref, NULL);
		g_list_free (list);

		return TRUE;
	}

	return FALSE;
}

static gboolean
chat_text_view_focus_in_event_cb (GtkWidget  *widget,
				  GdkEvent   *event,
				  EmpathyChat *chat)
{
	gtk_widget_grab_focus (chat->input_text_view);

	return TRUE;
}

static gboolean
chat_input_set_size_request_idle (gpointer sw)
{
	gtk_widget_set_size_request (sw, -1, MAX_INPUT_HEIGHT);

	return FALSE;
}

static void
chat_input_size_request_cb (GtkWidget      *widget,
			    GtkRequisition *requisition,
			    EmpathyChat    *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	GtkWidget       *sw;

	sw = gtk_widget_get_parent (widget);
	if (requisition->height >= MAX_INPUT_HEIGHT && !priv->has_input_vscroll) {
		g_idle_add (chat_input_set_size_request_idle, sw);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
						GTK_POLICY_NEVER,
						GTK_POLICY_ALWAYS);
		priv->has_input_vscroll = TRUE;
	}

	if (requisition->height < MAX_INPUT_HEIGHT && priv->has_input_vscroll) {
		gtk_widget_set_size_request (sw, -1, -1);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
						GTK_POLICY_NEVER,
						GTK_POLICY_NEVER);
		priv->has_input_vscroll = FALSE;
	}
}

static void
chat_input_realize_cb (GtkWidget   *widget,
		       EmpathyChat *chat)
{
	DEBUG ("Setting focus to the input text view");
	if (gtk_widget_is_sensitive (widget)) {
		gtk_widget_grab_focus (widget);
	}
}

static void
chat_insert_smiley_activate_cb (EmpathySmileyManager *manager,
				EmpathySmiley        *smiley,
				gpointer              user_data)
{
	EmpathyChat   *chat = EMPATHY_CHAT (user_data);
	GtkTextBuffer *buffer;
	GtkTextIter    iter;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));

	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_buffer_insert (buffer, &iter, smiley->str, -1);

	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_buffer_insert (buffer, &iter, " ", -1);
}

typedef struct {
	EmpathyChat  *chat;
	gchar       *word;

	GtkTextIter  start;
	GtkTextIter  end;
} EmpathyChatSpell;

static EmpathyChatSpell *
chat_spell_new (EmpathyChat  *chat,
		const gchar *word,
		GtkTextIter  start,
		GtkTextIter  end)
{
	EmpathyChatSpell *chat_spell;

	chat_spell = g_slice_new0 (EmpathyChatSpell);

	chat_spell->chat = g_object_ref (chat);
	chat_spell->word = g_strdup (word);
	chat_spell->start = start;
	chat_spell->end = end;

	return chat_spell;
}

static void
chat_spell_free (EmpathyChatSpell *chat_spell)
{
	g_object_unref (chat_spell->chat);
	g_free (chat_spell->word);
	g_slice_free (EmpathyChatSpell, chat_spell);
}

static void
chat_spelling_menu_activate_cb (GtkMenuItem     *menu_item,
				                EmpathyChatSpell *chat_spell)
{
    empathy_chat_correct_word (chat_spell->chat,
                               &(chat_spell->start),
                               &(chat_spell->end),
                               gtk_menu_item_get_label (menu_item));
}

static GtkWidget *
chat_spelling_build_menu (EmpathyChatSpell *chat_spell)
{
    GtkWidget *menu, *menu_item;
    GList     *suggestions, *l;

    menu = gtk_menu_new ();
    suggestions = empathy_spell_get_suggestions (chat_spell->word);
    if (suggestions == NULL) {
        menu_item = gtk_menu_item_new_with_label (_("(No Suggestions)"));
	gtk_widget_set_sensitive (menu_item, FALSE);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
    } else {
        for (l = suggestions; l; l = l->next) {
            menu_item = gtk_menu_item_new_with_label (l->data);
            g_signal_connect (G_OBJECT (menu_item),
                          "activate",
                          G_CALLBACK (chat_spelling_menu_activate_cb),
                          chat_spell);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
        }
    }
    empathy_spell_free_suggestions (suggestions);

    gtk_widget_show_all (menu);

    return menu;
}

static void
chat_text_send_cb (GtkMenuItem *menuitem,
		   EmpathyChat *chat)
{
	chat_input_text_view_send (chat);
}

static void
chat_input_populate_popup_cb (GtkTextView *view,
			      GtkMenu     *menu,
			      EmpathyChat *chat)
{
	EmpathyChatPriv      *priv;
	GtkTextBuffer        *buffer;
	GtkTextTagTable      *table;
	GtkTextTag           *tag;
	gint                  x, y;
	GtkTextIter           iter, start, end;
	GtkWidget            *item;
	gchar                *str = NULL;
	EmpathyChatSpell     *chat_spell;
	GtkWidget            *spell_menu;
	EmpathySmileyManager *smiley_manager;
	GtkWidget            *smiley_menu;
	GtkWidget            *image;

	priv = GET_PRIV (chat);
	buffer = gtk_text_view_get_buffer (view);

	/* Add the emoticon menu. */
	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	item = gtk_image_menu_item_new_with_mnemonic (_("Insert Smiley"));
	image = gtk_image_new_from_icon_name ("face-smile",
					      GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	smiley_manager = empathy_smiley_manager_dup_singleton ();
	smiley_menu = empathy_smiley_menu_new (smiley_manager,
					       chat_insert_smiley_activate_cb,
					       chat);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), smiley_menu);
	g_object_unref (smiley_manager);

	/* Add the Send menu item. */
	gtk_text_buffer_get_bounds (buffer, &start, &end);
	str = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
	if (!EMP_STR_EMPTY (str)) {
		item = gtk_menu_item_new_with_mnemonic (_("_Send"));
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (chat_text_send_cb), chat);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}
	str = NULL;

	/* Add the spell check menu item. */
	table = gtk_text_buffer_get_tag_table (buffer);
	tag = gtk_text_tag_table_lookup (table, "misspelled");
	gtk_widget_get_pointer (GTK_WIDGET (view), &x, &y);
	gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (view),
					       GTK_TEXT_WINDOW_WIDGET,
					       x, y,
					       &x, &y);
	gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (view), &iter, x, y);
	start = end = iter;
	if (gtk_text_iter_backward_to_tag_toggle (&start, tag) &&
	    gtk_text_iter_forward_to_tag_toggle (&end, tag)) {

		str = gtk_text_buffer_get_text (buffer,
						&start, &end, FALSE);
	}
	if (!EMP_STR_EMPTY (str)) {
		chat_spell = chat_spell_new (chat, str, start, end);
		g_object_set_data_full (G_OBJECT (menu),
					"chat_spell", chat_spell,
					(GDestroyNotify) chat_spell_free);

		item = gtk_separator_menu_item_new ();
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);

		item = gtk_image_menu_item_new_with_mnemonic (_("_Spelling Suggestions"));
		image = gtk_image_new_from_icon_name (GTK_STOCK_SPELL_CHECK,
						      GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);

		spell_menu = chat_spelling_build_menu (chat_spell);
		gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), spell_menu);

		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}
}


#ifdef ENABLE_TPL
static gboolean
chat_log_filter (TplLogEntry *log,
		 gpointer user_data)
#else
static gboolean
chat_log_filter (EmpathyMessage *message,
		 gpointer user_data)
#endif /* ENABLE_TPL */
{
	EmpathyChat *chat = user_data;
#ifdef ENABLE_TPL
	EmpathyMessage *message;
#endif /* ENABLE_TPL */
	EmpathyChatPriv *priv = GET_PRIV (chat);
	const GList *pending;

#ifdef ENABLE_TPL
	g_return_val_if_fail (TPL_IS_LOG_ENTRY (log), FALSE);
#else
	g_return_val_if_fail (EMPATHY_IS_MESSAGE (message), FALSE);
#endif /* ENABLE_TPL */
	g_return_val_if_fail (EMPATHY_IS_CHAT (chat), FALSE);

	pending = empathy_tp_chat_get_pending_messages (priv->tp_chat);
#ifdef ENABLE_TPL
	message = empathy_message_from_tpl_log_entry (log);
#endif /* ENABLE_TPL */

	for (; pending; pending = g_list_next (pending)) {
		if (empathy_message_equal (message, pending->data)) {
			return FALSE;
		}
	}
#ifdef ENABLE_TPL
	g_object_unref (message);
#endif
	return TRUE;
}


static void
show_pending_messages (EmpathyChat *chat) {
	EmpathyChatPriv *priv = GET_PRIV (chat);
	const GList *messages, *l;

	g_return_if_fail (EMPATHY_IS_CHAT (chat));

	if (chat->view == NULL || priv->tp_chat == NULL)
		return;

	if (!priv->can_show_pending)
		return;

	messages = empathy_tp_chat_get_pending_messages (priv->tp_chat);

	for (l = messages; l != NULL ; l = g_list_next (l)) {
		EmpathyMessage *message = EMPATHY_MESSAGE (l->data);
		chat_message_received (chat, message);
	}
}


#ifdef ENABLE_TPL
static void
got_filtered_messages_cb (GObject *manager,
		GAsyncResult *result,
		gpointer user_data)
{
	GList *l;
	GList *messages;
	EmpathyChat *chat = EMPATHY_CHAT (user_data);
	EmpathyChatPriv *priv = GET_PRIV (chat);
	GError *error = NULL;

	messages = tpl_log_manager_get_filtered_messages_async_finish (result, &error);

	if (error != NULL) {
		DEBUG ("%s. Aborting.", error->message);
		empathy_chat_view_append_event (chat->view,
			_("Failed to retrieve recent logs"));
		g_error_free (error);
		goto out;
	}

	for (l = messages; l; l = g_list_next (l)) {
		EmpathyMessage *message;
		g_assert (TPL_IS_LOG_ENTRY (l->data));

		message = empathy_message_from_tpl_log_entry (l->data);
		g_object_unref (l->data);

		empathy_chat_view_append_message (chat->view, message);
		g_object_unref (message);
	}
	g_list_free (messages);

out:
	/* in case of TPL error, skip backlog and show pending messages */
	priv->can_show_pending = TRUE;
	show_pending_messages (chat);

	/* FIXME: See Bug#610994, we are forcing the ACK of the queue. See comments
	 * about it in EmpathyChatPriv definition */
	priv->retrieving_backlogs = FALSE;
	empathy_chat_messages_read (chat);

	/* Turn back on scrolling */
	empathy_chat_view_scroll (chat->view, TRUE);
}
#endif /* ENABLE_TPL */


static void
chat_add_logs (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	gboolean         is_chatroom;
#ifndef ENABLE_TPL
	GList           *messages, *l;
#endif /* ENABLE_TPL */

	if (!priv->id) {
		return;
	}

	/* Turn off scrolling temporarily */
	empathy_chat_view_scroll (chat->view, FALSE);

	/* Add messages from last conversation */
	is_chatroom = priv->handle_type == TP_HANDLE_TYPE_ROOM;

#ifndef ENABLE_TPL
	messages = empathy_log_manager_get_filtered_messages (priv->log_manager,
							      priv->account,
							      priv->id,
							      is_chatroom,
							      5,
							      chat_log_filter,
							      chat);

	for (l = messages; l; l = g_list_next (l)) {
		empathy_chat_view_append_message (chat->view, l->data);
		g_object_unref (l->data);
	}

	g_list_free (messages);

	/* Turn back on scrolling */
	empathy_chat_view_scroll (chat->view, TRUE);
#else
	priv->retrieving_backlogs = TRUE;
	tpl_log_manager_get_filtered_messages_async (priv->log_manager,
							      priv->account,
							      priv->id,
							      is_chatroom,
							      5,
							      chat_log_filter,
							      chat,
							      got_filtered_messages_cb,
							      (gpointer) chat);
#endif /* ENABLE_TPL */
}

static gint
chat_contacts_completion_func (const gchar *s1,
			       const gchar *s2,
			       gsize        n)
{
	gchar *tmp, *nick1, *nick2;
	gint   ret;

	if (s1 == s2) {
		return 0;
	}
	if (!s1 || !s2) {
		return s1 ? -1 : +1;
	}

	tmp = g_utf8_normalize (s1, -1, G_NORMALIZE_DEFAULT);
	nick1 = g_utf8_casefold (tmp, -1);
	g_free (tmp);

	tmp = g_utf8_normalize (s2, -1, G_NORMALIZE_DEFAULT);
	nick2 = g_utf8_casefold (tmp, -1);
	g_free (tmp);

	ret = strncmp (nick1, nick2, n);

	g_free (nick1);
	g_free (nick2);

	return ret;
}

static gchar *
build_part_message (guint           reason,
		    const gchar    *name,
		    EmpathyContact *actor,
		    const gchar    *message)
{
	GString *s = g_string_new ("");
	const gchar *actor_name = NULL;

	if (actor != NULL) {
		actor_name = empathy_contact_get_name (actor);
	}

	/* Having an actor only really makes sense for a few actions... */
	switch (reason) {
	case TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE:
		g_string_append_printf (s, _("%s has disconnected"), name);
		break;
	case TP_CHANNEL_GROUP_CHANGE_REASON_KICKED:
		if (actor_name != NULL) {
			/* translators: reverse the order of these arguments
			 * if the kicked should come before the kicker in your locale.
			 */
			g_string_append_printf (s, _("%1$s was kicked by %2$s"),
				name, actor_name);
		} else {
			g_string_append_printf (s, _("%s was kicked"), name);
		}
		break;
	case TP_CHANNEL_GROUP_CHANGE_REASON_BANNED:
		if (actor_name != NULL) {
			/* translators: reverse the order of these arguments
			 * if the banned should come before the banner in your locale.
			 */
			g_string_append_printf (s, _("%1$s was banned by %2$s"),
				name, actor_name);
		} else {
			g_string_append_printf (s, _("%s was banned"), name);
		}
		break;
	default:
		g_string_append_printf (s, _("%s has left the room"), name);
	}

	if (!EMP_STR_EMPTY (message)) {
		/* Note to translators: this string is appended to
		 * notifications like "foo has left the room", with the message
		 * given by the user living the room. If this poses a problem,
		 * please let us know. :-)
		 */
		g_string_append_printf (s, _(" (%s)"), message);
	}

	return g_string_free (s, FALSE);
}

static void
chat_members_changed_cb (EmpathyTpChat  *tp_chat,
			 EmpathyContact *contact,
			 EmpathyContact *actor,
			 guint           reason,
			 gchar          *message,
			 gboolean        is_member,
			 EmpathyChat    *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	const gchar *name = empathy_contact_get_name (contact);
	gchar *str;

	g_return_if_fail (TP_CHANNEL_GROUP_CHANGE_REASON_RENAMED != reason);

	if (priv->block_events_timeout_id != 0)
		return;

	if (is_member) {
		str = g_strdup_printf (_("%s has joined the room"),
				       name);
	} else {
		str = build_part_message (reason, name, actor, message);
	}

	empathy_chat_view_append_event (chat->view, str);
	g_free (str);
}

static void
chat_member_renamed_cb (EmpathyTpChat  *tp_chat,
			 EmpathyContact *old_contact,
			 EmpathyContact *new_contact,
			 guint           reason,
			 gchar          *message,
			 EmpathyChat    *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);

	g_return_if_fail (TP_CHANNEL_GROUP_CHANGE_REASON_RENAMED == reason);

	if (priv->block_events_timeout_id == 0) {
		gchar *str;

		str = g_strdup_printf (_("%s is now known as %s"),
				       empathy_contact_get_name (old_contact),
				       empathy_contact_get_name (new_contact));
		empathy_chat_view_append_event (chat->view, str);
		g_free (str);
	}

}

static gboolean
chat_reset_size_request (gpointer widget)
{
	gtk_widget_set_size_request (widget, -1, -1);

	return FALSE;
}

static void
chat_update_contacts_visibility (EmpathyChat *chat,
			 gboolean show)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	GtkAllocation allocation;

	if (!priv->scrolled_window_contacts) {
		return;
	}

	if (priv->remote_contact != NULL) {
		show = FALSE;
	}

	if (show && priv->contact_list_view == NULL) {
		EmpathyContactListStore *store;
		gint                     min_width;

		/* We are adding the contact list to the chat, we don't want the
		 * chat view to become too small. If the chat view is already
		 * smaller than 250 make sure that size won't change. If the
		 * chat view is bigger the contact list will take some space on
		 * it but we make sure the chat view don't become smaller than
		 * 250. Relax the size request once the resize is done */
		gtk_widget_get_allocation (priv->vbox_left, &allocation);
		min_width = MIN (allocation.width, 250);
		gtk_widget_set_size_request (priv->vbox_left, min_width, -1);
		g_idle_add (chat_reset_size_request, priv->vbox_left);

		if (priv->contacts_width > 0) {
			gtk_paned_set_position (GTK_PANED (priv->hpaned),
						priv->contacts_width);
		}

		store = empathy_contact_list_store_new (EMPATHY_CONTACT_LIST (priv->tp_chat));
		priv->contact_list_view = GTK_WIDGET (empathy_contact_list_view_new (store,
			EMPATHY_CONTACT_LIST_FEATURE_CONTACT_TOOLTIP,
			EMPATHY_CONTACT_FEATURE_CHAT |
			EMPATHY_CONTACT_FEATURE_CALL |
			EMPATHY_CONTACT_FEATURE_LOG |
			EMPATHY_CONTACT_FEATURE_INFO));
		gtk_container_add (GTK_CONTAINER (priv->scrolled_window_contacts),
				   priv->contact_list_view);
		gtk_widget_show (priv->contact_list_view);
		gtk_widget_show (priv->scrolled_window_contacts);
		g_object_unref (store);
	} else if (!show) {
		priv->contacts_width = gtk_paned_get_position (GTK_PANED (priv->hpaned));
		gtk_widget_hide (priv->scrolled_window_contacts);
		if (priv->contact_list_view != NULL) {
			gtk_widget_destroy (priv->contact_list_view);
			priv->contact_list_view = NULL;
		}
	}
}

void
empathy_chat_set_show_contacts (EmpathyChat *chat,
				gboolean     show)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);

	priv->show_contacts = show;

	chat_update_contacts_visibility (chat, show);

	g_object_notify (G_OBJECT (chat), "show-contacts");
}

static void
chat_remote_contact_changed_cb (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);

	if (priv->remote_contact != NULL) {
		g_object_unref (priv->remote_contact);
		priv->remote_contact = NULL;
	}

	g_free (priv->id);

	priv->id = g_strdup (empathy_tp_chat_get_id (priv->tp_chat));
	priv->remote_contact = empathy_tp_chat_get_remote_contact (priv->tp_chat);
	if (priv->remote_contact != NULL) {
		g_object_ref (priv->remote_contact);
		priv->handle_type = TP_HANDLE_TYPE_CONTACT;
	}
	else if (priv->tp_chat != NULL) {
		TpChannel *channel;

		channel = empathy_tp_chat_get_channel (priv->tp_chat);
		g_object_get (channel, "handle-type", &priv->handle_type, NULL);
	}

	chat_update_contacts_visibility (chat, priv->show_contacts);

	g_object_notify (G_OBJECT (chat), "remote-contact");
	g_object_notify (G_OBJECT (chat), "id");
}

static void
chat_destroy_cb (EmpathyTpChat *tp_chat,
		 EmpathyChat   *chat)
{
	EmpathyChatPriv *priv;

	priv = GET_PRIV (chat);

	if (!priv->tp_chat) {
		return;
	}

	chat_composing_remove_timeout (chat);
	g_object_unref (priv->tp_chat);
	priv->tp_chat = NULL;
	g_object_notify (G_OBJECT (chat), "tp-chat");

	empathy_chat_view_append_event (chat->view, _("Disconnected"));
	gtk_widget_set_sensitive (chat->input_text_view, FALSE);

	chat_update_contacts_visibility (chat, FALSE);
}

static gboolean
chat_hpaned_pos_changed_cb (GtkWidget* hpaned, gpointer user_data)
{
	gint hpaned_pos;
	hpaned_pos = gtk_paned_get_position (GTK_PANED(hpaned));
	empathy_conf_set_int (empathy_conf_get (),
			      EMPATHY_PREFS_UI_CHAT_WINDOW_PANED_POS,
			      hpaned_pos);
	return TRUE;
}

static void
chat_create_ui (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	GtkBuilder      *gui;
 	GList           *list = NULL;
	gchar           *filename;
	GtkTextBuffer   *buffer;
	gint              paned_pos;

	filename = empathy_file_lookup ("empathy-chat.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
					"chat_widget", &priv->widget,
					"hpaned", &priv->hpaned,
					"vbox_left", &priv->vbox_left,
					"scrolled_window_chat", &priv->scrolled_window_chat,
					"scrolled_window_input", &priv->scrolled_window_input,
					"hbox_topic", &priv->hbox_topic,
					"label_topic", &priv->label_topic,
					"scrolled_window_contacts", &priv->scrolled_window_contacts,
					"info_bar_vbox", &priv->info_bar_vbox,
					NULL);
	g_free (filename);

	/* Add message view. */
	chat->view = empathy_theme_manager_create_view (empathy_theme_manager_get ());
	/* If this is a GtkTextView, it's set as a drag destination for text/plain
	   and other types, even though it's non-editable and doesn't accept any
	   drags.  This steals drag motion for anything inside the scrollbars,
	   making drag destinations on chat windows far less useful.
	 */
	gtk_drag_dest_unset (GTK_WIDGET (chat->view));
	g_signal_connect (chat->view, "focus_in_event",
			  G_CALLBACK (chat_text_view_focus_in_event_cb),
			  chat);
	gtk_container_add (GTK_CONTAINER (priv->scrolled_window_chat),
			   GTK_WIDGET (chat->view));
	gtk_widget_show (GTK_WIDGET (chat->view));

	/* Add input GtkTextView */
	chat->input_text_view = g_object_new (GTK_TYPE_TEXT_VIEW,
					      "pixels-above-lines", 2,
					      "pixels-below-lines", 2,
					      "pixels-inside-wrap", 1,
					      "right-margin", 2,
					      "left-margin", 2,
					      "wrap-mode", GTK_WRAP_WORD_CHAR,
					      NULL);
	g_signal_connect (chat->input_text_view, "key-press-event",
			  G_CALLBACK (chat_input_key_press_event_cb),
			  chat);
	g_signal_connect (chat->input_text_view, "size-request",
			  G_CALLBACK (chat_input_size_request_cb),
			  chat);
	g_signal_connect (chat->input_text_view, "realize",
			  G_CALLBACK (chat_input_realize_cb),
			  chat);
	g_signal_connect (chat->input_text_view, "populate-popup",
			  G_CALLBACK (chat_input_populate_popup_cb),
			  chat);
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));
	empathy_signal_connect_weak  (buffer, "changed",
			  G_CALLBACK (chat_input_text_buffer_changed_cb),
			  G_OBJECT (chat));
	gtk_text_buffer_create_tag (buffer, "misspelled",
				    "underline", PANGO_UNDERLINE_ERROR,
				    NULL);
	gtk_container_add (GTK_CONTAINER (priv->scrolled_window_input),
			   chat->input_text_view);
	gtk_widget_show (chat->input_text_view);

	/* Add the (invisible) search bar */
	priv->search_bar = empathy_search_bar_new (chat->view);
	gtk_box_pack_start (GTK_BOX(priv->vbox_left),
	                    priv->search_bar,
	                    FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX(priv->vbox_left), priv->search_bar, 1);

	/* Initialy hide the topic, will be shown if not empty */
	gtk_widget_hide (priv->hbox_topic);

	g_signal_connect (priv->hpaned, "notify::position",
			  G_CALLBACK (chat_hpaned_pos_changed_cb),
			  NULL);

        /* Load the paned position */
	if (empathy_conf_get_int (empathy_conf_get (),
				 EMPATHY_PREFS_UI_CHAT_WINDOW_PANED_POS,
				 &paned_pos)
		&& paned_pos)
		gtk_paned_set_position (GTK_PANED(priv->hpaned), paned_pos);

	/* Set widget focus order */
	list = g_list_append (NULL, priv->search_bar);
	list = g_list_append (list, priv->scrolled_window_input);
	gtk_container_set_focus_chain (GTK_CONTAINER (priv->vbox_left), list);
	g_list_free (list);

	list = g_list_append (NULL, priv->vbox_left);
	list = g_list_append (list, priv->scrolled_window_contacts);
	gtk_container_set_focus_chain (GTK_CONTAINER (priv->hpaned), list);
	g_list_free (list);

	list = g_list_append (NULL, priv->hpaned);
	list = g_list_append (list, priv->hbox_topic);
	gtk_container_set_focus_chain (GTK_CONTAINER (priv->widget), list);
	g_list_free (list);

	/* Add the main widget in the chat widget */
	gtk_container_add (GTK_CONTAINER (chat), priv->widget);
	g_object_unref (gui);
}

static void
chat_size_request (GtkWidget      *widget,
		   GtkRequisition *requisition)
{
  GtkBin *bin = GTK_BIN (widget);
  GtkWidget *child;

  requisition->width = gtk_container_get_border_width (GTK_CONTAINER (widget)) * 2;
  requisition->height = gtk_container_get_border_width (GTK_CONTAINER (widget)) * 2;

  child = gtk_bin_get_child (bin);

  if (child && gtk_widget_get_visible (child))
    {
      GtkRequisition child_requisition;

      gtk_widget_size_request (child, &child_requisition);

      requisition->width += child_requisition.width;
      requisition->height += child_requisition.height;
    }
}

static void
chat_size_allocate (GtkWidget     *widget,
		    GtkAllocation *allocation)
{
  GtkBin *bin = GTK_BIN (widget);
  GtkAllocation child_allocation;
  GtkWidget *child;

  gtk_widget_set_allocation (widget, allocation);

  child = gtk_bin_get_child (bin);

  if (child && gtk_widget_get_visible (child))
    {
      child_allocation.x = allocation->x + gtk_container_get_border_width (GTK_CONTAINER (widget));
      child_allocation.y = allocation->y + gtk_container_get_border_width (GTK_CONTAINER (widget));
      child_allocation.width = MAX (allocation->width - gtk_container_get_border_width (GTK_CONTAINER (widget)) * 2, 0);
      child_allocation.height = MAX (allocation->height - gtk_container_get_border_width (GTK_CONTAINER (widget)) * 2, 0);

      gtk_widget_size_allocate (child, &child_allocation);
    }
}

static void
chat_finalize (GObject *object)
{
	EmpathyChat     *chat;
	EmpathyChatPriv *priv;

	chat = EMPATHY_CHAT (object);
	priv = GET_PRIV (chat);

	DEBUG ("Finalized: %p", object);

	g_list_foreach (priv->input_history, (GFunc) chat_input_history_entry_free, NULL);
	g_list_free (priv->input_history);

	g_list_foreach (priv->compositors, (GFunc) g_object_unref, NULL);
	g_list_free (priv->compositors);

	chat_composing_remove_timeout (chat);

	g_object_unref (priv->account_manager);
	g_object_unref (priv->log_manager);

	if (priv->tp_chat) {
		g_signal_handlers_disconnect_by_func (priv->tp_chat,
			chat_destroy_cb, chat);
		g_signal_handlers_disconnect_by_func (priv->tp_chat,
			chat_message_received_cb, chat);
		g_signal_handlers_disconnect_by_func (priv->tp_chat,
			chat_send_error_cb, chat);
		g_signal_handlers_disconnect_by_func (priv->tp_chat,
			chat_state_changed_cb, chat);
		g_signal_handlers_disconnect_by_func (priv->tp_chat,
			chat_property_changed_cb, chat);
		g_signal_handlers_disconnect_by_func (priv->tp_chat,
			chat_members_changed_cb, chat);
		g_signal_handlers_disconnect_by_func (priv->tp_chat,
			chat_remote_contact_changed_cb, chat);
		empathy_tp_chat_leave (priv->tp_chat);
		g_object_unref (priv->tp_chat);
	}
	if (priv->account) {
		g_object_unref (priv->account);
	}
	if (priv->remote_contact) {
		g_object_unref (priv->remote_contact);
	}

	if (priv->block_events_timeout_id) {
		g_source_remove (priv->block_events_timeout_id);
	}

	g_free (priv->id);
	g_free (priv->name);
	g_free (priv->subject);
	g_completion_free (priv->completion);

	G_OBJECT_CLASS (empathy_chat_parent_class)->finalize (object);
}

static void
chat_constructed (GObject *object)
{
	EmpathyChat *chat = EMPATHY_CHAT (object);
	EmpathyChatPriv *priv = GET_PRIV (chat);

	if (priv->handle_type != TP_HANDLE_TYPE_ROOM)
		chat_add_logs (chat);
#ifndef ENABLE_TPL
	/* When async API are involved, pending message are shown at the end of the
	 * callbacks' chain fired by chat_add_logs */
	priv->can_show_pending = TRUE;
	show_pending_messages (chat);
#endif /* ENABLE_TPL */
}

static void
empathy_chat_class_init (EmpathyChatClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = chat_finalize;
	object_class->get_property = chat_get_property;
	object_class->set_property = chat_set_property;
	object_class->constructed = chat_constructed;

	widget_class->size_request = chat_size_request;
	widget_class->size_allocate = chat_size_allocate;

	g_object_class_install_property (object_class,
					 PROP_TP_CHAT,
					 g_param_spec_object ("tp-chat",
							      "Empathy tp chat",
							      "The tp chat object",
							      EMPATHY_TYPE_TP_CHAT,
							      G_PARAM_CONSTRUCT |
							      G_PARAM_READWRITE |
							      G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_ACCOUNT,
					 g_param_spec_object ("account",
							      "Account of the chat",
							      "The account of the chat",
							      TP_TYPE_ACCOUNT,
							      G_PARAM_READABLE |
							      G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_ID,
					 g_param_spec_string ("id",
							      "Chat's id",
							      "The id of the chat",
							      NULL,
							      G_PARAM_READABLE |
							      G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "Chat's name",
							      "The name of the chat",
							      NULL,
							      G_PARAM_READABLE |
							      G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_SUBJECT,
					 g_param_spec_string ("subject",
							      "Chat's subject",
							      "The subject or topic of the chat",
							      NULL,
							      G_PARAM_READABLE |
							      G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_REMOTE_CONTACT,
					 g_param_spec_object ("remote-contact",
							      "The remote contact",
							      "The remote contact is any",
							      EMPATHY_TYPE_CONTACT,
							      G_PARAM_READABLE |
							      G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_SHOW_CONTACTS,
					 g_param_spec_boolean ("show-contacts",
							       "Contacts' visibility",
							       "The visibility of the contacts' list",
							       TRUE,
							       G_PARAM_READWRITE |
							       G_PARAM_STATIC_STRINGS));

	signals[COMPOSING] =
		g_signal_new ("composing",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1, G_TYPE_BOOLEAN);

	signals[NEW_MESSAGE] =
		g_signal_new ("new-message",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, EMPATHY_TYPE_MESSAGE);

	g_type_class_add_private (object_class, sizeof (EmpathyChatPriv));
}

static gboolean
chat_block_events_timeout_cb (gpointer data)
{
	EmpathyChatPriv *priv = GET_PRIV (data);

	priv->block_events_timeout_id = 0;

	return FALSE;
}

static void
account_manager_prepared_cb (GObject *source_object,
			     GAsyncResult *result,
			     gpointer user_data)
{
	GList *accounts, *l;
	TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
	EmpathyChat *chat = user_data;
	GError *error = NULL;

	if (!tp_account_manager_prepare_finish (account_manager, result, &error)) {
		DEBUG ("Failed to prepare the account manager: %s", error->message);
		g_error_free (error);
		return;
	}

	accounts = tp_account_manager_get_valid_accounts (account_manager);

	for (l = accounts; l != NULL; l = l->next) {
		TpAccount *account = l->data;
		empathy_signal_connect_weak (account, "status-changed",
					     G_CALLBACK (chat_new_connection_cb),
					     G_OBJECT (chat));
	}

	g_list_free (accounts);
}

static void
empathy_chat_init (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (chat,
		EMPATHY_TYPE_CHAT, EmpathyChatPriv);

	chat->priv = priv;
#ifndef ENABLE_TPL
	priv->log_manager = empathy_log_manager_dup_singleton ();
#else
	priv->log_manager = tpl_log_manager_dup_singleton ();
#endif /* ENABLE_TPL */
	priv->contacts_width = -1;
	priv->input_history = NULL;
	priv->input_history_current = NULL;
	priv->account_manager = tp_account_manager_dup ();

	tp_account_manager_prepare_async (priv->account_manager, NULL,
					  account_manager_prepared_cb, chat);

	empathy_conf_get_bool (empathy_conf_get (),
			       EMPATHY_PREFS_CHAT_SHOW_CONTACTS_IN_ROOMS,
			       &priv->show_contacts);

	/* Block events for some time to avoid having "has come online" or
	 * "joined" messages. */
	priv->block_events_timeout_id =
		g_timeout_add_seconds (1, chat_block_events_timeout_cb, chat);

	/* Add nick name completion */
	priv->completion = g_completion_new ((GCompletionFunc) empathy_contact_get_name);
	g_completion_set_compare (priv->completion, chat_contacts_completion_func);

	chat_create_ui (chat);
}

EmpathyChat *
empathy_chat_new (EmpathyTpChat *tp_chat)
{
	return g_object_new (EMPATHY_TYPE_CHAT, "tp-chat", tp_chat, NULL);
}

EmpathyTpChat *
empathy_chat_get_tp_chat (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_CHAT (chat), NULL);

	return priv->tp_chat;
}

static void display_password_info_bar (EmpathyChat *self,
				       gboolean retry);

static void
provide_password_cb (GObject *tp_chat,
		     GAsyncResult *res,
		     gpointer user_data)
{
	EmpathyChat *self = EMPATHY_CHAT (user_data);
	EmpathyChatPriv *priv = GET_PRIV (self);
	GError *error = NULL;

	if (!empathy_tp_chat_provide_password_finish (EMPATHY_TP_CHAT (tp_chat), res,
						      &error)) {
		DEBUG ("error: %s", error->message);
		/* FIXME: what should we do if that's another error? Close the channel?
		 * Display the raw D-Bus error to the user isn't very useful */
		if (g_error_matches (error, TP_ERRORS, TP_ERROR_AUTHENTICATION_FAILED))
			display_password_info_bar (self, TRUE);
		g_error_free (error);
		return;
	}

	/* Room joined */
	gtk_widget_set_sensitive (priv->hpaned, TRUE);
	gtk_widget_grab_focus (self->input_text_view);
}

static void
password_infobar_response_cb (GtkWidget *info_bar,
			      gint response_id,
			      EmpathyChat *self)
{
	EmpathyChatPriv *priv = GET_PRIV (self);
	GtkWidget *entry;
	const gchar *password;

	if (response_id != GTK_RESPONSE_OK)
		goto out;

	entry = g_object_get_data (G_OBJECT (info_bar), "password-entry");
	g_assert (entry != NULL);

	password = gtk_entry_get_text (GTK_ENTRY (entry));

	empathy_tp_chat_provide_password_async (priv->tp_chat, password,
						provide_password_cb, self);

 out:
	gtk_widget_destroy (info_bar);
}

static void
password_entry_activate_cb (GtkWidget *entry,
			  GtkWidget *info_bar)
{
	gtk_info_bar_response (GTK_INFO_BAR (info_bar), GTK_RESPONSE_OK);
}

static void
passwd_join_button_cb (GtkButton *button,
			  GtkWidget *info_bar)
{
	gtk_info_bar_response (GTK_INFO_BAR (info_bar), GTK_RESPONSE_OK);
}

static void
display_password_info_bar (EmpathyChat *self,
			   gboolean retry)
{
	EmpathyChatPriv *priv = GET_PRIV (self);
	GtkWidget *info_bar;
	GtkWidget *content_area;
	GtkWidget *hbox;
	GtkWidget *image;
	GtkWidget *label;
	GtkWidget *entry;
	GtkWidget *alig;
	GtkWidget *button;
	GtkMessageType type;
	const gchar *msg, *button_label;

	if (retry) {
		/* Previous password was wrong */
		type = GTK_MESSAGE_ERROR;
		msg = _("Wrong password; please try again:");
		button_label = _("Retry");
	}
	else {
		/* First time we're trying to join */
		type = GTK_MESSAGE_QUESTION;
		msg = _("This room is protected by a password:");
		button_label = _("Join");
	}

	info_bar = gtk_info_bar_new ();
	gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar), type);

	content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));

	hbox = gtk_hbox_new (FALSE, 3);
	gtk_container_add (GTK_CONTAINER (content_area), hbox);

	/* Add image */
	image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_AUTHENTICATION,
					  GTK_ICON_SIZE_DIALOG);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	/* Add message */
	label = gtk_label_new (msg);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

	/* Add password entry */
	entry = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);

	g_signal_connect (entry, "activate",
			  G_CALLBACK (password_entry_activate_cb), info_bar);

	/* Focus the password entry once it's realized */
	g_signal_connect (entry, "realize", G_CALLBACK (gtk_widget_grab_focus), NULL);

	/* Add 'Join' button */
	alig = gtk_alignment_new (0, 0.5, 0, 0);

	button = gtk_button_new_with_label (button_label);
	gtk_container_add (GTK_CONTAINER (alig), button);
	gtk_box_pack_start (GTK_BOX (hbox), alig, FALSE, FALSE, 0);

	g_signal_connect (button, "clicked", G_CALLBACK (passwd_join_button_cb),
			  info_bar);

	g_object_set_data (G_OBJECT (info_bar), "password-entry", entry);

	gtk_box_pack_start (GTK_BOX (priv->info_bar_vbox), info_bar,
			    FALSE, FALSE, 3);
	gtk_widget_show_all (hbox);

	g_signal_connect (info_bar, "response",
			  G_CALLBACK (password_infobar_response_cb), self);

	gtk_widget_show_all (info_bar);
}

static void
chat_password_needed_changed_cb (EmpathyChat *self)
{
	EmpathyChatPriv *priv = GET_PRIV (self);

	if (empathy_tp_chat_password_needed (priv->tp_chat)) {
		display_password_info_bar (self, FALSE);
		gtk_widget_set_sensitive (priv->hpaned, FALSE);
	}
}

void
empathy_chat_set_tp_chat (EmpathyChat   *chat,
			  EmpathyTpChat *tp_chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	TpConnection    *connection;
	GPtrArray       *properties;

	g_return_if_fail (EMPATHY_IS_CHAT (chat));
	g_return_if_fail (EMPATHY_IS_TP_CHAT (tp_chat));
	g_return_if_fail (empathy_tp_chat_is_ready (tp_chat));

	if (priv->tp_chat) {
		return;
	}

	if (priv->account) {
		g_object_unref (priv->account);
	}

	priv->tp_chat = g_object_ref (tp_chat);
	connection = empathy_tp_chat_get_connection (priv->tp_chat);
	priv->account = g_object_ref (empathy_get_account_for_connection (connection));

	g_signal_connect (tp_chat, "destroy",
			  G_CALLBACK (chat_destroy_cb),
			  chat);
	g_signal_connect (tp_chat, "message-received",
			  G_CALLBACK (chat_message_received_cb),
			  chat);
	g_signal_connect (tp_chat, "send-error",
			  G_CALLBACK (chat_send_error_cb),
			  chat);
	g_signal_connect (tp_chat, "chat-state-changed",
			  G_CALLBACK (chat_state_changed_cb),
			  chat);
	g_signal_connect (tp_chat, "property-changed",
			  G_CALLBACK (chat_property_changed_cb),
			  chat);
	g_signal_connect (tp_chat, "members-changed",
			  G_CALLBACK (chat_members_changed_cb),
			  chat);
	g_signal_connect (tp_chat, "member-renamed",
			  G_CALLBACK (chat_member_renamed_cb),
			  chat);
	g_signal_connect_swapped (tp_chat, "notify::remote-contact",
				  G_CALLBACK (chat_remote_contact_changed_cb),
				  chat);
	g_signal_connect_swapped (tp_chat, "notify::password-needed",
				  G_CALLBACK (chat_password_needed_changed_cb),
				  chat);

	/* Get initial value of properties */
	properties = empathy_tp_chat_get_properties (priv->tp_chat);
	if (properties != NULL) {
		guint i;

		for (i = 0; i < properties->len; i++) {
			EmpathyTpChatProperty *property;

			property = g_ptr_array_index (properties, i);
			if (property->value == NULL)
				continue;

			chat_property_changed_cb (priv->tp_chat,
						  property->name,
						  property->value,
						  chat);
		}
	}

	chat_remote_contact_changed_cb (chat);

	if (chat->input_text_view) {
		gtk_widget_set_sensitive (chat->input_text_view, TRUE);
		if (priv->block_events_timeout_id == 0) {
			empathy_chat_view_append_event (chat->view, _("Connected"));
		}
	}

	g_object_notify (G_OBJECT (chat), "tp-chat");
	g_object_notify (G_OBJECT (chat), "id");
	g_object_notify (G_OBJECT (chat), "account");

	/* This is a noop when tp-chat is set at object construction time and causes
	 * the pending messages to be show when it's set on the object after it has
	 * been created */
	show_pending_messages (chat);

	/* check if a password is needed */
	chat_password_needed_changed_cb (chat);
}

TpAccount *
empathy_chat_get_account (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_CHAT (chat), NULL);

	return priv->account;
}

const gchar *
empathy_chat_get_id (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_CHAT (chat), NULL);

	return priv->id;
}

const gchar *
empathy_chat_get_name (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	const gchar *ret;

	g_return_val_if_fail (EMPATHY_IS_CHAT (chat), NULL);

	ret = priv->name;
	if (!ret && priv->remote_contact) {
		ret = empathy_contact_get_name (priv->remote_contact);
	}

	if (!ret)
		ret = priv->id;

	return ret ? ret : _("Conversation");
}

const gchar *
empathy_chat_get_subject (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_CHAT (chat), NULL);

	return priv->subject;
}

EmpathyContact *
empathy_chat_get_remote_contact (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_CHAT (chat), NULL);

	return priv->remote_contact;
}

GtkWidget *
empathy_chat_get_contact_menu (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);
	GtkWidget       *menu = NULL;

	g_return_val_if_fail (EMPATHY_IS_CHAT (chat), NULL);

	if (priv->remote_contact) {
		menu = empathy_contact_menu_new (priv->remote_contact,
						 EMPATHY_CONTACT_FEATURE_CALL |
						 EMPATHY_CONTACT_FEATURE_LOG |
						 EMPATHY_CONTACT_FEATURE_INFO);
	}
	else if (priv->contact_list_view) {
		EmpathyContactListView *view;

		view = EMPATHY_CONTACT_LIST_VIEW (priv->contact_list_view);
		menu = empathy_contact_list_view_get_contact_menu (view);
	}

	return menu;
}

void
empathy_chat_clear (EmpathyChat *chat)
{
	g_return_if_fail (EMPATHY_IS_CHAT (chat));

	empathy_chat_view_clear (chat->view);
}

void
empathy_chat_scroll_down (EmpathyChat *chat)
{
	g_return_if_fail (EMPATHY_IS_CHAT (chat));

	empathy_chat_view_scroll_down (chat->view);
}

void
empathy_chat_cut (EmpathyChat *chat)
{
	GtkTextBuffer *buffer;

	g_return_if_fail (EMPATHY_IS_CHAT (chat));

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));
	if (gtk_text_buffer_get_has_selection (buffer)) {
		GtkClipboard *clipboard;

		clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

		gtk_text_buffer_cut_clipboard (buffer, clipboard, TRUE);
	}
}

void
empathy_chat_copy (EmpathyChat *chat)
{
	GtkTextBuffer *buffer;

	g_return_if_fail (EMPATHY_IS_CHAT (chat));

	if (empathy_chat_view_get_has_selection (chat->view)) {
		empathy_chat_view_copy_clipboard (chat->view);
		return;
	}

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));
	if (gtk_text_buffer_get_has_selection (buffer)) {
		GtkClipboard *clipboard;

		clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

		gtk_text_buffer_copy_clipboard (buffer, clipboard);
	}
}

void
empathy_chat_paste (EmpathyChat *chat)
{
	GtkTextBuffer *buffer;
	GtkClipboard  *clipboard;
	EmpathyChatPriv *priv;

	g_return_if_fail (EMPATHY_IS_CHAT (chat));

	priv = GET_PRIV (chat);

	if (priv->tp_chat == NULL ||
	    !GTK_WIDGET_IS_SENSITIVE (chat->input_text_view))
		return;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));
	clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

	gtk_text_buffer_paste_clipboard (buffer, clipboard, NULL, TRUE);
}

void
empathy_chat_find (EmpathyChat *chat)
{
	EmpathyChatPriv *priv;

	g_return_if_fail (EMPATHY_IS_CHAT (chat));

	priv = GET_PRIV (chat);

	empathy_search_bar_show (EMPATHY_SEARCH_BAR (priv->search_bar));
}

void
empathy_chat_correct_word (EmpathyChat  *chat,
			  GtkTextIter *start,
			  GtkTextIter *end,
			  const gchar *new_word)
{
	GtkTextBuffer *buffer;

	g_return_if_fail (chat != NULL);
	g_return_if_fail (new_word != NULL);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));

	gtk_text_buffer_delete (buffer, start, end);
	gtk_text_buffer_insert (buffer, start,
				new_word,
				-1);
}

gboolean
empathy_chat_is_room (EmpathyChat *chat)
{
	EmpathyChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_CHAT (chat), FALSE);

	return (priv->handle_type == TP_HANDLE_TYPE_ROOM);
}

guint
empathy_chat_get_nb_unread_messages (EmpathyChat *self)
{
	EmpathyChatPriv *priv = GET_PRIV (self);

	g_return_val_if_fail (EMPATHY_IS_CHAT (self), 0);

	return priv->unread_messages;
}

/* called when the messages have been read by user */
void
empathy_chat_messages_read (EmpathyChat *self)
{
	EmpathyChatPriv *priv = GET_PRIV (self);

	g_return_if_fail (EMPATHY_IS_CHAT (self));

	/* FIXME: See Bug#610994, See comments about it in EmpathyChatPriv
	 * definition. If we are still retrieving the backlogs, do not ACK */
	if (priv->retrieving_backlogs)
		return;

	if (priv->tp_chat != NULL ) {
			empathy_tp_chat_acknowledge_all_messages (priv->tp_chat);
	}
	priv->unread_messages = 0;
}
