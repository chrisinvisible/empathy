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
 * Authors: Davyd Madeley <davyd.madeley@collabora.co.uk>
 */

#ifndef __EMPATHY_KLUDGE_LABEL_H__
#define __EMPATHY_KLUDGE_LABEL_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_KLUDGE_LABEL	(empathy_kludge_label_get_type ())
#define EMPATHY_KLUDGE_LABEL(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_KLUDGE_LABEL, EmpathyKludgeLabel))
#define EMPATHY_KLUDGE_LABEL_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), EMPATHY_TYPE_KLUDGE_LABEL, EmpathyKludgeLabelClass))
#define EMPATHY_IS_KLUDGE_LABEL(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_KLUDGE_LABEL))
#define EMPATHY_IS_KLUDGE_LABEL_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EMPATHY_TYPE_KLUDGE_LABEL))
#define EMPATHY_KLUDGE_LABEL_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_KLUDGE_LABEL, EmpathyKludgeLabelClass))

typedef struct _EmpathyKludgeLabel EmpathyKludgeLabel;
typedef struct _EmpathyKludgeLabelClass EmpathyKludgeLabelClass;

struct _EmpathyKludgeLabel
{
	GtkLabel parent;
};

struct _EmpathyKludgeLabelClass
{
	GtkLabelClass parent_class;
};

GType empathy_kludge_label_get_type (void);
GtkWidget *empathy_kludge_label_new (const char *str);

G_END_DECLS

#endif
