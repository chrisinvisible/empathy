#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>

#include "test-helper.h"

void
test_init (int argc,
    char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_type_init ();
}

void
test_deinit (void)
{
  ;
}
