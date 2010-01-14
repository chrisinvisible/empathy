/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007-2009 Collabora Ltd.
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
 */

#include <config.h>

#include <string.h>

#include <telepathy-glib/util.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#if HAVE_GEOCLUE
#include <geoclue/geoclue-geocode.h>
#endif

#include <extensions/extensions.h>

#include "empathy-tp-contact-factory.h"
#include "empathy-utils.h"
#include "empathy-location.h"

#define DEBUG_FLAG EMPATHY_DEBUG_TP | EMPATHY_DEBUG_CONTACT
#include "empathy-debug.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyTpContactFactory)
typedef struct {
	TpConnection   *connection;
	GList          *contacts;

	gchar         **avatar_mime_types;
	guint           avatar_min_width;
	guint           avatar_min_height;
	guint           avatar_max_width;
	guint           avatar_max_height;
	guint           avatar_max_size;
	/* can_request_ft and can_request_st are meaningful only if the connection
	 * doesn't implement ContactCapabilities. If it's implemented, we use it to
	 * check if contacts support file transfer and stream tubes. */
	gboolean        can_request_ft;
	gboolean        can_request_st;
	/* TRUE if ContactCapabilities is implemented by the Connection */
	gboolean        contact_caps_supported;
} EmpathyTpContactFactoryPriv;

G_DEFINE_TYPE (EmpathyTpContactFactory, empathy_tp_contact_factory, G_TYPE_OBJECT);

enum {
	PROP_0,
	PROP_CONNECTION,

	PROP_MIME_TYPES,
	PROP_MIN_WIDTH,
	PROP_MIN_HEIGHT,
	PROP_MAX_WIDTH,
	PROP_MAX_HEIGHT,
	PROP_MAX_SIZE
};

static TpContactFeature contact_features[] = {
	TP_CONTACT_FEATURE_ALIAS,
	TP_CONTACT_FEATURE_PRESENCE,
};

static EmpathyContact *
tp_contact_factory_find_by_handle (EmpathyTpContactFactory *tp_factory,
				   guint                    handle)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);
	GList                       *l;

	for (l = priv->contacts; l; l = l->next) {
		if (empathy_contact_get_handle (l->data) == handle) {
			return l->data;
		}
	}

	return NULL;
}

static EmpathyContact *
tp_contact_factory_find_by_tp_contact (EmpathyTpContactFactory *tp_factory,
				       TpContact               *tp_contact)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);
	GList                       *l;

	for (l = priv->contacts; l; l = l->next) {
		if (empathy_contact_get_tp_contact (l->data) == tp_contact) {
			return l->data;
		}
	}

	return NULL;
}

static void
tp_contact_factory_weak_notify (gpointer data,
				GObject *where_the_object_was)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (data);

	DEBUG ("Remove finalized contact %p", where_the_object_was);

	priv->contacts = g_list_remove (priv->contacts, where_the_object_was);
}

static void
tp_contact_factory_set_aliases_cb (TpConnection *connection,
				   const GError *error,
				   gpointer      user_data,
				   GObject      *tp_factory)
{
	if (error) {
		DEBUG ("Error: %s", error->message);
	}
}

static void
tp_contact_factory_set_location_cb (TpConnection *tp_conn,
				    const GError *error,
				    gpointer user_data,
				    GObject *weak_object)
{
	if (error != NULL) {
		DEBUG ("Error setting location: %s", error->message);
	}
}

static void
tp_contact_factory_set_avatar_cb (TpConnection *connection,
				  const gchar  *token,
				  const GError *error,
				  gpointer      user_data,
				  GObject      *tp_factory)
{
	if (error) {
		DEBUG ("Error: %s", error->message);
	}
}

static void
tp_contact_factory_clear_avatar_cb (TpConnection *connection,
				    const GError *error,
				    gpointer      user_data,
				    GObject      *tp_factory)
{
	if (error) {
		DEBUG ("Error: %s", error->message);
	}
}

static void
tp_contact_factory_avatar_retrieved_cb (TpConnection *connection,
					guint         handle,
					const gchar  *token,
					const GArray *avatar_data,
					const gchar  *mime_type,
					gpointer      user_data,
					GObject      *tp_factory)
{
	EmpathyContact *contact;

	contact = tp_contact_factory_find_by_handle (EMPATHY_TP_CONTACT_FACTORY (tp_factory),
						     handle);
	if (!contact) {
		return;
	}

	DEBUG ("Avatar retrieved for contact %s (%d)",
		empathy_contact_get_id (contact),
		handle);

	empathy_contact_load_avatar_data (contact,
					  (guchar *) avatar_data->data,
					  avatar_data->len,
					  mime_type,
					  token);
}

static void
tp_contact_factory_request_avatars_cb (TpConnection *connection,
				       const GError *error,
				       gpointer      user_data,
				       GObject      *tp_factory)
{
	if (error) {
		DEBUG ("Error: %s", error->message);
	}
}

static gboolean
tp_contact_factory_avatar_maybe_update (EmpathyTpContactFactory *tp_factory,
					guint                    handle,
					const gchar             *token)
{
	EmpathyContact *contact;
	EmpathyAvatar  *avatar;

	contact = tp_contact_factory_find_by_handle (tp_factory, handle);
	if (!contact) {
		return TRUE;
	}

	/* Check if we have an avatar */
	if (EMP_STR_EMPTY (token)) {
		empathy_contact_set_avatar (contact, NULL);
		return TRUE;
	}

	/* Check if the avatar changed */
	avatar = empathy_contact_get_avatar (contact);
	if (avatar && !tp_strdiff (avatar->token, token)) {
		return TRUE;
	}

	/* The avatar changed, search the new one in the cache */
	if (empathy_contact_load_avatar_cache (contact, token)) {
		/* Got from cache, use it */
		return TRUE;
	}

	/* Avatar is not up-to-date, we have to request it. */
	return FALSE;
}

