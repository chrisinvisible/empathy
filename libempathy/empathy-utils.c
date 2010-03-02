/*
 * Copyright (C) 2003-2007 Imendio AB
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
 * Authors: Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#include "config.h"

#include <string.h>
#include <time.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include <libxml/uri.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>

#include "empathy-utils.h"
#include "empathy-contact-manager.h"
#include "empathy-dispatcher.h"
#include "empathy-dispatch-operation.h"
#include "empathy-idle.h"
#include "empathy-tp-call.h"

#include <extensions/extensions.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include "empathy-debug.h"

/* Translation between presence types and string */
static struct {
	gchar *name;
	TpConnectionPresenceType type;
} presence_types[] = {
	{ "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE },
	{ "busy",      TP_CONNECTION_PRESENCE_TYPE_BUSY },
	{ "away",      TP_CONNECTION_PRESENCE_TYPE_AWAY },
	{ "ext_away",  TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY },
	{ "hidden",    TP_CONNECTION_PRESENCE_TYPE_HIDDEN },
	{ "offline",   TP_CONNECTION_PRESENCE_TYPE_OFFLINE },
	{ "unset",     TP_CONNECTION_PRESENCE_TYPE_UNSET },
	{ "unknown",   TP_CONNECTION_PRESENCE_TYPE_UNKNOWN },
	{ "error",     TP_CONNECTION_PRESENCE_TYPE_ERROR },
	/* alternative names */
	{ "dnd",      TP_CONNECTION_PRESENCE_TYPE_BUSY },
	{ "brb",      TP_CONNECTION_PRESENCE_TYPE_AWAY },
	{ "xa",       TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY },
	{ NULL, },
};



void
empathy_init (void)
{
	static gboolean initialized = FALSE;

	if (initialized)
		return;

	g_type_init ();

	/* Setup gettext */
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	/* Setup debug output for empathy and telepathy-glib */
	if (g_getenv ("EMPATHY_TIMING") != NULL) {
		g_log_set_default_handler (tp_debug_timestamped_log_handler, NULL);
	}
	empathy_debug_set_flags (g_getenv ("EMPATHY_DEBUG"));
	tp_debug_divert_messages (g_getenv ("EMPATHY_LOGFILE"));

	emp_cli_init ();

	initialized = TRUE;
}

gchar *
empathy_substring (const gchar *str,
		  gint         start,
		  gint         end)
{
	return g_strndup (str + start, end - start);
}

gint
empathy_strcasecmp (const gchar *s1,
		   const gchar *s2)
{
	return empathy_strncasecmp (s1, s2, -1);
}

gint
empathy_strncasecmp (const gchar *s1,
		    const gchar *s2,
		    gsize        n)
{
	gchar *u1, *u2;
	gint   ret_val;

	u1 = g_utf8_casefold (s1, n);
	u2 = g_utf8_casefold (s2, n);

	ret_val = g_utf8_collate (u1, u2);
	g_free (u1);
	g_free (u2);

	return ret_val;
}

gboolean
empathy_xml_validate (xmlDoc      *doc,
		     const gchar *dtd_filename)
{
	gchar        *path;
	xmlChar      *escaped;
	xmlValidCtxt  cvp;
	xmlDtd       *dtd;
	gboolean      ret;

	path = g_build_filename (g_getenv ("EMPATHY_SRCDIR"), "libempathy",
				 dtd_filename, NULL);
	if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
		g_free (path);
		path = g_build_filename (DATADIR, "empathy", dtd_filename, NULL);
	}
	DEBUG ("Loading dtd file %s", path);

	/* The list of valid chars is taken from libxml. */
	escaped = xmlURIEscapeStr ((const xmlChar *) path,
		(const xmlChar *)":@&=+$,/?;");
	g_free (path);

	memset (&cvp, 0, sizeof (cvp));
	dtd = xmlParseDTD (NULL, escaped);
	ret = xmlValidateDtd (&cvp, doc, dtd);

	xmlFree (escaped);
	xmlFreeDtd (dtd);

	return ret;
}

xmlNodePtr
empathy_xml_node_get_child (xmlNodePtr   node,
			   const gchar *child_name)
{
	xmlNodePtr l;

        g_return_val_if_fail (node != NULL, NULL);
        g_return_val_if_fail (child_name != NULL, NULL);

	for (l = node->children; l; l = l->next) {
		if (l->name && strcmp ((const gchar *) l->name, child_name) == 0) {
			return l;
		}
	}

	return NULL;
}

