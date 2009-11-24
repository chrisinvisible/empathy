#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "test-helper.h"

#define DEBUG_FLAG EMPATHY_DEBUG_TESTS
#include <libempathy/empathy-debug.h>

#include <libempathy-gtk/empathy-ui-utils.h>

static void
test_replace_link (GString *string,
                   const gchar *text,
                   gssize len,
                   gpointer user_data)
{
  g_string_append_c (string, '[');
  g_string_append_len (string, text, len);
  g_string_append_c (string, ']');
}

static void
test_parsers (void)
{
  guint i;
  gchar *tests[] =
    {
      "http://foo.com", "[http://foo.com]",
      NULL, NULL
    };
  EmpathyStringParser parsers[] =
    {
      {empathy_string_match_link, test_replace_link},
      {NULL, NULL}
    };

  for (i = 0; tests[i] != NULL; i += 2)
    {
      GString *string;

      string = g_string_new (NULL);
      empathy_string_parser_substr (string, tests[i], -1, parsers);

      DEBUG ("'%s' => '%s'", tests[i], string->str);
      g_assert_cmpstr (tests[i + 1], ==, string->str);

      g_string_free (string, TRUE);
    }
}

int
main (int argc,
    char **argv)
{
  int result;

  test_init (argc, argv);

  g_test_add_func ("/parsers", test_parsers);

  result = g_test_run ();
  test_deinit ();

  return result;
}