static void
tp_contact_factory_got_known_avatar_tokens (TpConnection *connection,
					    GHashTable   *tokens,
					    const GError *error,
					    gpointer      user_data,
					    GObject      *weak_object)
{
	EmpathyTpContactFactory *tp_factory = EMPATHY_TP_CONTACT_FACTORY (weak_object);
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);
	GArray *handles;
	GHashTableIter iter;
	gpointer key, value;

	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	handles = g_array_new (FALSE, FALSE, sizeof (guint));

	g_hash_table_iter_init (&iter, tokens);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		guint handle = GPOINTER_TO_UINT (key);
		const gchar *token = value;

		if (!tp_contact_factory_avatar_maybe_update (tp_factory,
							     handle, token)) {
			g_array_append_val (handles, handle);
		}
	}

	DEBUG ("Got %d tokens, need to request %d avatars",
		g_hash_table_size (tokens), handles->len);

	/* Request needed avatars */
	if (handles->len > 0) {
		tp_cli_connection_interface_avatars_call_request_avatars (priv->connection,
									  -1,
									  handles,
									  tp_contact_factory_request_avatars_cb,
									  NULL, NULL,
									  G_OBJECT (tp_factory));
	}

	g_array_free (handles, TRUE);
}

static void
tp_contact_factory_avatar_updated_cb (TpConnection *connection,
				      guint         handle,
				      const gchar  *new_token,
				      gpointer      user_data,
				      GObject      *tp_factory)
{
	GArray *handles;

	if (tp_contact_factory_avatar_maybe_update (EMPATHY_TP_CONTACT_FACTORY (tp_factory),
						    handle, new_token)) {
		/* Avatar was cached, nothing to do */
		return;
	}

	DEBUG ("Need to request avatar for token %s", new_token);

	handles = g_array_new (FALSE, FALSE, sizeof (guint));
	g_array_append_val (handles, handle);

	tp_cli_connection_interface_avatars_call_request_avatars (connection,
								  -1,
								  handles,
								  tp_contact_factory_request_avatars_cb,
								  NULL, NULL,
								  tp_factory);
	g_array_free (handles, TRUE);
}

static void
tp_contact_factory_update_capabilities (EmpathyTpContactFactory *tp_factory,
					guint                    handle,
					const gchar             *channel_type,
					guint                    generic,
					guint                    specific)
{
	EmpathyContact      *contact;
	EmpathyCapabilities  capabilities;

	contact = tp_contact_factory_find_by_handle (tp_factory, handle);
	if (!contact) {
		return;
	}

	capabilities = empathy_contact_get_capabilities (contact);
	capabilities &= ~EMPATHY_CAPABILITIES_UNKNOWN;

	if (strcmp (channel_type, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA) == 0) {
		capabilities &= ~EMPATHY_CAPABILITIES_AUDIO;
		capabilities &= ~EMPATHY_CAPABILITIES_VIDEO;
		if (specific & TP_CHANNEL_MEDIA_CAPABILITY_AUDIO) {
			capabilities |= EMPATHY_CAPABILITIES_AUDIO;
		}
		if (specific & TP_CHANNEL_MEDIA_CAPABILITY_VIDEO) {
			capabilities |= EMPATHY_CAPABILITIES_VIDEO;
		}
	}

	DEBUG ("Changing capabilities for contact %s (%d) to %d",
		empathy_contact_get_id (contact),
		empathy_contact_get_handle (contact),
		capabilities);

	empathy_contact_set_capabilities (contact, capabilities);
}

static void
tp_contact_factory_got_capabilities (TpConnection    *connection,
				     const GPtrArray *capabilities,
				     const GError    *error,
				     gpointer         user_data,
				     GObject         *weak_object)
{
	EmpathyTpContactFactory *tp_factory;
	guint i;

	tp_factory = EMPATHY_TP_CONTACT_FACTORY (weak_object);

	if (error) {
		DEBUG ("Error: %s", error->message);
		/* FIXME Should set the capabilities of the contacts for which this request
		 * originated to NONE */
		return;
	}

	for (i = 0; i < capabilities->len; i++)	{
		GValueArray *values;
		guint        handle;
		const gchar *channel_type;
		guint        generic;
		guint        specific;

		values = g_ptr_array_index (capabilities, i);
		handle = g_value_get_uint (g_value_array_get_nth (values, 0));
		channel_type = g_value_get_string (g_value_array_get_nth (values, 1));
		generic = g_value_get_uint (g_value_array_get_nth (values, 2));
		specific = g_value_get_uint (g_value_array_get_nth (values, 3));

		tp_contact_factory_update_capabilities (tp_factory,
							handle,
							channel_type,
							generic,
							specific);
	}
}

#if HAVE_GEOCLUE
#define GEOCODE_SERVICE "org.freedesktop.Geoclue.Providers.Yahoo"
#define GEOCODE_PATH "/org/freedesktop/Geoclue/Providers/Yahoo"

/* This callback is called by geoclue when it found a position
 * for the given address.  A position is necessary for a contact
 * to show up on the map
 */
static void
geocode_cb (GeoclueGeocode *geocode,
	    GeocluePositionFields fields,
	    double latitude,
	    double longitude,
	    double altitude,
	    GeoclueAccuracy *accuracy,
	    GError *error,
	    gpointer contact)
{
	GValue *new_value;
	GHashTable *location;

	location = empathy_contact_get_location (EMPATHY_CONTACT (contact));

	if (error != NULL) {
		DEBUG ("Error geocoding location : %s", error->message);
		g_object_unref (geocode);
		g_object_unref (contact);
		return;
	}

	if (fields & GEOCLUE_POSITION_FIELDS_LATITUDE) {
		new_value = tp_g_value_slice_new_double (latitude);
		g_hash_table_replace (location, g_strdup (EMPATHY_LOCATION_LAT),
			new_value);
		DEBUG ("\t - Latitude: %f", latitude);
	}
	if (fields & GEOCLUE_POSITION_FIELDS_LONGITUDE) {
		new_value = tp_g_value_slice_new_double (longitude);
		g_hash_table_replace (location, g_strdup (EMPATHY_LOCATION_LON),
			new_value);
		DEBUG ("\t - Longitude: %f", longitude);
	}
	if (fields & GEOCLUE_POSITION_FIELDS_ALTITUDE) {
		new_value = tp_g_value_slice_new_double (altitude);
		g_hash_table_replace (location, g_strdup (EMPATHY_LOCATION_ALT),
			new_value);
		DEBUG ("\t - Altitude: %f", altitude);
	}

	/* Don't change the accuracy as we used an address to get this position */
	g_object_notify (contact, "location");
	g_object_unref (geocode);
	g_object_unref (contact);
}
#endif

