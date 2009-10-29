/*
 * test-helper.h - Header for some test helper functions
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
 */

#ifndef __TEST_HELPER_H__
#define __TEST_HELPER_H__

#include <telepathy-glib/account.h>

void test_init (int argc,
    char **argv);

void test_deinit (void);

gchar * get_xml_file (const gchar *filename);
gchar * get_user_xml_file (const gchar *filename);
void copy_xml_file (const gchar *orig, const gchar *dest);
TpAccount * get_test_account (void);
void destroy_test_account (TpAccount *account);


#endif
