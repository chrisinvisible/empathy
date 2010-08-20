/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2008, 2010 Collabora Ltd.
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
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Philip Withnall <philip.withnall@collabora.co.uk>
 *
 * Based off EmpathyContactListStore.
 */

#ifndef __EMPATHY_PERSONA_STORE_H__
#define __EMPATHY_PERSONA_STORE_H__

#include <gtk/gtk.h>

#include <folks/folks.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_PERSONA_STORE (empathy_persona_store_get_type ())
#define EMPATHY_PERSONA_STORE(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), \
    EMPATHY_TYPE_PERSONA_STORE, EmpathyPersonaStore))
#define EMPATHY_PERSONA_STORE_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), \
    EMPATHY_TYPE_PERSONA_STORE, EmpathyPersonaStoreClass))
#define EMPATHY_IS_PERSONA_STORE(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
    EMPATHY_TYPE_PERSONA_STORE))
#define EMPATHY_IS_PERSONA_STORE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), \
    EMPATHY_TYPE_PERSONA_STORE))
#define EMPATHY_PERSONA_STORE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), \
    EMPATHY_TYPE_PERSONA_STORE, EmpathyPersonaStoreClass))

typedef enum
{
  EMPATHY_PERSONA_STORE_SORT_STATE,
  EMPATHY_PERSONA_STORE_SORT_NAME
} EmpathyPersonaStoreSort;

typedef enum
{
  EMPATHY_PERSONA_STORE_COL_ICON_STATUS,
  EMPATHY_PERSONA_STORE_COL_PIXBUF_AVATAR,
  EMPATHY_PERSONA_STORE_COL_PIXBUF_AVATAR_VISIBLE,
  EMPATHY_PERSONA_STORE_COL_NAME,
  EMPATHY_PERSONA_STORE_COL_ACCOUNT_NAME,
  EMPATHY_PERSONA_STORE_COL_DISPLAY_ID,
  EMPATHY_PERSONA_STORE_COL_PRESENCE_TYPE,
  EMPATHY_PERSONA_STORE_COL_STATUS,
  EMPATHY_PERSONA_STORE_COL_PERSONA,
  EMPATHY_PERSONA_STORE_COL_IS_ACTIVE,
  EMPATHY_PERSONA_STORE_COL_IS_ONLINE,
  EMPATHY_PERSONA_STORE_COL_CAN_AUDIO_CALL,
  EMPATHY_PERSONA_STORE_COL_CAN_VIDEO_CALL,
  EMPATHY_PERSONA_STORE_COL_COUNT,
} EmpathyPersonaStoreCol;

typedef struct
{
  GtkListStore parent;
  gpointer priv;
} EmpathyPersonaStore;

typedef struct
{
  GtkListStoreClass parent_class;
} EmpathyPersonaStoreClass;

GType empathy_persona_store_get_type (void) G_GNUC_CONST;

EmpathyPersonaStore *empathy_persona_store_new (FolksIndividual *individual);

FolksIndividual *empathy_persona_store_get_individual (
    EmpathyPersonaStore *self);
void empathy_persona_store_set_individual (EmpathyPersonaStore *self,
    FolksIndividual *individual);

gboolean empathy_persona_store_get_show_avatars (EmpathyPersonaStore *self);
void empathy_persona_store_set_show_avatars (EmpathyPersonaStore *self,
    gboolean show_avatars);

gboolean empathy_persona_store_get_show_protocols (EmpathyPersonaStore *self);
void empathy_persona_store_set_show_protocols   (EmpathyPersonaStore *self,
    gboolean show_protocols);

EmpathyPersonaStoreSort empathy_persona_store_get_sort_criterion (
    EmpathyPersonaStore *self);
void empathy_persona_store_set_sort_criterion (EmpathyPersonaStore *self,
    EmpathyPersonaStoreSort sort_criterion);

G_END_DECLS

#endif /* __EMPATHY_PERSONA_STORE_H__ */