#if HAVE_GEOCLUE
static gchar *
get_dup_string (GHashTable *location,
    gchar *key)
{
  GValue *value;

  value = g_hash_table_lookup (location, key);
  if (value != NULL)
    return g_value_dup_string (value);

  return NULL;
}
#endif

static void
tp_contact_factory_geocode (EmpathyContact *contact)
{
#if HAVE_GEOCLUE
	static GeoclueGeocode *geocode;
	gchar *str;
	GHashTable *address;
	GValue* value;
	GHashTable *location;

	location = empathy_contact_get_location (contact);
	if (location == NULL)
		return;

	value = g_hash_table_lookup (location, EMPATHY_LOCATION_LAT);
	if (value != NULL)
		return;

	if (geocode == NULL) {
		geocode = geoclue_geocode_new (GEOCODE_SERVICE, GEOCODE_PATH);
		g_object_add_weak_pointer (G_OBJECT (geocode), (gpointer *) &geocode);
	}
	else
		g_object_ref (geocode);

	address = geoclue_address_details_new ();

	str = get_dup_string (location, EMPATHY_LOCATION_COUNTRY_CODE);
	if (str != NULL) {
		g_hash_table_insert (address,
			g_strdup (GEOCLUE_ADDRESS_KEY_COUNTRYCODE), str);
		DEBUG ("\t - countrycode: %s", str);
	}

	str = get_dup_string (location, EMPATHY_LOCATION_COUNTRY);
	if (str != NULL) {
		g_hash_table_insert (address,
			g_strdup (GEOCLUE_ADDRESS_KEY_COUNTRY), str);
		DEBUG ("\t - country: %s", str);
	}

	str = get_dup_string (location, EMPATHY_LOCATION_POSTAL_CODE);
	if (str != NULL) {
		g_hash_table_insert (address,
			g_strdup (GEOCLUE_ADDRESS_KEY_POSTALCODE), str);
		DEBUG ("\t - postalcode: %s", str);
	}

	str = get_dup_string (location, EMPATHY_LOCATION_REGION);
	if (str != NULL) {
		g_hash_table_insert (address,
			g_strdup (GEOCLUE_ADDRESS_KEY_REGION), str);
		DEBUG ("\t - region: %s", str);
	}

	str = get_dup_string (location, EMPATHY_LOCATION_LOCALITY);
	if (str != NULL) {
		g_hash_table_insert (address,
			g_strdup (GEOCLUE_ADDRESS_KEY_LOCALITY), str);
		DEBUG ("\t - locality: %s", str);
	}

	str = get_dup_string (location, EMPATHY_LOCATION_STREET);
	if (str != NULL) {
		g_hash_table_insert (address,
			g_strdup (GEOCLUE_ADDRESS_KEY_STREET), str);
		DEBUG ("\t - street: %s", str);
	}

	g_object_ref (contact);
	geoclue_geocode_address_to_position_async (geocode, address,
		geocode_cb, contact);

	g_hash_table_unref (address);
#endif
}

static void
tp_contact_factory_update_location (EmpathyTpContactFactory *tp_factory,
				    guint handle,
				    GHashTable *location)
{
	EmpathyContact *contact;
	GHashTable     *new_location;

	contact = tp_contact_factory_find_by_handle (tp_factory, handle);

	if (contact == NULL)
		return;

	new_location = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free, (GDestroyNotify) tp_g_value_slice_free);
	tp_g_hash_table_update (new_location, location, (GBoxedCopyFunc) g_strdup,
		(GBoxedCopyFunc) tp_g_value_slice_dup);
	empathy_contact_set_location (contact, new_location);
	g_hash_table_unref (new_location);

	tp_contact_factory_geocode (contact);
}

static void
tp_contact_factory_got_locations (TpConnection                 *tp_conn,
				  GHashTable              *locations,
				  const GError            *error,
				  gpointer                 user_data,
				  GObject                 *weak_object)
{
	GHashTableIter iter;
	gpointer key, value;
	EmpathyTpContactFactory *tp_factory;

	tp_factory = EMPATHY_TP_CONTACT_FACTORY (user_data);
	if (error != NULL) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	g_hash_table_iter_init (&iter, locations);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		guint           handle = GPOINTER_TO_INT (key);
		GHashTable     *location = value;

		tp_contact_factory_update_location (tp_factory, handle, location);
	}
}

static void
tp_contact_factory_capabilities_changed_cb (TpConnection    *connection,
					    const GPtrArray *capabilities,
					    gpointer         user_data,
					    GObject         *weak_object)
{
	EmpathyTpContactFactory *tp_factory = EMPATHY_TP_CONTACT_FACTORY (weak_object);
	guint                    i;

	for (i = 0; i < capabilities->len; i++)	{
		GValueArray *values;
		guint        handle;
		const gchar *channel_type;
		guint        generic;
		guint        specific;

		values = g_ptr_array_index (capabilities, i);
		handle = g_value_get_uint (g_value_array_get_nth (values, 0));
		channel_type = g_value_get_string (g_value_array_get_nth (values, 1));
		generic = g_value_get_uint (g_value_array_get_nth (values, 3));
		specific = g_value_get_uint (g_value_array_get_nth (values, 5));

		tp_contact_factory_update_capabilities (tp_factory,
							handle,
							channel_type,
							generic,
							specific);
	}
}

static void
tp_contact_factory_location_updated_cb (TpConnection      *tp_conn,
					guint         handle,
					GHashTable   *location,
					gpointer      user_data,
					GObject      *weak_object)
{
	EmpathyTpContactFactory *tp_factory = EMPATHY_TP_CONTACT_FACTORY (weak_object);
	tp_contact_factory_update_location (tp_factory, handle, location);
}

