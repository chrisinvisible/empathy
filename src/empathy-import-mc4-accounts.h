/*
 * Copyright (C) 2009 Collabora Ltd.
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
 * Authors: Arnaud Maillet <arnaud.maillet@collabora.co.uk>
 */

#ifndef __EMPATHY_IMPORT_MC4_ACCOUNTS_H__
#define __EMPATHY_IMPORT_MC4_ACCOUNTS_H__

G_BEGIN_DECLS

#include <libempathy/empathy-connection-managers.h>

gboolean empathy_import_mc4_accounts (EmpathyConnectionManagers *managers);
gboolean empathy_import_mc4_has_imported (void);

G_END_DECLS

#endif /* __EMPATHY_IMPORT_MC4_ACCOUNTS_H__ */
