/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004 Imendio AB
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 */

#ifndef __EMPATHY_TIME_H__
#define __EMPATHY_TIME_H__

#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif
#include <time.h>

#include <glib.h>

G_BEGIN_DECLS

/*
 * Translators: use your locale preferred time format.
 * The fields follow the strftime standard:
 * look at the manual if you need help (man strftime)
 */
#define EMPATHY_TIME_FORMAT_DISPLAY_SHORT _("%H:%M")
#define EMPATHY_DATE_FORMAT_DISPLAY_SHORT  _("%a %d %b %Y")
#define EMPATHY_TIME_DATE_FORMAT_DISPLAY_SHORT _("%a %d %b %Y, %H:%M")

time_t  empathy_time_get_current     (void);
time_t  empathy_time_get_local_time  (struct tm   *tm);
time_t  empathy_time_parse           (const gchar *str);
gchar  *empathy_time_to_string_utc   (time_t       t,
				      const gchar *format);
gchar  *empathy_time_to_string_local (time_t       t,
				      const gchar *format);
gchar  *empathy_time_to_string_relative (time_t t);

G_END_DECLS

#endif /* __EMPATHY_TIME_H__ */