static EmpathyCapabilities
channel_classes_to_capabilities (GPtrArray *classes,
				 gboolean audio_video)
{
	EmpathyCapabilities capabilities = 0;
	guint i;

	for (i = 0; i < classes->len; i++) {
		GValueArray *class_struct;
		GHashTable *fixed_prop;
		GStrv allowed_prop;
		TpHandleType handle_type;
		const gchar *chan_type;

		class_struct = g_ptr_array_index (classes, i);
		fixed_prop = g_value_get_boxed (g_value_array_get_nth (class_struct, 0));
		allowed_prop = g_value_get_boxed (g_value_array_get_nth (class_struct, 1));

		handle_type = tp_asv_get_uint32 (fixed_prop,
			TP_IFACE_CHANNEL ".TargetHandleType", NULL);
		if (handle_type != TP_HANDLE_TYPE_CONTACT)
			continue;

		chan_type = tp_asv_get_string (fixed_prop,
			TP_IFACE_CHANNEL ".ChannelType");

		if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER)) {
			capabilities |= EMPATHY_CAPABILITIES_FT;
		}

		else if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE)) {
			capabilities |= EMPATHY_CAPABILITIES_STREAM_TUBE;
		}
		else if (audio_video && !tp_strdiff (chan_type,
			TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA)) {
			guint j;

			for (j = 0; allowed_prop[j] != NULL; j++) {
				if (!tp_strdiff (allowed_prop[j],
						TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialAudio"))
					capabilities |= EMPATHY_CAPABILITIES_AUDIO;
				else if (!tp_strdiff (allowed_prop[j],
						TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialVideo"))
					capabilities |= EMPATHY_CAPABILITIES_VIDEO;
			}
		}
	}

	return capabilities;
}

static void
get_requestable_channel_classes_cb (TpProxy *connection,
				    const GValue *value,
				    const GError *error,
				    gpointer user_data,
				    GObject *weak_object)
{
	EmpathyTpContactFactory     *self = EMPATHY_TP_CONTACT_FACTORY (weak_object);
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (self);
	GPtrArray                   *classes;
	GList                       *l;
	EmpathyCapabilities         capabilities;

	if (error != NULL) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	classes = g_value_get_boxed (value);

	DEBUG ("ContactCapabilities is not implemented; use RCC");
	capabilities = channel_classes_to_capabilities (classes, FALSE);
	if ((capabilities & EMPATHY_CAPABILITIES_FT) != 0) {
		DEBUG ("Assume all contacts support FT as CM implements it");
		priv->can_request_ft = TRUE;
	}

	if ((capabilities & EMPATHY_CAPABILITIES_STREAM_TUBE) != 0) {
		DEBUG ("Assume all contacts support stream tubes as CM implements them");
		priv->can_request_st = TRUE;
	}

	if (!priv->can_request_ft && !priv->can_request_st)
		return ;

	/* Update the capabilities of all contacts */
	for (l = priv->contacts; l != NULL; l = g_list_next (l)) {
		EmpathyContact *contact = l->data;
		EmpathyCapabilities caps;

		caps = empathy_contact_get_capabilities (contact);

		/* ContactCapabilities is not supported; assume all contacts can do file
		 * transfer if it's implemented in the CM */
		if (priv->can_request_ft)
			caps |= EMPATHY_CAPABILITIES_FT;

		if (priv->can_request_st)
			caps |= EMPATHY_CAPABILITIES_STREAM_TUBE;

		empathy_contact_set_capabilities (contact, caps);
	}
}

static void
tp_contact_factory_got_avatar_requirements_cb (TpConnection *proxy,
					       const gchar **mime_types,
					       guint         min_width,
					       guint         min_height,
					       guint         max_width,
					       guint         max_height,
					       guint         max_size,
					       const GError *error,
					       gpointer      user_data,
					       GObject      *tp_factory)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);

	if (error) {
		DEBUG ("Failed to get avatar requirements: %s", error->message);
		/* We'll just leave avatar_mime_types as NULL; the
		 * avatar-setting code can use this as a signal that you can't
		 * set avatars.
		 */
	} else {
		priv->avatar_mime_types = g_strdupv ((gchar **) mime_types);
		priv->avatar_min_width = min_width;
		priv->avatar_min_height = min_height;
		priv->avatar_max_width = max_width;
		priv->avatar_max_height = max_height;
		priv->avatar_max_size = max_size;
	}
}

static void
update_contact_capabilities (EmpathyTpContactFactory *self,
			     GHashTable *caps)
{
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, caps);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		TpHandle handle = GPOINTER_TO_UINT (key);
		GPtrArray *classes = value;
		EmpathyContact *contact;
		EmpathyCapabilities  capabilities;

		contact = tp_contact_factory_find_by_handle (self, handle);
		if (contact == NULL)
			continue;

		capabilities = empathy_contact_get_capabilities (contact);
		capabilities &= ~EMPATHY_CAPABILITIES_UNKNOWN;

		capabilities |= channel_classes_to_capabilities (classes, TRUE);

		DEBUG ("Changing capabilities for contact %s (%d) to %d",
			empathy_contact_get_id (contact),
			empathy_contact_get_handle (contact),
			capabilities);

		empathy_contact_set_capabilities (contact, capabilities);
	}
}

static void
tp_contact_factory_got_contact_capabilities (TpConnection *connection,
					     GHashTable *caps,
					     const GError *error,
					     gpointer user_data,
					     GObject *weak_object)
{
	EmpathyTpContactFactory *self = EMPATHY_TP_CONTACT_FACTORY (weak_object);

	if (error != NULL) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	update_contact_capabilities (self, caps);
}

