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

#include <extensions/extensions.h>

#include "empathy-tp-contact-factory.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_TP | EMPATHY_DEBUG_CONTACT
#include "empathy-debug.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyTpContactFactory)
typedef struct {
	TpConnection   *connection;
	GList          *contacts;
} EmpathyTpContactFactoryPriv;

G_DEFINE_TYPE (EmpathyTpContactFactory, empathy_tp_contact_factory, G_TYPE_OBJECT);

enum {
	PROP_0,
	PROP_CONNECTION,
};

static TpContactFeature contact_features[] = {
	TP_CONTACT_FEATURE_ALIAS,
	TP_CONTACT_FEATURE_AVATAR_TOKEN,
	TP_CONTACT_FEATURE_AVATAR_DATA,
	TP_CONTACT_FEATURE_PRESENCE,
	TP_CONTACT_FEATURE_LOCATION,
	TP_CONTACT_FEATURE_CAPABILITIES,
};

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
tp_contact_factory_add_contact (EmpathyTpContactFactory *tp_factory,
				EmpathyContact          *contact)
{
	EmpathyTpContactFactoryPriv *priv = GET_PRIV (tp_factory);
	TpHandle self_handle;
	TpHandle handle;

	/* Keep a weak ref to that contact */
	g_object_weak_ref (G_OBJECT (contact),
			   tp_contact_factory_weak_notify,
			   tp_factory);
	priv->contacts = g_list_prepend (priv->contacts, contact);

	/* The contact keeps a ref to its factory */
	g_object_set_data_full (G_OBJECT (contact), "empathy-factory",
				g_object_ref (tp_factory),
				g_object_unref);

	/* Set is-user property. Note that it could still be the handle is
	 * different from the connection's self handle, in the case the handle
	 * comes from a group interface. */
	self_handle = tp_connection_get_self_handle (priv->connection);
	handle = empathy_contact_get_handle (contact);
	empathy_contact_set_is_user (contact, self_handle == handle);

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

	G_OBJECT_CLASS (empathy_tp_contact_factory_parent_class)->finalize (object);
}

static void
empathy_tp_contact_factory_class_init (EmpathyTpContactFactoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tp_contact_factory_finalize;
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

	g_type_class_add_private (object_class, sizeof (EmpathyTpContactFactoryPriv));
}

static void
empathy_tp_contact_factory_init (EmpathyTpContactFactory *tp_factory)
{
	EmpathyTpContactFactoryPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (tp_factory,
		EMPATHY_TYPE_TP_CONTACT_FACTORY, EmpathyTpContactFactoryPriv);

	tp_factory->priv = priv;
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

