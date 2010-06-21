/*
 * test-helper.c - Source for some test helper functions
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

#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include <libempathy-gtk/empathy-ui-utils.h>

#include "test-helper.h"

void
test_init (int argc,
    char **argv)
{
  g_test_init (&argc, &argv, NULL);
  gtk_init (&argc, &argv);
  empathy_gtk_init ();
}

void
test_deinit (void)
{
  ;
}

gchar *
get_xml_file (const gchar *filename)
{
  return g_build_filename (g_getenv ("EMPATHY_SRCDIR"), "tests", "xml",
      filename, NULL);
}

gchar *
get_user_xml_file (const gchar *filename)
{
  return g_build_filename (g_get_tmp_dir (), filename, NULL);
}

void
copy_xml_file (const gchar *orig,
               const gchar *dest)
{
  gboolean result;
  gchar *buffer;
  gsize length;
  gchar *sample;
  gchar *file;

  sample = get_xml_file (orig);
  result = g_file_get_contents (sample, &buffer, &length, NULL);
  g_assert (result);

  file = get_user_xml_file (dest);
  result = g_file_set_contents (file, buffer, length, NULL);
  g_assert (result);

  g_free (sample);
  g_free (file);
  g_free (buffer);
}