static void
tp_contact_factory_add_contact (EmpathyTpContactFactory *tp_factory,
				EmpathyContact          *contact)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);
	TpHandle self_handle;
	TpHandle handle;
	GArray handles = {(gchar *) &handle, 1};
	EmpathyCapabilities caps;

	/* Keep a weak ref to that contact */
	g_object_weak_ref (G_OBJECT (contact),
			   tp_contact_factory_weak_notify,
			   tp_factory);
	priv->contacts = g_list_prepend (priv->contacts, contact);

	/* The contact keeps a ref to its factory */
	g_object_set_data_full (G_OBJECT (contact), "empathy-factory",
				g_object_ref (tp_factory),
				g_object_unref);

	caps = empathy_contact_get_capabilities (contact);

	/* Set the FT capability */
	if (!priv->contact_caps_supported) {
		/* ContactCapabilities is not supported; assume all contacts can do file
		 * transfer if it's implemented in the CM */
		if (priv->can_request_ft) {
			caps |= EMPATHY_CAPABILITIES_FT;
		}

		/* Set the Stream Tube capability */
		if (priv->can_request_st) {
			caps |= EMPATHY_CAPABILITIES_STREAM_TUBE;
		}
	}

	empathy_contact_set_capabilities (contact, caps);

	/* Set is-user property. Note that it could still be the handle is
	 * different from the connection's self handle, in the case the handle
	 * comes from a group interface. */
	self_handle = tp_connection_get_self_handle (priv->connection);
	handle = empathy_contact_get_handle (contact);
	empathy_contact_set_is_user (contact, self_handle == handle);

	/* FIXME: This should be done by TpContact */
	if (tp_proxy_has_interface_by_id (priv->connection,
			TP_IFACE_QUARK_CONNECTION_INTERFACE_AVATARS)) {
		tp_cli_connection_interface_avatars_call_get_known_avatar_tokens (
			priv->connection, -1, &handles,
			tp_contact_factory_got_known_avatar_tokens, NULL, NULL,
			G_OBJECT (tp_factory));
	}

	if (priv->contact_caps_supported) {
		tp_cli_connection_interface_contact_capabilities_call_get_contact_capabilities (
			priv->connection, -1, &handles,
			tp_contact_factory_got_contact_capabilities, NULL, NULL,
			G_OBJECT (tp_factory));
	}
	else if (tp_proxy_has_interface_by_id (priv->connection,
			TP_IFACE_QUARK_CONNECTION_INTERFACE_CAPABILITIES)) {
		tp_cli_connection_interface_capabilities_call_get_capabilities (
			priv->connection, -1, &handles,
			tp_contact_factory_got_capabilities, NULL, NULL,
			G_OBJECT (tp_factory));
	}

	if (tp_proxy_has_interface_by_id (TP_PROXY (priv->connection),
		TP_IFACE_QUARK_CONNECTION_INTERFACE_LOCATION)) {
		tp_cli_connection_interface_location_call_get_locations (priv->connection,
									 -1,
									 &handles,
									 tp_contact_factory_got_locations,
									 tp_factory,
									 NULL,
									 NULL);
	}

	DEBUG ("Contact added: %s (%d)",
		empathy_contact_get_id (contact),
		empathy_contact_get_handle (contact));
}

typedef union {
	EmpathyTpContactFactoryContactsByIdCb ids_cb;
	EmpathyTpContactFactoryContactsByHandleCb handles_cb;
	EmpathyTpContactFactoryContactCb contact_cb;
} GetContactsCb;

typedef struct {
	EmpathyTpContactFactory *tp_factory;
	GetContactsCb callback;
	gpointer user_data;
	GDestroyNotify destroy;
} GetContactsData;

static void
get_contacts_data_free (gpointer user_data)
{
	GetContactsData *data = user_data;

	if (data->destroy) {
		data->destroy (data->user_data);
	}
	g_object_unref (data->tp_factory);

	g_slice_free (GetContactsData, data);
}

static EmpathyContact *
dup_contact_for_tp_contact (EmpathyTpContactFactory *tp_factory,
			    TpContact               *tp_contact)
{
	EmpathyContact *contact;

	contact = tp_contact_factory_find_by_tp_contact (tp_factory,
							 tp_contact);

	if (contact != NULL) {
		g_object_ref (contact);
	} else {
		contact = empathy_contact_new (tp_contact);
		tp_contact_factory_add_contact (tp_factory, contact);
	}

	return contact;
}

static EmpathyContact **
contacts_array_new (EmpathyTpContactFactory *tp_factory,
		    guint                    n_contacts,
		    TpContact * const *      contacts)
{
	EmpathyContact **ret;
	guint            i;

	ret = g_new0 (EmpathyContact *, n_contacts);
	for (i = 0; i < n_contacts; i++) {
		ret[i] = dup_contact_for_tp_contact (tp_factory, contacts[i]);
	}

	return ret;
}

static void
contacts_array_free (guint            n_contacts,
		     EmpathyContact **contacts)
{
	guint i;

	for (i = 0; i < n_contacts; i++) {
		g_object_unref (contacts[i]);
	}
	g_free (contacts);
}

static void
get_contacts_by_id_cb (TpConnection *connection,
		       guint n_contacts,
		       TpContact * const *contacts,
		       const gchar * const *requested_ids,
		       GHashTable *failed_id_errors,
		       const GError *error,
		       gpointer user_data,
		       GObject *weak_object)
{
	GetContactsData *data = user_data;
	EmpathyContact **empathy_contacts;

	empathy_contacts = contacts_array_new (data->tp_factory,
					       n_contacts, contacts);
	if (data->callback.ids_cb) {
		data->callback.ids_cb (data->tp_factory,
				       n_contacts, empathy_contacts,
				       requested_ids,
				       failed_id_errors,
				       error,
				       data->user_data, weak_object);
	}

	contacts_array_free (n_contacts, empathy_contacts);
}

/* The callback is NOT given a reference to the EmpathyContact objects */
void
empathy_tp_contact_factory_get_from_ids (EmpathyTpContactFactory *tp_factory,
					 guint                    n_ids,
					 const gchar * const     *ids,
					 EmpathyTpContactFactoryContactsByIdCb callback,
					 gpointer                 user_data,
					 GDestroyNotify           destroy,
					 GObject                 *weak_object)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);
	GetContactsData *data;

	g_return_if_fail (EMPATHY_IS_TP_CONTACT_FACTORY (tp_factory));
	g_return_if_fail (ids != NULL);

	data = g_slice_new (GetContactsData);
	data->callback.ids_cb = callback;
	data->user_data = user_data;
	data->destroy = destroy;
	data->tp_factory = g_object_ref (tp_factory);
	tp_connection_get_contacts_by_id (priv->connection,
					  n_ids, ids,
					  G_N_ELEMENTS (contact_features),
					  contact_features,
					  get_contacts_by_id_cb,
					  data,
					  (GDestroyNotify) get_contacts_data_free,
					  weak_object);
}

