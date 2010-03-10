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

#include <config.h>

#include <string.h>

#include "empathy-string-parser.h"
#include "empathy-smiley-manager.h"
#include "empathy-ui-utils.h"

#define SCHEMES           "([a-zA-Z\\+]+)"
#define INVALID_CHARS     "\\s\"'"
#define INVALID_CHARS_EXT INVALID_CHARS "\\[\\]<>(){},;:?"
#define BODY              "([^"INVALID_CHARS"]+)"
#define BODY_END          "([^"INVALID_CHARS"]*)[^"INVALID_CHARS_EXT".]"
#define BODY_STRICT       "([^"INVALID_CHARS_EXT"]+)"
#define URI_REGEX         "("SCHEMES"://"BODY_END")" \
		          "|((www|ftp)\\."BODY_END")" \
		          "|((mailto:)?"BODY_STRICT"@"BODY"\\."BODY_END")"

static GRegex *
uri_regex_dup_singleton (void)
{
	static GRegex *uri_regex = NULL;

	/* We intentionally leak the regex so it's not recomputed */
	if (!uri_regex) {
		uri_regex = g_regex_new (URI_REGEX, 0, 0, NULL);
	}

	return g_regex_ref (uri_regex);
}

void
empathy_string_parser_substr (const gchar *text,
			      gssize len,
			      EmpathyStringParser *parsers,
			      gpointer user_data)
{
	if (parsers != NULL && parsers[0].match_func != NULL) {
		parsers[0].match_func (text, len,
				       parsers[0].replace_func, parsers + 1,
				       user_data);
	}
}

void
empathy_string_match_link (const gchar *text,
			   gssize len,
			   EmpathyStringReplace replace_func,
			   EmpathyStringParser *sub_parsers,
			   gpointer user_data)
{
	GRegex     *uri_regex;
	GMatchInfo *match_info;
	gboolean    match;
	gint        last = 0;

	uri_regex = uri_regex_dup_singleton ();
	match = g_regex_match_full (uri_regex, text, len, 0, 0, &match_info, NULL);
	if (match) {
		gint s = 0, e = 0;

		do {
			g_match_info_fetch_pos (match_info, 0, &s, &e);

			if (s > last) {
				/* Append the text between last link (or the
				 * start of the message) and this link */
				empathy_string_parser_substr (text + last,
							      s - last,
							      sub_parsers,
							      user_data);
			}

			replace_func (text + s, e - s, NULL, user_data);

			last = e;
		} while (g_match_info_next (match_info, NULL));
	}

	empathy_string_parser_substr (text + last, len - last,
				      sub_parsers, user_data);

	g_match_info_free (match_info);
	g_regex_unref (uri_regex);
}

void
empathy_string_match_smiley (const gchar *text,
			     gssize len,
			     EmpathyStringReplace replace_func,
			     EmpathyStringParser *sub_parsers,
			     gpointer user_data)
{
	guint last = 0;
	EmpathySmileyManager *smiley_manager;
	GSList *hits, *l;

	smiley_manager = empathy_smiley_manager_dup_singleton ();
	hits = empathy_smiley_manager_parse_len (smiley_manager, text, len);

	for (l = hits; l; l = l->next) {
		EmpathySmileyHit *hit = l->data;

		if (hit->start > last) {
			/* Append the text between last smiley (or the
			 * start of the message) and this smiley */
			empathy_string_parser_substr (text + last,
						      hit->start - last,
						      sub_parsers, user_data);
		}

		replace_func (text + hit->start, hit->end - hit->start,
			      hit, user_data);

		last = hit->end;

		empathy_smiley_hit_free (hit);
	}
	g_slist_free (hits);
	g_object_unref (smiley_manager);

	empathy_string_parser_substr (text + last, len - last,
				      sub_parsers, user_data);
}

void
empathy_string_match_all (const gchar *text,
			  gssize len,
			  EmpathyStringReplace replace_func,
			  EmpathyStringParser *sub_parsers,
			  gpointer user_data)
{
	replace_func (text, len, NULL, user_data);
}

void
empathy_string_replace_link (const gchar *text,
                             gssize len,
                             gpointer match_data,
                             gpointer user_data)
{
	GString *string = user_data;
	gchar *real_url;
	gchar *escaped;

	real_url = empathy_make_absolute_url_len (text, len);

	/* The thing we are making a link of may contain
	 * characters which need escaping */
	escaped = g_markup_escape_text (text, len);

	/* Append the link inside <a href=""></a> tag */
	g_string_append_printf (string, "<a href=\"%s\">%s</a>",
				real_url, escaped);

	g_free (real_url);
	g_free (escaped);
}

void
empathy_string_replace_escaped (const gchar *text,
				gssize len,
				gpointer match_data,
				gpointer user_data)
{
	GString *string = user_data;
	gchar *escaped;

	escaped = g_markup_escape_text (text, len);
	g_string_append (string, escaped);
	g_free (escaped);
}

gchar *
empathy_add_link_markup (const gchar *text)
{
	EmpathyStringParser parsers[] = {
		{empathy_string_match_link, empathy_string_replace_link},
		{empathy_string_match_all, empathy_string_replace_escaped},
		{NULL, NULL}
	};
	GString *string;

	g_return_val_if_fail (text != NULL, NULL);

	/* GtkLabel with links could make infinite loop because of
	 * GNOME bug #612066. It is fixed in GTK >= 2.18.8 and GTK >= 2.19.7.
	 * FIXME: Remove this check once we have an hard dep on GTK 2.20 */
	if (gtk_check_version (2, 18, 8) != NULL ||
	    (gtk_minor_version == 19 && gtk_micro_version < 7)) {
		return g_markup_escape_text (text, -1);
	}

	string = g_string_sized_new (strlen (text));
	empathy_string_parser_substr (text, -1, parsers, string);

	return g_string_free (string, FALSE);
}

