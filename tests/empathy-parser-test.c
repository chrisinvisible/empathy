#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "test-helper.h"

#define DEBUG_FLAG EMPATHY_DEBUG_TESTS
#include <libempathy/empathy-debug.h>

#include <libempathy-gtk/empathy-ui-utils.h>

static void
test_replace_match (const gchar *text,
                    gssize len,
                    gpointer match_data,
                    gpointer user_data)
{
  GString *string = user_data;

  g_string_append_c (string, '[');
  g_string_append_len (string, text, len);
  g_string_append_c (string, ']');
}

static void
test_replace_verbatim (const gchar *text,
                       gssize len,
                       gpointer match_data,
                       gpointer user_data)
{
  GString *string = user_data;

  g_string_append_len (string, text, len);
}

static void
test_parsers (void)
{
  guint i;
  gchar *tests[] =
    {
      /* Basic link matches */
      "http://foo.com", "[http://foo.com]",
      "git://foo.com", "[git://foo.com]",
      "git+ssh://foo.com", "[git+ssh://foo.com]",
      "mailto:user@server.com", "[mailto:user@server.com]",
      "www.foo.com", "[www.foo.com]",
      "ftp.foo.com", "[ftp.foo.com]",
      "user@server.com", "[user@server.com]",
      "http://foo.com. bar", "[http://foo.com]. bar",
      "http://foo.com; bar", "[http://foo.com]; bar",
      "http://foo.com: bar", "[http://foo.com]: bar",
      "http://foo.com:bar", "[http://foo.com:bar]",

      /* They are not links! */
      "http://", "http[:/]/", /* Hm... */
      "www.", "www.",
      "w.foo.com", "w.foo.com",
      "@server.com", "@server.com",
      "mailto:user@", "mailto:user@",
      "mailto:user@.com", "mailto:user@.com",
      "user@.com", "user@.com",

      /* Links inside (), {}, [] or "" */
      /* FIXME: How to test if the ending ] is matched or not? */
      "Foo (www.foo.com)", "Foo ([www.foo.com])",
      "Foo {www.foo.com}", "Foo {[www.foo.com]}",
      "Foo [www.foo.com]", "Foo [[www.foo.com]]",
      "Foo \"www.foo.com\"", "Foo \"[www.foo.com]\"",
      "Foo (www.foo.com/bar(123)baz)", "Foo ([www.foo.com/bar(123)baz])",
      "<a href=\"http://foo.com\">bar</a>", "<a href=\"[http://foo.com]\">bar</a>",

      /* Basic smileys */
      "a:)b", "a[:)]b",
      ">:)", "[>:)]",
      ">:(", ">[:(]",

      /* Smileys and links mixed */
      ":)http://foo.com", "[:)][http://foo.com]",
      "a :) b http://foo.com c :( d www.test.com e", "a [:)] b [http://foo.com] c [:(] d [www.test.com] e",

      /* FIXME: Known issues. Brackets should be counted by the parser */
      //"Foo www.bar.com/test(123)", "Foo [www.bar.com/test(123)]",
      //"Foo (www.bar.com/test(123))", "Foo ([www.bar.com/test(123)])",
      //"Foo www.bar.com/test{123}", "Foo [www.bar.com/test{123}]",
      //"Foo (:))", "Foo ([:)])",
      //"Foo <a href=\"http://foo.com\">:)</a>", "Foo <a href=\"[http://foo.com]\">[:)]</a>",

      NULL, NULL
    };
  EmpathyStringParser parsers[] =
    {
      {empathy_string_match_link, test_replace_match},
      {empathy_string_match_smiley, test_replace_match},
      {empathy_string_match_all, test_replace_verbatim},
      {NULL, NULL}
    };

  DEBUG ("Started");
  for (i = 0; tests[i] != NULL; i += 2)
    {
      GString *string;

      string = g_string_new (NULL);
      empathy_string_parser_substr (tests[i], -1, parsers, string);

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