static void
get_contact_by_id_cb (TpConnection *connection,
		      guint n_contacts,
		      TpContact * const *contacts,
		      const gchar * const *requested_ids,
		      GHashTable *failed_id_errors,
		      const GError *error,
		      gpointer user_data,
		      GObject *weak_object)
{
	GetContactsData *data = user_data;
	EmpathyContact  *contact = NULL;

	if (n_contacts == 1) {
		contact = dup_contact_for_tp_contact (data->tp_factory,
						      contacts[0]);
	}
	else if (error == NULL) {
		GHashTableIter iter;
		gpointer       value;

		g_hash_table_iter_init (&iter, failed_id_errors);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			if (value) {
				error = value;
				break;
			}
		}
	}

	if (data->callback.contact_cb) {
		data->callback.contact_cb (data->tp_factory,
				           contact,
				           error,
				           data->user_data, weak_object);
	}

	if (contact != NULL)
		g_object_unref (contact);
}

/* The callback is NOT given a reference to the EmpathyContact objects */
void
empathy_tp_contact_factory_get_from_id (EmpathyTpContactFactory *tp_factory,
					const gchar             *id,
					EmpathyTpContactFactoryContactCb callback,
					gpointer                 user_data,
					GDestroyNotify           destroy,
					GObject                 *weak_object)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);
	GetContactsData *data;

	g_return_if_fail (EMPATHY_IS_TP_CONTACT_FACTORY (tp_factory));
	g_return_if_fail (id != NULL);

	data = g_slice_new (GetContactsData);
	data->callback.contact_cb = callback;
	data->user_data = user_data;
	data->destroy = destroy;
	data->tp_factory = g_object_ref (tp_factory);
	tp_connection_get_contacts_by_id (priv->connection,
					  1, &id,
					  G_N_ELEMENTS (contact_features),
					  contact_features,
					  get_contact_by_id_cb,
					  data,
					  (GDestroyNotify) get_contacts_data_free,
					  weak_object);
}

static void
get_contacts_by_handle_cb (TpConnection *connection,
			   guint n_contacts,
			   TpContact * const *contacts,
			   guint n_failed,
			   const TpHandle *failed,
			   const GError *error,
			   gpointer user_data,
			   GObject *weak_object)
{
	GetContactsData *data = user_data;
	EmpathyContact **empathy_contacts;

	empathy_contacts = contacts_array_new (data->tp_factory,
					       n_contacts, contacts);
	if (data->callback.handles_cb) {
		data->callback.handles_cb (data->tp_factory,
					   n_contacts, empathy_contacts,
					   n_failed, failed,
					   error,
					   data->user_data, weak_object);
	}

	contacts_array_free (n_contacts, empathy_contacts);
}

/* The callback is NOT given a reference to the EmpathyContact objects */
void
empathy_tp_contact_factory_get_from_handles (EmpathyTpContactFactory *tp_factory,
					     guint n_handles,
					     const TpHandle *handles,
					     EmpathyTpContactFactoryContactsByHandleCb callback,
					     gpointer                 user_data,
					     GDestroyNotify           destroy,
					     GObject                 *weak_object)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);
	GetContactsData *data;

	if (n_handles == 0) {
		callback (tp_factory, 0, NULL, 0, NULL, NULL, user_data, weak_object);
		return;
	}

	g_return_if_fail (EMPATHY_IS_TP_CONTACT_FACTORY (tp_factory));
	g_return_if_fail (handles != NULL);

	data = g_slice_new (GetContactsData);
	data->callback.handles_cb = callback;
	data->user_data = user_data;
	data->destroy = destroy;
	data->tp_factory = g_object_ref (tp_factory);
	tp_connection_get_contacts_by_handle (priv->connection,
					      n_handles, handles,
					      G_N_ELEMENTS (contact_features),
					      contact_features,
					      get_contacts_by_handle_cb,
					      data,
					      (GDestroyNotify) get_contacts_data_free,
					      weak_object);
}

/* The callback is NOT given a reference to the EmpathyContact objects */
static void
get_contact_by_handle_cb (TpConnection *connection,
			  guint n_contacts,
			  TpContact * const *contacts,
			  guint n_failed,
			  const TpHandle *failed,
			  const GError *error,
			  gpointer user_data,
			  GObject *weak_object)
{
	GetContactsData *data = user_data;
	EmpathyContact  *contact = NULL;
	GError *err = NULL;

	if (n_contacts == 1) {
		contact = dup_contact_for_tp_contact (data->tp_factory,
						      contacts[0]);
	}
	else {
		if (error == NULL) {
			/* tp-glib will provide an error only if the whole operation failed,
			 * but not if, for example, the handle was invalid. We create an error
			 * so the caller of empathy_tp_contact_factory_get_from_handle can
			 * rely on the error to check if the operation succeeded or not. */

			err = g_error_new_literal (TP_ERRORS, TP_ERROR_INVALID_HANDLE,
						      "handle is invalid");
		}
		else {
			err = g_error_copy (error);
		}
	}

	if (data->callback.contact_cb) {
		data->callback.contact_cb (data->tp_factory,
				           contact,
				           err,
				           data->user_data, weak_object);
	}

	g_clear_error (&err);
	if (contact != NULL)
		g_object_unref (contact);
}

void
empathy_tp_contact_factory_get_from_handle (EmpathyTpContactFactory *tp_factory,
					    TpHandle                 handle,
					    EmpathyTpContactFactoryContactCb callback,
					    gpointer                 user_data,
					    GDestroyNotify           destroy,
					    GObject                 *weak_object)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);
	GetContactsData *data;

	g_return_if_fail (EMPATHY_IS_TP_CONTACT_FACTORY (tp_factory));

	data = g_slice_new (GetContactsData);
	data->callback.contact_cb = callback;
	data->user_data = user_data;
	data->destroy = destroy;
	data->tp_factory = g_object_ref (tp_factory);
	tp_connection_get_contacts_by_handle (priv->connection,
					      1, &handle,
					      G_N_ELEMENTS (contact_features),
					      contact_features,
					      get_contact_by_handle_cb,
					      data,
					      (GDestroyNotify) get_contacts_data_free,
					      weak_object);
}

