#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "test-helper.h"

#include <libempathy/empathy-chatroom.h>

#if 0
static EmpathyChatroom *
create_chatroom (void)
{
  EmpathyAccount *account;
  EmpathyChatroom *chatroom;

  account = get_test_account ();
  chatroom = empathy_chatroom_new (account);
  fail_if (chatroom == NULL);

  return chatroom;
}

START_TEST (test_empathy_chatroom_new)
{
  EmpathyChatroom *chatroom;
  gboolean auto_connect, favorite;

  chatroom = create_chatroom ();
  fail_if (empathy_chatroom_get_auto_connect (chatroom));
  g_object_get (chatroom,
      "auto_connect", &auto_connect,
      "favorite", &favorite,
      NULL);
  fail_if (auto_connect);
  fail_if (favorite);

  g_object_unref (empathy_chatroom_get_account (chatroom));
  g_object_unref (chatroom);
}
END_TEST

START_TEST (test_favorite_and_auto_connect)
{
  /* auto connect implies favorite */
  EmpathyChatroom *chatroom;
  gboolean auto_connect, favorite;

  chatroom = create_chatroom ();

  /* set auto_connect so favorite as a side effect */
  empathy_chatroom_set_auto_connect (chatroom, TRUE);
  fail_if (!empathy_chatroom_get_auto_connect (chatroom));
  g_object_get (chatroom,
      "auto_connect", &auto_connect,
      "favorite", &favorite,
      NULL);
  fail_if (!auto_connect);
  fail_if (!favorite);

  /* Remove auto_connect. Chatroom is still favorite */
  empathy_chatroom_set_auto_connect (chatroom, FALSE);
  fail_if (empathy_chatroom_get_auto_connect (chatroom));
  g_object_get (chatroom,
      "auto_connect", &auto_connect,
      "favorite", &favorite,
      NULL);
  fail_if (auto_connect);
  fail_if (!favorite);

  /* Remove favorite too now */
  g_object_set (chatroom, "favorite", FALSE, NULL);
  fail_if (empathy_chatroom_get_auto_connect (chatroom));
  g_object_get (chatroom,
      "auto_connect", &auto_connect,
      "favorite", &favorite,
      NULL);
  fail_if (auto_connect);
  fail_if (favorite);

  /* Just add favorite but not auto-connect */
  g_object_set (chatroom, "favorite", TRUE, NULL);
  fail_if (empathy_chatroom_get_auto_connect (chatroom));
  g_object_get (chatroom,
      "auto_connect", &auto_connect,
      "favorite", &favorite,
      NULL);
  fail_if (auto_connect);
  fail_if (!favorite);

  /* ... and re-add auto_connect */
  g_object_set (chatroom, "auto_connect", TRUE, NULL);
  fail_if (!empathy_chatroom_get_auto_connect (chatroom));
  g_object_get (chatroom,
      "auto_connect", &auto_connect,
      "favorite", &favorite,
      NULL);
  fail_if (!auto_connect);
  fail_if (!favorite);

  /* Remove favorite remove auto_connect too */
  g_object_set (chatroom, "favorite", FALSE, NULL);
  fail_if (empathy_chatroom_get_auto_connect (chatroom));
  g_object_get (chatroom,
      "auto_connect", &auto_connect,
      "favorite", &favorite,
      NULL);
  fail_if (auto_connect);
  fail_if (favorite);

  g_object_unref (empathy_chatroom_get_account (chatroom));
  g_object_unref (chatroom);
}
END_TEST

static void
favorite_changed (EmpathyChatroom *chatroom,
                  GParamSpec *spec,
                  gboolean *changed)
{
  *changed = TRUE;
}

START_TEST (test_change_favorite)
{
  EmpathyChatroom *chatroom;
  gboolean changed = FALSE;

  chatroom = create_chatroom ();

  g_signal_connect (chatroom, "notify::favorite", G_CALLBACK (favorite_changed),
      &changed);

  /* change favorite to TRUE */
  g_object_set (chatroom, "favorite", TRUE, NULL);
  fail_if (!changed);

  changed = FALSE;

  /* change favorite to FALSE */
  g_object_set (chatroom, "favorite", FALSE, NULL);
  fail_if (!changed);
}
END_TEST
#endif

int
main (int argc,
    char **argv)
{
  int result;

  test_init (argc, argv);

#if 0
  g_test_add_func ("/chatroom/new", test_empathy_chatroom_new);
  g_test_add_func ("/chatroom/favorite-and-auto-connect",
      test_favorite_and_auto_connect);
  g_test_add_func ("/chatroom/change-favorite", test_change_favorite);
#endif

  result = g_test_run ();
  test_deinit ();
  return result;
}
