/*
 * Copyright (C) 2007 Raphael Slinckx <raphael@slinckx.net>
 * Copyright (C) 2007-2009 Collabora Ltd.
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
 * Authors: Raphael Slinckx <raphael@slinckx.net>
 *          Cosimo Cecchi   <cosimo.cecchi@collabora.co.uk>
 */

#include <config.h>

#include <gtk/gtk.h>

#include <libempathy/empathy-utils.h>

#include "empathy-cell-renderer-activatable.h"

enum {
  PATH_ACTIVATED,
  LAST_SIGNAL
};

enum {
  PROP_SHOW_ON_SELECT = 1
};

typedef struct {
  gboolean show_on_select;
} EmpathyCellRendererActivatablePriv;

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EmpathyCellRendererActivatable,
    empathy_cell_renderer_activatable, GTK_TYPE_CELL_RENDERER_PIXBUF)

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyCellRendererActivatable)

static void
empathy_cell_renderer_activatable_init (EmpathyCellRendererActivatable *cell)
{
  cell->priv = G_TYPE_INSTANCE_GET_PRIVATE (cell,
      EMPATHY_TYPE_CELL_RENDERER_ACTIVATABLE,
      EmpathyCellRendererActivatablePriv);

  g_object_set (cell,
      "xpad", 0,
      "ypad", 0,
      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
      "follow-state", TRUE,
      NULL);
}

static void
cell_renderer_activatable_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyCellRendererActivatablePriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
      case PROP_SHOW_ON_SELECT:
        g_value_set_boolean (value, priv->show_on_select);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
cell_renderer_activatable_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyCellRendererActivatablePriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
      case PROP_SHOW_ON_SELECT:
        priv->show_on_select = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

GtkCellRenderer *
empathy_cell_renderer_activatable_new (void)
{
  return g_object_new (EMPATHY_TYPE_CELL_RENDERER_ACTIVATABLE, NULL);
}

static gboolean
cell_renderer_activatable_activate (GtkCellRenderer      *cell,
    GdkEvent *event,
    GtkWidget *widget,
    const gchar *path_string,
    GdkRectangle *background_area,
    GdkRectangle *cell_area,
    GtkCellRendererState  flags)
{
  EmpathyCellRendererActivatable *activatable;
  gint ex, ey, bx, by, bw, bh;

  activatable = EMPATHY_CELL_RENDERER_ACTIVATABLE (cell);

  if (!GTK_IS_TREE_VIEW (widget) || event == NULL ||
      event->type != GDK_BUTTON_PRESS) {
    return FALSE;
  }

  ex  = (gint) ((GdkEventButton *) event)->x;
  ey  = (gint) ((GdkEventButton *) event)->y;
  bx = background_area->x;
  by = background_area->y;
  bw = background_area->width;
  bh = background_area->height;

  if (ex < bx || ex > (bx+bw) || ey < by || ey > (by+bh)){
    /* Click wasn't on the icon */
    return FALSE;
  }

  g_signal_emit (activatable, signals[PATH_ACTIVATED], 0, path_string);

  return TRUE;
}

/* copied from gtkcellrendererpixbuf.c */

static GdkPixbuf *
create_colorized_pixbuf (GdkPixbuf *src,
    GdkColor  *new_color)
{
  gint i, j;
  gint width, height, has_alpha, src_row_stride, dst_row_stride;
  gint red_value, green_value, blue_value;
  guchar *target_pixels;
  guchar *original_pixels;
  guchar *pixsrc;
  guchar *pixdest;
  GdkPixbuf *dest;

  red_value = new_color->red / 255.0;
  green_value = new_color->green / 255.0;
  blue_value = new_color->blue / 255.0;

  dest = gdk_pixbuf_new (gdk_pixbuf_get_colorspace (src),
      gdk_pixbuf_get_has_alpha (src),
      gdk_pixbuf_get_bits_per_sample (src),
      gdk_pixbuf_get_width (src),
      gdk_pixbuf_get_height (src));

  has_alpha = gdk_pixbuf_get_has_alpha (src);
  width = gdk_pixbuf_get_width (src);
  height = gdk_pixbuf_get_height (src);
  src_row_stride = gdk_pixbuf_get_rowstride (src);
  dst_row_stride = gdk_pixbuf_get_rowstride (dest);
  target_pixels = gdk_pixbuf_get_pixels (dest);
  original_pixels = gdk_pixbuf_get_pixels (src);

  for (i = 0; i < height; i++)
    {
      pixdest = target_pixels + i*dst_row_stride;
      pixsrc = original_pixels + i*src_row_stride;
      for (j = 0; j < width; j++)
        {
          *pixdest++ = (*pixsrc++ * red_value) >> 8;
          *pixdest++ = (*pixsrc++ * green_value) >> 8;
          *pixdest++ = (*pixsrc++ * blue_value) >> 8;
          if (has_alpha)
            *pixdest++ = *pixsrc++;
        }
    }

  return dest;
}

