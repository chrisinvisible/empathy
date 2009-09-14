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
 * Authors: Jonny Lamb <jonny.lamb@collabora.co.uk>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#ifndef __EMPATHY_IMPORT_UTILS_H__
#define __EMPATHY_IMPORT_UTILS_H__

#include <telepathy-glib/connection-manager.h>
#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  /* Table mapping CM param string to a GValue */
  GHashTable *settings;
  /* Protocol name */
  gchar *protocol;
  /* Connection manager name */
  gchar *connection_manager;
  /* The name of the account import source */
  gchar *source;
} EmpathyImportAccountData;

typedef enum {
  EMPATHY_IMPORT_APPLICATION_ALL = 0,
  EMPATHY_IMPORT_APPLICATION_PIDGIN,
  EMPATHY_IMPORT_APPLICATION_INVALID
} EmpathyImportApplication;

EmpathyImportAccountData *empathy_import_account_data_new (
    const gchar *source);
void empathy_import_account_data_free (EmpathyImportAccountData *data);

gboolean empathy_import_accounts_to_import (void);
GList *empathy_import_accounts_load (EmpathyImportApplication id);

gboolean empathy_import_protocol_is_supported (const gchar *protocol,
    TpConnectionManager **cm);

G_END_DECLS

#endif /* __EMPATHY_IMPORT_UTILS_H__ */
