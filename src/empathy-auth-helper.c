/*
 * Copyright (C) 2010 Collabora Ltd.
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
 * Authors: Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#include <config.h>

#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#define DEBUG_FLAG EMPATHY_DEBUG_TLS
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-auth-factory.h>
#include <libempathy/empathy-server-tls-handler.h>

#include <libempathy-gtk/empathy-ui-utils.h>

static void
auth_factory_new_handler_cb (EmpathyAuthFactory *factory,
    EmpathyServerTLSHandler *handler,
    gpointer user_data)
{
  DEBUG ("New TLS server handler received from the factory");
}

int
main (int argc,
    char **argv)
{
  GOptionContext *context;
  GError *error = NULL;
  EmpathyAuthFactory *factory;

  g_thread_init (NULL);

  context = g_option_context_new (N_(" - Empathy authentication helper"));
  g_option_context_add_group (context, gtk_get_option_group (TRUE));
  g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_print ("%s\nRun '%s --help' to see a full list of available command "
          "line options.\n", error->message, argv[0]);
      g_warning ("Error in empathy-auth-helper init: %s", error->message);
      return EXIT_FAILURE;
    }

  g_option_context_free (context);

  empathy_gtk_init ();
  g_set_application_name (_("Empathy authentication helper"));

  gtk_window_set_default_icon_name ("empathy");
  textdomain (GETTEXT_PACKAGE);

  factory = empathy_auth_factory_dup_singleton ();

  g_signal_connect (factory, "new-server-tls-handler",
      G_CALLBACK (auth_factory_new_handler_cb), NULL);

  if (!empathy_auth_factory_register (factory, &error))
    {
      g_critical ("Failed to register the auth factory: %s\n", error->message);
      g_error_free (error);
      g_object_unref (factory);

      return EXIT_FAILURE;
    }

  DEBUG ("Empathy auth client started.");  

  gtk_main ();

  return EXIT_SUCCESS;
}
