/*
 * Copyright (C) 2009 Collabora Ltd.
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
 *
 * Authors: Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

/* empathy-account-assistant.c */

#include "empathy-account-assistant.h"

G_DEFINE_TYPE (EmpathyAccountAssistant, empathy_account_assistant,
    GTK_TYPE_ASSISTANT)


static void
empathy_account_assistant_class_init (EmpathyAccountAssistantClass *klass)
{
}

static void
empathy_account_assistant_init (EmpathyAccountAssistant *self)
{
}

EmpathyAccountAssistant*
empathy_account_assistant_new (void)
{
  return g_object_new (EMPATHY_TYPE_ACCOUNT_ASSISTANT, NULL);
}
