#include <config.h>

#include <gtk/gtk.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include "empathy-account-assistant.h"

static void
managers_prepare_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GtkWidget *assistant;
  EmpathyConnectionManagers *managers = EMPATHY_CONNECTION_MANAGERS (source);

  g_assert (empathy_connection_managers_prepare_finish (managers, result,
        NULL));

  assistant = empathy_account_assistant_show (NULL, managers);

  gtk_widget_show_all (assistant);

  g_signal_connect_swapped (assistant, "destroy",
      G_CALLBACK (gtk_main_quit), NULL);
}

int main (int argc, char **argv)
{
  EmpathyConnectionManagers *managers;

  gtk_init (&argc, &argv);
  empathy_gtk_init ();

  managers = empathy_connection_managers_dup_singleton ();

  empathy_connection_managers_prepare_async (managers,
      managers_prepare_cb, NULL);

  gtk_main ();

  return 0;
}