xmlChar *
empathy_xml_node_get_child_content (xmlNodePtr   node,
				   const gchar *child_name)
{
	xmlNodePtr l;

        g_return_val_if_fail (node != NULL, NULL);
        g_return_val_if_fail (child_name != NULL, NULL);

	l = empathy_xml_node_get_child (node, child_name);
	if (l) {
		return xmlNodeGetContent (l);
	}

	return NULL;
}

xmlNodePtr
empathy_xml_node_find_child_prop_value (xmlNodePtr   node,
				       const gchar *prop_name,
				       const gchar *prop_value)
{
	xmlNodePtr l;
	xmlNodePtr found = NULL;

        g_return_val_if_fail (node != NULL, NULL);
        g_return_val_if_fail (prop_name != NULL, NULL);
        g_return_val_if_fail (prop_value != NULL, NULL);

	for (l = node->children; l && !found; l = l->next) {
		xmlChar *prop;

		if (!xmlHasProp (l, (const xmlChar *) prop_name)) {
			continue;
		}

		prop = xmlGetProp (l, (const xmlChar *) prop_name);
		if (prop && strcmp ((const gchar *) prop, prop_value) == 0) {
			found = l;
		}

		xmlFree (prop);
	}

	return found;
}

const gchar *
empathy_presence_get_default_message (TpConnectionPresenceType presence)
{
	switch (presence) {
	case TP_CONNECTION_PRESENCE_TYPE_AVAILABLE:
		return _("Available");
	case TP_CONNECTION_PRESENCE_TYPE_BUSY:
		return _("Busy");
	case TP_CONNECTION_PRESENCE_TYPE_AWAY:
	case TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY:
		return _("Away");
	case TP_CONNECTION_PRESENCE_TYPE_HIDDEN:
		return _("Invisible");
	case TP_CONNECTION_PRESENCE_TYPE_OFFLINE:
		return _("Offline");
	case TP_CONNECTION_PRESENCE_TYPE_UNSET:
	case TP_CONNECTION_PRESENCE_TYPE_UNKNOWN:
	case TP_CONNECTION_PRESENCE_TYPE_ERROR:
		return NULL;
	}

	return NULL;
}

const gchar *
empathy_presence_to_str (TpConnectionPresenceType presence)
{
	int i;

	for (i = 0 ; presence_types[i].name != NULL; i++)
		if (presence == presence_types[i].type)
			return presence_types[i].name;

	return NULL;
}

TpConnectionPresenceType
empathy_presence_from_str (const gchar *str)
{
	int i;

	for (i = 0 ; presence_types[i].name != NULL; i++)
		if (!tp_strdiff (str, presence_types[i].name))
			return presence_types[i].type;

	return TP_CONNECTION_PRESENCE_TYPE_UNSET;
}

const gchar *
empathy_status_reason_get_default_message (TpConnectionStatusReason reason)
{
	switch (reason) {
	case TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED:
		return _("No reason specified");
	case TP_CONNECTION_STATUS_REASON_REQUESTED:
		return _("Status is set to offline");
	case TP_CONNECTION_STATUS_REASON_NETWORK_ERROR:
		return _("Network error");
	case TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED:
		return _("Authentication failed");
	case TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR:
		return _("Encryption error");
	case TP_CONNECTION_STATUS_REASON_NAME_IN_USE:
		return _("Name in use");
	case TP_CONNECTION_STATUS_REASON_CERT_NOT_PROVIDED:
		return _("Certificate not provided");
	case TP_CONNECTION_STATUS_REASON_CERT_UNTRUSTED:
		return _("Certificate untrusted");
	case TP_CONNECTION_STATUS_REASON_CERT_EXPIRED:
		return _("Certificate expired");
	case TP_CONNECTION_STATUS_REASON_CERT_NOT_ACTIVATED:
		return _("Certificate not activated");
	case TP_CONNECTION_STATUS_REASON_CERT_HOSTNAME_MISMATCH:
		return _("Certificate hostname mismatch");
	case TP_CONNECTION_STATUS_REASON_CERT_FINGERPRINT_MISMATCH:
		return _("Certificate fingerprint mismatch");
	case TP_CONNECTION_STATUS_REASON_CERT_SELF_SIGNED:
		return _("Certificate self-signed");
	case TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR:
		return _("Certificate error");
	default:
		return _("Unknown reason");
	}
}

gchar *
empathy_file_lookup (const gchar *filename, const gchar *subdir)
{
	gchar *path;

	if (!subdir) {
		subdir = ".";
	}

	path = g_build_filename (g_getenv ("EMPATHY_SRCDIR"), subdir, filename, NULL);
	if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
		g_free (path);
		path = g_build_filename (DATADIR, "empathy", filename, NULL);
	}

	return path;
}

