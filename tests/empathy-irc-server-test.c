#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "test-irc-helper.h"
#include "test-helper.h"

#include <libempathy/empathy-irc-server.h>

static void
test_empathy_irc_server_new (void)
{
  EmpathyIrcServer *server;

  server = empathy_irc_server_new ("test.localhost", 6667, TRUE);
  check_server (server, "test.localhost", 6667, TRUE);

  g_object_unref (server);
}

static void
test_property_change (void)
{
  EmpathyIrcServer *server;

  server = empathy_irc_server_new ("test.localhost", 6667, TRUE);
  g_assert (server != NULL);

  g_object_set (server,
      "address", "test2.localhost",
      "port", 6668,
      "ssl", FALSE,
      NULL);

  check_server (server, "test2.localhost", 6668, FALSE);

  g_object_unref (server);
}

static gboolean modified = FALSE;

static void
modified_cb (EmpathyIrcServer *server,
             gpointer unused)
{
  modified = TRUE;
}

static void
test_modified_signal (void)
{
  EmpathyIrcServer *server;

  server = empathy_irc_server_new ("test.localhost", 6667, TRUE);
  g_assert (server != NULL);

  g_signal_connect (server, "modified", G_CALLBACK (modified_cb), NULL);

  /* address */
  g_object_set (server, "address", "test2.localhost", NULL);
  g_assert (modified);
  modified = FALSE;
  g_object_set (server, "address", "test2.localhost", NULL);
  g_assert (!modified);

  /* port */
  g_object_set (server, "port", 6668, NULL);
  g_assert (modified);
  modified = FALSE;
  g_object_set (server, "port", 6668, NULL);
  g_assert (!modified);

  /* ssl */
  g_object_set (server, "ssl", FALSE, NULL);
  g_assert (modified);
  modified = FALSE;
  g_object_set (server, "ssl", FALSE, NULL);
  g_assert (!modified);

  g_object_unref (server);
}

int
main (int argc,
    char **argv)
{
  int result;

  test_init (argc, argv);

  g_test_add_func ("/irc-server/new", test_empathy_irc_server_new);
  g_test_add_func ("/irc-server/property-change", test_property_change);
  g_test_add_func ("/irc-server/modified-signal", test_modified_signal);

  result = g_test_run ();
  test_deinit ();
  return result;
}
