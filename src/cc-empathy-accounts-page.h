/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
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
 */

#ifndef __CC_EMPATHY_ACCOUNTS_PAGE_H
#define __CC_EMPATHY_ACCOUNTS_PAGE_H

#include <gtk/gtk.h>
#include <libgnome-control-center-extension/cc-page.h>

G_BEGIN_DECLS

#define CC_TYPE_EMPATHY_ACCOUNTS_PAGE         (cc_empathy_accounts_page_get_type ())
#define CC_EMPATHY_ACCOUNTS_PAGE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CC_TYPE_EMPATHY_ACCOUNTS_PAGE, CcEmpathyAccountsPage))
#define CC_EMPATHY_ACCOUNTS_PAGE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CC_TYPE_EMPATHY_ACCOUNTS_PAGE, CcEmpathyAccountsPageClass))
#define CC_IS_EMPATHY_ACCOUNTS_PAGE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CC_TYPE_EMPATHY_ACCOUNTS_PAGE))
#define CC_IS_EMPATHY_ACCOUNTS_PAGE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CC_TYPE_EMPATHY_ACCOUNTS_PAGE))
#define CC_EMPATHY_ACCOUNTS_PAGE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CC_TYPE_EMPATHY_ACCOUNTS_PAGE, CcEmpathyAccountsPageClass))

typedef struct CcEmpathyAccountsPagePrivate CcEmpathyAccountsPagePrivate;

typedef struct
{
  CcPage parent;
  CcEmpathyAccountsPagePrivate *priv;
} CcEmpathyAccountsPage;

typedef struct
{
  CcPageClass parent_class;
} CcEmpathyAccountsPageClass;

GType   cc_empathy_accounts_page_get_type   (void);
CcPage* cc_empathy_accounts_page_new        (void);
void    cc_empathy_accounts_page_destroy_dialogs (CcEmpathyAccountsPage *self);

G_END_DECLS

#endif /* __CC_EMPATHY_ACCOUNTS_PAGE_H */