static void
cell_renderer_activatable_render (
    GtkCellRenderer      *cell,
    GdkWindow            *window,
    GtkWidget            *widget,
    GdkRectangle         *background_area,
    GdkRectangle         *cell_area,
    GdkRectangle         *expose_area,
    GtkCellRendererState  flags)
{
  GdkPixbuf *pixbuf;
  GdkPixbuf *invisible = NULL;
  GdkPixbuf *colorized = NULL;
  GdkRectangle pix_rect;
  GdkRectangle draw_rect;
  GtkStyle *style;
  gboolean follow_state;
  cairo_t *cr;
  int xpad, ypad;
  EmpathyCellRendererActivatablePriv *priv = GET_PRIV (cell);

  if (priv->show_on_select && !(flags & (GTK_CELL_RENDERER_SELECTED)))
    return;

  g_object_get (cell,
      "follow-state", &follow_state,
      "xpad", &xpad,
      "ypad", &ypad,
      "pixbuf", &pixbuf,
      NULL);
  style = gtk_widget_get_style (widget);

  gtk_cell_renderer_get_size (cell, widget, cell_area,
      &pix_rect.x,
      &pix_rect.y,
      &pix_rect.width,
      &pix_rect.height);

  pix_rect.x += cell_area->x + xpad;
  pix_rect.y += cell_area->y + ypad;
  pix_rect.width  -= xpad * 2;
  pix_rect.height -= ypad * 2;

  if (!gdk_rectangle_intersect (cell_area, &pix_rect, &draw_rect) ||
      !gdk_rectangle_intersect (expose_area, &draw_rect, &draw_rect))
    return;

  if (pixbuf == NULL)
    return;

  if (GTK_WIDGET_STATE (widget) == GTK_STATE_INSENSITIVE || !cell->sensitive)
    {
      GtkIconSource *source;

      source = gtk_icon_source_new ();
      gtk_icon_source_set_pixbuf (source, pixbuf);
      /* The size here is arbitrary; since size isn't
       * wildcarded in the source, it isn't supposed to be
       * scaled by the engine function
       */
      gtk_icon_source_set_size (source, GTK_ICON_SIZE_SMALL_TOOLBAR);
      gtk_icon_source_set_size_wildcarded (source, FALSE);

      invisible = gtk_style_render_icon (style,
          source,
          gtk_widget_get_direction (widget),
          GTK_STATE_INSENSITIVE,
          /* arbitrary */
          (GtkIconSize)-1,
          widget,
          "gtkcellrendererpixbuf");

      gtk_icon_source_free (source);

      pixbuf = invisible;
    }
  else if (follow_state &&
      (flags & (GTK_CELL_RENDERER_SELECTED|GTK_CELL_RENDERER_PRELIT)) != 0)
    {
      GtkStateType state;

      if ((flags & GTK_CELL_RENDERER_SELECTED) != 0)
	{
	  if (GTK_WIDGET_HAS_FOCUS (widget))
	    state = GTK_STATE_SELECTED;
	  else
	    state = GTK_STATE_ACTIVE;
	}
      else
	state = GTK_STATE_PRELIGHT;

      colorized = create_colorized_pixbuf (pixbuf,
          &style->base[state]);

      pixbuf = colorized;
    }

  cr = gdk_cairo_create (window);

  gdk_cairo_set_source_pixbuf (cr, pixbuf, pix_rect.x, pix_rect.y);
  gdk_cairo_rectangle (cr, &draw_rect);
  cairo_fill (cr);

  cairo_destroy (cr);

  if (invisible != NULL)
    g_object_unref (invisible);

  if (colorized != NULL)
    g_object_unref (colorized);
}

static void
empathy_cell_renderer_activatable_class_init (
    EmpathyCellRendererActivatableClass *klass)
{
  GtkCellRendererClass *cell_class;
  GObjectClass *oclass;

  oclass = G_OBJECT_CLASS (klass);
  oclass->get_property = cell_renderer_activatable_get_property;
  oclass->set_property = cell_renderer_activatable_set_property;

  cell_class = GTK_CELL_RENDERER_CLASS (klass);
  cell_class->activate = cell_renderer_activatable_activate;
  cell_class->render = cell_renderer_activatable_render;

  signals[PATH_ACTIVATED] =
    g_signal_new ("path-activated",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE,
        1, G_TYPE_STRING);

  g_object_class_install_property (oclass, PROP_SHOW_ON_SELECT,
      g_param_spec_boolean ("show-on-select",
          "Show on select",
          "Whether the cell renderer should be shown only when it's selected",
          FALSE,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  g_type_class_add_private (klass,
      sizeof (EmpathyCellRendererActivatablePriv));
}
