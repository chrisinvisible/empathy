/*
 * Copyright (C) 2010 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 */

#ifndef __EMPATHY_STRING_PARSER_H__
#define __EMPATHY_STRING_PARSER_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _EmpathyStringParser EmpathyStringParser;

typedef void (*EmpathyStringReplace) (const gchar *text,
				      gssize len,
				      gpointer match_data,
				      gpointer user_data);
typedef void (*EmpathyStringMatch) (const gchar *text,
				    gssize len,
				    EmpathyStringReplace replace_func,
				    EmpathyStringParser *sub_parsers,
				    gpointer user_data);

struct _EmpathyStringParser {
	EmpathyStringMatch match_func;
	EmpathyStringReplace replace_func;
};

void
empathy_string_parser_substr (const gchar *text,
			      gssize len,
			      EmpathyStringParser *parsers,
			      gpointer user_data);

void
empathy_string_match_link (const gchar *text,
			   gssize len,
			   EmpathyStringReplace replace_func,
			   EmpathyStringParser *sub_parsers,
			   gpointer user_data);

void
empathy_string_match_smiley (const gchar *text,
			     gssize len,
			     EmpathyStringReplace replace_func,
			     EmpathyStringParser *sub_parsers,
			     gpointer user_data);

void
empathy_string_match_all (const gchar *text,
			  gssize len,
			  EmpathyStringReplace replace_func,
			  EmpathyStringParser *sub_parsers,
			  gpointer user_data);

/* Replace functions assume user_data is a GString */
void
empathy_string_replace_link (const gchar *text,
                             gssize len,
                             gpointer match_data,
                             gpointer user_data);

void
empathy_string_replace_escaped (const gchar *text,
				gssize len,
				gpointer match_data,
				gpointer user_data);

/* Returns a new string with <a> html tag around links, and escape the rest.
 * To be used with gtk_label_set_markup() for example */
gchar *
empathy_add_link_markup (const gchar *text);

G_END_DECLS

#endif /*  __EMPATHY_STRING_PARSER_H__ */
