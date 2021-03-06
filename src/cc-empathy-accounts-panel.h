/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifndef __CC_EMPATHY_ACCOUNTS_PANEL_H
#define __CC_EMPATHY_ACCOUNTS_PANEL_H

#include <gtk/gtk.h>
#include <libgnome-control-center/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_EMPATHY_ACCOUNTS_PANEL         (cc_empathy_accounts_panel_get_type ())
#define CC_EMPATHY_ACCOUNTS_PANEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CC_TYPE_EMPATHY_ACCOUNTS_PANEL, CcEmpathyAccountsPanel))
#define CC_EMPATHY_ACCOUNTS_PANEL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CC_TYPE_EMPATHY_ACCOUNTS_PANEL, CcEmpathyAccountsPanelClass))
#define CC_IS_EMPATHY_ACCOUNTS_PANEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CC_TYPE_EMPATHY_ACCOUNTS_PANEL))
#define CC_IS_EMPATHY_ACCOUNTS_PANEL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CC_TYPE_EMPATHY_ACCOUNTS_PANEL))
#define CC_EMPATHY_ACCOUNTS_PANEL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CC_TYPE_EMPATHY_ACCOUNTS_PANEL, CcEmpathyAccountsPanelClass))

typedef struct CcEmpathyAccountsPanelPrivate CcEmpathyAccountsPanelPrivate;

typedef struct
{
  CcPanel parent;
  CcEmpathyAccountsPanelPrivate *priv;
} CcEmpathyAccountsPanel;

typedef struct
{
  CcPanelClass parent_class;
} CcEmpathyAccountsPanelClass;

GType cc_empathy_accounts_panel_get_type (void);
void  cc_empathy_accounts_panel_register (GIOModule *module);

G_END_DECLS

#endif /* __CC_EMPATHY_ACCOUNTS_PANEL_H */