guint
empathy_proxy_hash (gconstpointer key)
{
	TpProxy      *proxy = TP_PROXY (key);
	TpProxyClass *proxy_class = TP_PROXY_GET_CLASS (key);

	g_return_val_if_fail (TP_IS_PROXY (proxy), 0);
	g_return_val_if_fail (proxy_class->must_have_unique_name, 0);

	return g_str_hash (proxy->object_path) ^ g_str_hash (proxy->bus_name);
}

gboolean
empathy_proxy_equal (gconstpointer a,
		     gconstpointer b)
{
	TpProxy *proxy_a = TP_PROXY (a);
	TpProxy *proxy_b = TP_PROXY (b);
	TpProxyClass *proxy_a_class = TP_PROXY_GET_CLASS (a);
	TpProxyClass *proxy_b_class = TP_PROXY_GET_CLASS (b);

	g_return_val_if_fail (TP_IS_PROXY (proxy_a), FALSE);
	g_return_val_if_fail (TP_IS_PROXY (proxy_b), FALSE);
	g_return_val_if_fail (proxy_a_class->must_have_unique_name, 0);
	g_return_val_if_fail (proxy_b_class->must_have_unique_name, 0);

	return g_str_equal (proxy_a->object_path, proxy_b->object_path) &&
	       g_str_equal (proxy_a->bus_name, proxy_b->bus_name);
}

gboolean
empathy_check_available_state (void)
{
	TpConnectionPresenceType presence;
	EmpathyIdle *idle;

	idle = empathy_idle_dup_singleton ();
	presence = empathy_idle_get_state (idle);
	g_object_unref (idle);

	if (presence != TP_CONNECTION_PRESENCE_TYPE_AVAILABLE &&
		presence != TP_CONNECTION_PRESENCE_TYPE_UNSET) {
		return FALSE;
	}

	return TRUE;
}

gint
empathy_uint_compare (gconstpointer a,
		      gconstpointer b)
{
	return *(guint *) a - *(guint *) b;
}

gchar *
empathy_protocol_icon_name (const gchar *protocol)
{
  if (!tp_strdiff (protocol, "yahoojp"))
    /* Yahoo Japan uses the same icon as Yahoo */
    protocol = "yahoo";
  else if (!tp_strdiff (protocol, "simple"))
    /* SIMPLE uses the same icon as SIP */
    protocol = "sip";
  else if (!tp_strdiff (protocol, "sms"))
    return g_strdup ("phone");

  return g_strdup_printf ("im-%s", protocol);
}

GType
empathy_type_dbus_ao (void)
{
  static GType t = 0;

  if (G_UNLIKELY (t == 0))
     t = dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH);

  return t;
}

const char *
empathy_protocol_name_to_display_name (const gchar *proto_name)
{
  int i;
  static struct {
    const gchar *proto;
    const gchar *display;
    gboolean translated;
  } names[] = {
    { "jabber", "Jabber", FALSE },
    { "gtalk", "Google Talk", FALSE },
    { "msn", "MSN", FALSE, },
    { "local-xmpp", N_("People Nearby"), TRUE },
    { "irc", "IRC", FALSE },
    { "icq", "ICQ", FALSE },
    { "aim", "AIM", FALSE },
    { "yahoo", "Yahoo!", FALSE },
    { "yahoojp", N_("Yahoo! Japan"), TRUE },
    { "facebook", N_("Facebook Chat"), TRUE },
    { "groupwise", "GroupWise", FALSE },
    { "sip", "SIP", FALSE },
    { NULL, NULL }
  };

  for (i = 0; names[i].proto != NULL; i++)
    {
      if (!tp_strdiff (proto_name, names[i].proto))
        {
          if (names[i].translated)
            return _(names[i].display);
          else
            return names[i].display;
        }
    }

  return NULL;
}

typedef struct {
    GObject *instance;
    GObject *user_data;
    gulong handler_id;
} WeakHandlerCtx;

static WeakHandlerCtx *
whc_new (GObject *instance,
         GObject *user_data)
{
  WeakHandlerCtx *ctx = g_slice_new0 (WeakHandlerCtx);

  ctx->instance = instance;
  ctx->user_data = user_data;

  return ctx;
}

static void
whc_free (WeakHandlerCtx *ctx)
{
  g_slice_free (WeakHandlerCtx, ctx);
}

static void user_data_destroyed_cb (gpointer, GObject *);

static void
instance_destroyed_cb (gpointer ctx_,
                       GObject *where_the_instance_was)
{
  WeakHandlerCtx *ctx = ctx_;

  DEBUG ("instance for %p destroyed; cleaning up", ctx);

  /* No need to disconnect the signal here, the instance has gone away. */
  g_object_weak_unref (ctx->user_data, user_data_destroyed_cb, ctx);
  whc_free (ctx);
}

