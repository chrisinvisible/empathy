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

#ifndef __EMPATHY_TP_CONTACT_FACTORY_H__
#define __EMPATHY_TP_CONTACT_FACTORY_H__

#include <glib.h>

#include <telepathy-glib/connection.h>

#include "empathy-contact.h"

G_BEGIN_DECLS

typedef void (*EmpathyTpContactFactoryContactsByIdCb) (TpConnection            *connection,
						       guint                    n_contacts,
						       EmpathyContact * const * contacts,
						       const gchar * const *    requested_ids,
						       GHashTable              *failed_id_errors,
						       const GError            *error,
						       gpointer                 user_data,
						       GObject                 *weak_object);

typedef void (*EmpathyTpContactFactoryContactsByHandleCb) (TpConnection            *connection,
							   guint                    n_contacts,
							   EmpathyContact * const * contacts,
                                                           guint                    n_failed,
                                                           const TpHandle          *failed,
                                                           const GError            *error,
						           gpointer                 user_data,
						           GObject                 *weak_object);

typedef void (*EmpathyTpContactFactoryContactCb) (TpConnection            *connection,
						  EmpathyContact          *contact,
						  const GError            *error,
						  gpointer                 user_data,
						  GObject                 *weak_object);

void                     empathy_tp_contact_factory_get_from_ids     (TpConnection            *connection,
								      guint                    n_ids,
								      const gchar * const     *ids,
								      EmpathyTpContactFactoryContactsByIdCb callback,
								      gpointer                 user_data,
								      GDestroyNotify           destroy,
								      GObject                 *weak_object);
void                     empathy_tp_contact_factory_get_from_handles (TpConnection            *connection,
								      guint                    n_handles,
								      const TpHandle          *handles,
								      EmpathyTpContactFactoryContactsByHandleCb callback,
								      gpointer                 user_data,
								      GDestroyNotify           destroy,
								      GObject                 *weak_object);
void                     empathy_tp_contact_factory_get_from_id      (TpConnection            *connection,
								      const gchar             *id,
								      EmpathyTpContactFactoryContactCb callback,
								      gpointer                 user_data,
								      GDestroyNotify           destroy,
								      GObject                 *weak_object);
void                     empathy_tp_contact_factory_get_from_handle  (TpConnection            *connection,
								      TpHandle                 handle,
								      EmpathyTpContactFactoryContactCb callback,
								      gpointer                 user_data,
								      GDestroyNotify           destroy,
								      GObject                 *weak_object);
void                     empathy_tp_contact_factory_set_alias        (EmpathyContact          *contact,
								      const gchar             *alias);
G_END_DECLS

#endif /* __EMPATHY_TP_CONTACT_FACTORY_H__ */
