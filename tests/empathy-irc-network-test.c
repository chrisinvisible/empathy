#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "test-irc-helper.h"
#include "test-helper.h"

#include <libempathy/empathy-irc-network.h>

static void
test_empathy_irc_network_new (void)
{
  EmpathyIrcNetwork *network;

  network = empathy_irc_network_new ("Network1");
  check_network (network, "Network1", "UTF-8", NULL, 0);

  g_object_unref (network);
}

static void
test_property_change (void)
{
  EmpathyIrcNetwork *network;

  network = empathy_irc_network_new ("Network1");
  check_network (network, "Network1", "UTF-8", NULL, 0);

  g_object_set (network,
      "name", "Network2",
      "charset", "ISO-8859-1",
      NULL);

  check_network (network, "Network2", "ISO-8859-1", NULL, 0);

  g_object_unref (network);

}

static gboolean modified;

static void
modified_cb (EmpathyIrcNetwork *network,
             gpointer unused)
{
  modified = TRUE;
}

static void
test_modified_signal (void)
{
  EmpathyIrcNetwork *network;

  network = empathy_irc_network_new ("Network1");
  check_network (network, "Network1", "UTF-8", NULL, 0);

  modified = FALSE;
  g_signal_connect (network, "modified", G_CALLBACK (modified_cb), NULL);

  g_object_set (network, "name", "Network2", NULL);
  g_assert (modified);
  modified = FALSE;
  g_object_set (network, "name", "Network2", NULL);
  g_assert (!modified);

  g_object_unref (network);
}

static void
add_servers (EmpathyIrcNetwork *network,
             struct server_t *servers,
             guint nb_servers)
{
  guint i;

  for (i = 0; i < nb_servers; i ++)
    {
      EmpathyIrcServer *server;

      server = empathy_irc_server_new (servers[i].address,
          servers[i].port, servers[i].ssl);
      modified = FALSE;
      empathy_irc_network_append_server (network, server);
      g_assert (modified);
      g_object_unref (server);
    }
}

static void
test_add_server (void)
{
  EmpathyIrcNetwork *network;
  EmpathyIrcServer *server;
  GSList *servers, *l;
  struct server_t test_servers[] = {
    { "server1", 6667, FALSE },
    { "server2", 6668, TRUE },
    { "server3", 6667, FALSE },
    { "server4", 6669, TRUE }};
  struct server_t servers_without_3[] = {
    { "server1", 6667, FALSE },
    { "server2", 6668, TRUE },
    { "server4", 6669, TRUE }};

  network = empathy_irc_network_new ("Network1");
  check_network (network, "Network1", "UTF-8", NULL, 0);

  modified = FALSE;
  g_signal_connect (network, "modified", G_CALLBACK (modified_cb), NULL);

  check_network (network, "Network1", "UTF-8", NULL, 0);

  /* add the servers */
  add_servers (network, test_servers, 4);

  check_network (network, "Network1", "UTF-8", test_servers, 4);

  /* Now let's remove the 3rd server */
  servers = empathy_irc_network_get_servers (network);
  l = g_slist_nth (servers, 2);
  g_assert (l != NULL);
  server = l->data;
  modified = FALSE;
  empathy_irc_network_remove_server (network, server);
  g_assert (modified);

  /* free the list */
  g_slist_foreach (servers, (GFunc) g_object_unref, NULL);
  g_slist_free (servers);

  /* The 3rd server should have disappear */
  check_network (network, "Network1", "UTF-8", servers_without_3, 3);

  g_object_unref (network);
}

static void
test_modified_signal_because_of_server (void)
{
  EmpathyIrcNetwork *network;
  EmpathyIrcServer *server;

  network = empathy_irc_network_new ("Network1");
  check_network (network, "Network1", "UTF-8", NULL, 0);

  g_signal_connect (network, "modified", G_CALLBACK (modified_cb), NULL);

  server = empathy_irc_server_new ("server1", 6667, FALSE);
  empathy_irc_network_append_server (network, server);

  /* Change server properties */
  modified = FALSE;
  g_object_set (server, "address", "server2", NULL);
  g_assert (modified);
  modified = FALSE;
  g_object_set (server, "port", 6668, NULL);
  g_assert (modified);
  modified = FALSE;
  g_object_set (server, "ssl", TRUE, NULL);
  g_assert (modified);

  empathy_irc_network_remove_server (network, server);
  modified = FALSE;
  g_object_set (server, "address", "server3", NULL);
  /* We removed the server so the network is not modified anymore */
  g_assert (!modified);

  g_object_unref (network);
}

static void
test_empathy_irc_network_set_server_position (void)
{
  EmpathyIrcNetwork *network;
  GSList *servers, *l;
  struct server_t test_servers[] = {
    { "server1", 6667, FALSE },
    { "server2", 6668, TRUE },
    { "server3", 6667, FALSE },
    { "server4", 6669, TRUE }};
  struct server_t test_servers_sorted[] = {
    { "server2", 6668, TRUE },
    { "server4", 6669, TRUE },
    { "server3", 6667, FALSE },
    { "server1", 6667, FALSE }};

  network = empathy_irc_network_new ("Network1");
  check_network (network, "Network1", "UTF-8", NULL, 0);

  modified = FALSE;
  g_signal_connect (network, "modified", G_CALLBACK (modified_cb), NULL);

  /* add the servers */
  add_servers (network, test_servers, 4);
  check_network (network, "Network1", "UTF-8", test_servers, 4);

  /* get servers list */
  servers = empathy_irc_network_get_servers (network);
  g_assert (g_slist_length (servers) == 4);
  modified = FALSE;

  /* server1 go to the last position */
  empathy_irc_network_set_server_position (network, servers->data, -1);

  /* server2 go to the first position */
  l = servers->next;
  empathy_irc_network_set_server_position (network, l->data, 0);

  /* server3 go to the third position */
  l = l->next;
  empathy_irc_network_set_server_position (network, l->data, 2);

  /* server4 go to the second position*/
  l = l->next;
  empathy_irc_network_set_server_position (network, l->data, 1);

  g_assert (modified);

  /* free the list */
  g_slist_foreach (servers, (GFunc) g_object_unref, NULL);
  g_slist_free (servers);

  /* Check if servers are sorted */
  check_network (network, "Network1", "UTF-8", test_servers_sorted, 4);
}

int
main (int argc,
    char **argv)
{
  int result;

  test_init (argc, argv);

  g_test_add_func ("/irc-network/new", test_empathy_irc_network_new);
  g_test_add_func ("/irc-network/property-change", test_property_change);
  g_test_add_func ("/irc-network/modified-signal", test_modified_signal);
  g_test_add_func ("/irc-network/add-server", test_add_server);
  g_test_add_func ("/irc-network/modified-signal-because-of-server",
      test_modified_signal_because_of_server);
  g_test_add_func ("/irc-network/set-server-position",
      test_empathy_irc_network_set_server_position);

  result = g_test_run ();
  test_deinit ();
  return result;
}
