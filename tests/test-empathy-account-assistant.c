#include <config.h>

#include <gtk/gtk.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include "empathy-account-assistant.h"

int main (int argc, char **argv)
{
  GtkWidget *assistant;

  gtk_init (&argc, &argv);
  empathy_gtk_init ();

  assistant = empathy_account_assistant_show (NULL);

  gtk_widget_show_all (assistant);

  g_signal_connect_swapped (assistant, "destroy",
      G_CALLBACK (gtk_main_quit), NULL);

  gtk_main ();

  return 0;
}