void
empathy_tp_contact_factory_set_alias (EmpathyTpContactFactory *tp_factory,
				      EmpathyContact          *contact,
				      const gchar             *alias)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);
	GHashTable                  *new_alias;
	guint                        handle;

	g_return_if_fail (EMPATHY_IS_TP_CONTACT_FACTORY (tp_factory));
	g_return_if_fail (EMPATHY_IS_CONTACT (contact));

	handle = empathy_contact_get_handle (contact);

	DEBUG ("Setting alias for contact %s (%d) to %s",
		empathy_contact_get_id (contact),
		handle, alias);

	new_alias = g_hash_table_new_full (g_direct_hash,
					   g_direct_equal,
					   NULL,
					   g_free);

	g_hash_table_insert (new_alias,
			     GUINT_TO_POINTER (handle),
			     g_strdup (alias));

	tp_cli_connection_interface_aliasing_call_set_aliases (priv->connection,
							       -1,
							       new_alias,
							       tp_contact_factory_set_aliases_cb,
							       NULL, NULL,
							       G_OBJECT (tp_factory));

	g_hash_table_destroy (new_alias);
}

void
empathy_tp_contact_factory_set_avatar (EmpathyTpContactFactory *tp_factory,
				       const gchar             *data,
				       gsize                    size,
				       const gchar             *mime_type)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);

	g_return_if_fail (EMPATHY_IS_TP_CONTACT_FACTORY (tp_factory));

	if (data && size > 0 && size < G_MAXUINT) {
		GArray avatar;

		avatar.data = (gchar *) data;
		avatar.len = size;

		DEBUG ("Setting avatar on connection %s",
			tp_proxy_get_object_path (TP_PROXY (priv->connection)));

		tp_cli_connection_interface_avatars_call_set_avatar (priv->connection,
								     -1,
								     &avatar,
								     mime_type,
								     tp_contact_factory_set_avatar_cb,
								     NULL, NULL,
								     G_OBJECT (tp_factory));
	} else {
		DEBUG ("Clearing avatar on connection %s",
			tp_proxy_get_object_path (TP_PROXY (priv->connection)));

		tp_cli_connection_interface_avatars_call_clear_avatar (priv->connection,
								       -1,
								       tp_contact_factory_clear_avatar_cb,
								       NULL, NULL,
								       G_OBJECT (tp_factory));
	}
}

void
empathy_tp_contact_factory_set_location (EmpathyTpContactFactory *tp_factory,
					 GHashTable              *location)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);

	g_return_if_fail (EMPATHY_IS_TP_CONTACT_FACTORY (tp_factory));

	DEBUG ("Setting location");

	tp_cli_connection_interface_location_call_set_location (priv->connection,
								 -1,
								 location,
								 tp_contact_factory_set_location_cb,
								 NULL, NULL,
								 G_OBJECT (tp_factory));
}

static void
tp_contact_factory_get_property (GObject    *object,
				 guint       param_id,
				 GValue     *value,
				 GParamSpec *pspec)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_CONNECTION:
		g_value_set_object (value, priv->connection);
		break;
	case PROP_MIME_TYPES:
		g_value_set_boxed (value, priv->avatar_mime_types);
		break;
	case PROP_MIN_WIDTH:
		g_value_set_uint (value, priv->avatar_min_width);
		break;
	case PROP_MIN_HEIGHT:
		g_value_set_uint (value, priv->avatar_min_height);
		break;
	case PROP_MAX_WIDTH:
		g_value_set_uint (value, priv->avatar_max_width);
		break;
	case PROP_MAX_HEIGHT:
		g_value_set_uint (value, priv->avatar_max_height);
		break;
	case PROP_MAX_SIZE:
		g_value_set_uint (value, priv->avatar_max_size);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
tp_contact_factory_set_property (GObject      *object,
				 guint         param_id,
				 const GValue *value,
				 GParamSpec   *pspec)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_CONNECTION:
		priv->connection = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
tp_contact_factory_finalize (GObject *object)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (object);
	GList                       *l;

	DEBUG ("Finalized: %p", object);

	for (l = priv->contacts; l; l = l->next) {
		g_object_weak_unref (G_OBJECT (l->data),
				     tp_contact_factory_weak_notify,
				     object);
	}

	g_list_free (priv->contacts);

	g_object_unref (priv->connection);

	g_strfreev (priv->avatar_mime_types);

	G_OBJECT_CLASS (empathy_tp_contact_factory_parent_class)->finalize (object);
}

static void
tp_contact_factory_contact_capabilities_changed_cb (TpConnection *connection,
						    GHashTable *caps,
						    gpointer user_data,
						    GObject *weak_object)
{
	EmpathyTpContactFactory *self = EMPATHY_TP_CONTACT_FACTORY (weak_object);

	update_contact_capabilities (self, caps);
}

static void
connection_ready_cb (TpConnection *connection,
				const GError *error,
				gpointer user_data)
{
	EmpathyTpContactFactory *tp_factory = EMPATHY_TP_CONTACT_FACTORY (user_data);
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);

	if (error != NULL)
		goto out;

	/* FIXME: This should be moved to TpContact */
	tp_cli_connection_interface_avatars_connect_to_avatar_updated (priv->connection,
								       tp_contact_factory_avatar_updated_cb,
								       NULL, NULL,
								       G_OBJECT (tp_factory),
								       NULL);
	tp_cli_connection_interface_avatars_connect_to_avatar_retrieved (priv->connection,
									 tp_contact_factory_avatar_retrieved_cb,
									 NULL, NULL,
									 G_OBJECT (tp_factory),
									 NULL);

	if (tp_proxy_has_interface_by_id (connection,
				TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_CAPABILITIES)) {
		priv->contact_caps_supported = TRUE;

		tp_cli_connection_interface_contact_capabilities_connect_to_contact_capabilities_changed (
			priv->connection, tp_contact_factory_contact_capabilities_changed_cb,
			NULL, NULL, G_OBJECT (tp_factory), NULL);
	}
	else {
		tp_cli_connection_interface_capabilities_connect_to_capabilities_changed (priv->connection,
											  tp_contact_factory_capabilities_changed_cb,
											  NULL, NULL,
											  G_OBJECT (tp_factory),
											  NULL);
	}

	tp_cli_connection_interface_location_connect_to_location_updated (priv->connection,
									   tp_contact_factory_location_updated_cb,
									   NULL, NULL,
									   G_OBJECT (tp_factory),
									   NULL);

	tp_cli_connection_interface_avatars_call_get_avatar_requirements (priv->connection,
									  -1,
									  tp_contact_factory_got_avatar_requirements_cb,
									  NULL, NULL,
									  G_OBJECT (tp_factory));

	if (!priv->contact_caps_supported) {
		tp_cli_dbus_properties_call_get (priv->connection, -1,
			TP_IFACE_CONNECTION_INTERFACE_REQUESTS,
			"RequestableChannelClasses",
			get_requestable_channel_classes_cb, NULL, NULL,
			G_OBJECT (tp_factory));
	}

