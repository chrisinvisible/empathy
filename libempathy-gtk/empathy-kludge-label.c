/* vim: set ts=4 sts=4 sw=4 et: */
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

#include "empathy-kludge-label.h"

G_DEFINE_TYPE (EmpathyKludgeLabel, empathy_kludge_label, GTK_TYPE_LABEL);

static void
empathy_kludge_label_size_allocate (GtkWidget     *self,
                                    GtkAllocation *allocation)
{
    PangoLayout *layout;

    GTK_WIDGET_CLASS (empathy_kludge_label_parent_class)->size_allocate (
            self, allocation);

    /* force the width of the PangoLayout to be the width of the allocation */
    layout = gtk_label_get_layout (GTK_LABEL (self));
    pango_layout_set_width (layout, allocation->width * PANGO_SCALE);
}

static gboolean
empathy_kludge_label_expose_event (GtkWidget      *self,
                                   GdkEventExpose *event)
{
    PangoLayout *layout;
    PangoRectangle rect;
    GtkAllocation real_allocation;
    GtkAllocation allocation;
    gboolean r;

    layout = gtk_label_get_layout (GTK_LABEL (self));
    pango_layout_get_pixel_extents (layout, NULL, &rect);

    /* this is mind-bendingly evil:
     * get_layout_location() is going to remove rect.x from the position of the
     * layout when painting it. This really sucks. We're going to compensate by
     * adding this value to the allocation.
     */
    gtk_widget_get_allocation (self, &allocation);
    real_allocation = allocation;
    allocation.x += rect.x;
    gtk_widget_set_allocation (self, &allocation);

    r = GTK_WIDGET_CLASS (empathy_kludge_label_parent_class)->expose_event (
            self, event);

    gtk_widget_set_allocation (self, &real_allocation);

    return r;
}

static void
empathy_kludge_label_class_init (EmpathyKludgeLabelClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    widget_class->size_allocate = empathy_kludge_label_size_allocate;
    widget_class->expose_event = empathy_kludge_label_expose_event;
}

static void
empathy_kludge_label_init (EmpathyKludgeLabel *self)
{
}

GtkWidget *
empathy_kludge_label_new (const char *str)
{
    return g_object_new (EMPATHY_TYPE_KLUDGE_LABEL,
            "label", str,
            "xalign", 0.,
            NULL);
}