static void
user_data_destroyed_cb (gpointer ctx_,
                        GObject *where_the_user_data_was)
{
  WeakHandlerCtx *ctx = ctx_;

  DEBUG ("user_data for %p destroyed; disconnecting", ctx);

  g_signal_handler_disconnect (ctx->instance, ctx->handler_id);
  g_object_weak_unref (ctx->instance, instance_destroyed_cb, ctx);
  whc_free (ctx);
}

/* This function is copied from telepathy-gabble: util.c */
/**
 * empathy_signal_connect_weak:
 * @instance: the instance to connect to.
 * @detailed_signal: a string of the form "signal-name::detail".
 * @c_handler: the GCallback to connect.
 * @user_data: an object to pass as data to c_handler calls.
 *
 * Connects a #GCallback function to a signal for a particular object, as if
 * with g_signal_connect(). Additionally, arranges for the signal handler to be
 * disconnected if @user_data is destroyed.
 *
 * This is intended to be a convenient way for objects to use themselves as
 * user_data for callbacks without having to explicitly disconnect all the
 * handlers in their finalizers.
 */
void
empathy_signal_connect_weak (gpointer instance,
    const gchar *detailed_signal,
    GCallback c_handler,
    GObject *user_data)
{
  GObject *instance_obj = G_OBJECT (instance);
  WeakHandlerCtx *ctx = whc_new (instance_obj, user_data);

  DEBUG ("connecting to %p:%s with context %p", instance, detailed_signal, ctx);

  ctx->handler_id = g_signal_connect (instance, detailed_signal, c_handler,
      user_data);

  g_object_weak_ref (instance_obj, instance_destroyed_cb, ctx);
  g_object_weak_ref (user_data, user_data_destroyed_cb, ctx);
}

/* Note: this function depends on the account manager having its core feature
 * prepared. */
TpAccount *
empathy_get_account_for_connection (TpConnection *connection)
{
  TpAccountManager *manager;
  TpAccount *account = NULL;
  GList *accounts, *l;

  manager = tp_account_manager_dup ();

  accounts = tp_account_manager_get_valid_accounts (manager);

  for (l = accounts; l != NULL; l = l->next)
    {
      TpAccount *a = l->data;

      if (tp_account_get_connection (a) == connection)
        {
          account = a;
          break;
        }
    }

  g_list_free (accounts);
  g_object_unref (manager);

  return account;
}

gboolean
empathy_account_manager_get_accounts_connected (gboolean *connecting)
{
  TpAccountManager *manager;
  GList *accounts, *l;
  gboolean out_connecting = FALSE;
  gboolean out_connected = FALSE;

  manager = tp_account_manager_dup ();

  if (G_UNLIKELY (!tp_account_manager_is_prepared (manager,
          TP_ACCOUNT_MANAGER_FEATURE_CORE)))
    g_critical (G_STRLOC ": %s called before AccountManager ready", G_STRFUNC);

  accounts = tp_account_manager_get_valid_accounts (manager);

  for (l = accounts; l != NULL; l = l->next)
    {
      TpConnectionStatus s = tp_account_get_connection_status (
          TP_ACCOUNT (l->data), NULL);

      if (s == TP_CONNECTION_STATUS_CONNECTING)
        out_connecting = TRUE;
      else if (s == TP_CONNECTION_STATUS_CONNECTED)
        out_connected = TRUE;

      if (out_connecting && out_connected)
        break;
    }

  g_list_free (accounts);
  g_object_unref (manager);

  if (connecting != NULL)
    *connecting = out_connecting;

  return out_connected;
}

/* Change the RequestedPresence of a newly created account to ensure that it
 * is actually connected. */
void
empathy_connect_new_account (TpAccount *account,
    TpAccountManager *account_manager)
{
  TpConnectionPresenceType presence;
  gchar *status, *message;

  /* only force presence if presence was offline, unknown or unset */
  presence = tp_account_get_requested_presence (account, NULL, NULL);
  switch (presence)
    {
      case TP_CONNECTION_PRESENCE_TYPE_OFFLINE:
      case TP_CONNECTION_PRESENCE_TYPE_UNKNOWN:
      case TP_CONNECTION_PRESENCE_TYPE_UNSET:
        presence = tp_account_manager_get_most_available_presence (
            account_manager, &status, &message);

        if (presence == TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
          /* Global presence is offline; we force it so user doesn't have to
           * manually change the presence to connect his new account. */
          presence = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;

        tp_account_request_presence_async (account, presence,
            status, NULL, NULL, NULL);

        g_free (status);
        g_free (message);
        break;

       default:
        /* do nothing if the presence is not offline */
        break;
    }
}