out:
	g_object_unref (tp_factory);
}

static GObject *
tp_contact_factory_constructor (GType                  type,
				guint                  n_props,
				GObjectConstructParam *props)
{
	GObject *tp_factory;
	EmpathyTpContactFactoryPriv *priv;

	tp_factory = G_OBJECT_CLASS (empathy_tp_contact_factory_parent_class)->constructor (type, n_props, props);
	priv = GET_PRIV (tp_factory);

	/* Ensure to keep the self object alive while the call_when_ready is
	 * running */
	g_object_ref (tp_factory);
	tp_connection_call_when_ready (priv->connection, connection_ready_cb,
		tp_factory);

	return tp_factory;
}

static void
empathy_tp_contact_factory_class_init (EmpathyTpContactFactoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tp_contact_factory_finalize;
	object_class->constructor = tp_contact_factory_constructor;
	object_class->get_property = tp_contact_factory_get_property;
	object_class->set_property = tp_contact_factory_set_property;

	g_object_class_install_property (object_class,
					 PROP_CONNECTION,
					 g_param_spec_object ("connection",
							      "Factory's Connection",
							      "The connection associated with the factory",
							      TP_TYPE_CONNECTION,
							      G_PARAM_READWRITE |
							      G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_MIME_TYPES,
					 g_param_spec_boxed ("avatar-mime-types",
							     "Supported MIME types for avatars",
							     "Types of images that may be set as "
							     "avatars on this connection.",
							     G_TYPE_STRV,
							     G_PARAM_READABLE |
							     G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_MIN_WIDTH,
					 g_param_spec_uint ("avatar-min-width",
							    "Minimum width for avatars",
							    "Minimum width of avatar that may be set.",
							    0,
							    G_MAXUINT,
							    0,
							    G_PARAM_READABLE |
							    G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_MIN_HEIGHT,
					 g_param_spec_uint ("avatar-min-height",
							    "Minimum height for avatars",
							    "Minimum height of avatar that may be set.",
							    0,
							    G_MAXUINT,
							    0,
							    G_PARAM_READABLE |
							    G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_MAX_WIDTH,
					 g_param_spec_uint ("avatar-max-width",
							    "Maximum width for avatars",
							    "Maximum width of avatar that may be set "
							    "or 0 if there is no maximum.",
							    0,
							    G_MAXUINT,
							    0,
							    G_PARAM_READABLE |
							    G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_MAX_HEIGHT,
					 g_param_spec_uint ("avatar-max-height",
							    "Maximum height for avatars",
							    "Maximum height of avatar that may be set "
							    "or 0 if there is no maximum.",
							    0,
							    G_MAXUINT,
							    0,
							    G_PARAM_READABLE |
							    G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_MAX_SIZE,
					 g_param_spec_uint ("avatar-max-size",
							    "Maximum size for avatars in bytes",
							    "Maximum file size of avatar that may be "
							    "set or 0 if there is no maximum.",
							    0,
							    G_MAXUINT,
							    0,
							    G_PARAM_READABLE |
							    G_PARAM_STATIC_STRINGS));


	g_type_class_add_private (object_class, sizeof (EmpathyTpContactFactoryPriv));
}

static void
empathy_tp_contact_factory_init (EmpathyTpContactFactory *tp_factory)
{
	EmpathyTpContactFactoryPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (tp_factory,
		EMPATHY_TYPE_TP_CONTACT_FACTORY, EmpathyTpContactFactoryPriv);

	tp_factory->priv = priv;
	priv->can_request_ft = FALSE;
	priv->can_request_st = FALSE;
	priv->contact_caps_supported = FALSE;
}

static GHashTable *factories = NULL;

static void
tp_contact_factory_connection_invalidated_cb (TpProxy *connection,
					      guint    domain,
					      gint     code,
					      gchar   *message,
					      gpointer user_data)
{
	DEBUG ("Message: %s", message);
	g_hash_table_remove (factories, connection);
}

static void
tp_contact_factory_connection_weak_notify_cb (gpointer connection,
					      GObject *where_the_object_was)
{
	g_hash_table_remove (factories, connection);
}

static void
tp_contact_factory_remove_connection (gpointer connection)
{
	g_signal_handlers_disconnect_by_func (connection,
		tp_contact_factory_connection_invalidated_cb, NULL);
	g_object_unref (connection);
}

EmpathyTpContactFactory *
empathy_tp_contact_factory_dup_singleton (TpConnection *connection)
{
	EmpathyTpContactFactory *tp_factory;

	g_return_val_if_fail (TP_IS_CONNECTION (connection), NULL);

	if (factories == NULL) {
		factories = g_hash_table_new_full (empathy_proxy_hash,
						   empathy_proxy_equal,
						   tp_contact_factory_remove_connection,
						   NULL);
	}

	tp_factory = g_hash_table_lookup (factories, connection);
	if (tp_factory == NULL) {
		tp_factory = g_object_new (EMPATHY_TYPE_TP_CONTACT_FACTORY,
					   "connection", connection,
					   NULL);
		g_hash_table_insert (factories, g_object_ref (connection),
				     tp_factory);
		g_object_weak_ref (G_OBJECT (tp_factory),
				   tp_contact_factory_connection_weak_notify_cb,
				   connection);
		g_signal_connect (connection, "invalidated",
				  G_CALLBACK (tp_contact_factory_connection_invalidated_cb),
				  NULL);
	} else {
		g_object_ref (tp_factory);
	}

	return tp_factory;
}

