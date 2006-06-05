/*
 *      document.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2006 Enrico Troeger <enrico.troeger@uvena.de>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $Id$
 */

#include "geany.h"

#ifdef TIME_WITH_SYS_TIME
# include <time.h>
# include <sys/time.h>
#endif
#include <unistd.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#include "document.h"
#include "callbacks.h"
#include "support.h"
#include "sciwrappers.h"
#include "sci_cb.h"
#include "dialogs.h"
#include "msgwindow.h"
#include "callbacks.h"
#include "templates.h"
#include "treeviews.h"
#include "utils.h"



/* returns the index of the notebook page which has the given filename */
gint document_find_by_filename(const gchar *filename)
{
	guint i;

	if (! filename) return -1;

	for(i = 0; i < GEANY_MAX_OPEN_FILES; i++)
	{
#ifdef GEANY_WIN32
		// ignore the case of filenames and paths under WIN32, causes errors if not
		if (doc_list[i].file_name && ! strcasecmp(doc_list[i].file_name, filename)) return i;
#else
		if (doc_list[i].file_name && utils_strcmp(doc_list[i].file_name, filename)) return i;
#endif
	}
	return -1;
}


/* returns the index of the notebook page which has sci */
gint document_find_by_sci(ScintillaObject *sci)
{
	guint i;

	if (! sci) return -1;

	for(i = 0; i < GEANY_MAX_OPEN_FILES; i++)
	{
		if (doc_list[i].is_valid && doc_list[i].sci == sci) return i;
	}
	return -1;
}


/* returns the index of the given notebook page in the document list */
gint document_get_n_idx(guint page_num)
{
	ScintillaObject *sci;
	if (page_num >= GEANY_MAX_OPEN_FILES) return -1;

	sci = (ScintillaObject*)gtk_notebook_get_nth_page(
				GTK_NOTEBOOK(app->notebook), page_num);

	return document_find_by_sci(sci);
}


/* returns the index of the current notebook page in the document list */
gint document_get_cur_idx(void)
{
	gint cur_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
	ScintillaObject *sci = (ScintillaObject*)gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), cur_page);

	if (cur_page == -1)
		return -1;
	else
		return document_find_by_sci(sci);
}


/* returns the next free place(i.e. index) in the document list
 * If there is for any reason no free place, -1 is returned
 */
gint document_get_new_idx(void)
{
	guint i;

	for(i = 0; i < GEANY_MAX_OPEN_FILES; i++)
	{
		if (doc_list[i].sci == NULL)
		{
			return (gint) i;
		}
	}
	return -1;
}


/* changes the color of the tab text according to the status */
void document_change_tab_color(gint index)
{
	if (index >= 0 && doc_list[index].sci)
	{
		GdkColor colorred = {0, 65535, 0, 0};
		GdkColor colorblack = {0, 0, 0, 0};

		gtk_widget_modify_fg(doc_list[index].tab_label, GTK_STATE_NORMAL,
					(doc_list[index].changed) ? &colorred : &colorblack);
		gtk_widget_modify_fg(doc_list[index].tab_label, GTK_STATE_ACTIVE,
					(doc_list[index].changed) ? &colorred : &colorblack);
		gtk_widget_modify_fg(doc_list[index].tabmenu_label, GTK_STATE_PRELIGHT,
					(doc_list[index].changed) ? &colorred : &colorblack);
		gtk_widget_modify_fg(doc_list[index].tabmenu_label, GTK_STATE_NORMAL,
					(doc_list[index].changed) ? &colorred : &colorblack);
	}
}


void document_set_text_changed(gint index)
{
	document_change_tab_color(index);
	utils_save_buttons_toggle(doc_list[index].changed);
	utils_set_window_title(index);
}


/* sets in all document structs the flag is_valid to FALSE and initializes some members to NULL,
 * to mark it uninitialized. The flag is_valid is set to TRUE in document_create_new_sci(). */
void document_init_doclist(void)
{
	gint i;

	for (i = 0; i < GEANY_MAX_OPEN_FILES; i++)
	{
		doc_list[i].is_valid = FALSE;
		doc_list[i].has_tags = FALSE;
		doc_list[i].use_auto_indention = TRUE;
		doc_list[i].line_breaking = TRUE;
		doc_list[i].readonly = FALSE;
		doc_list[i].tag_store = NULL;
		doc_list[i].tag_tree = NULL;
		doc_list[i].file_name = NULL;
		doc_list[i].file_type = NULL;
		doc_list[i].tm_file = NULL;
		doc_list[i].encoding = NULL;
		doc_list[i].sci = NULL;
	}
}


/* creates a new tab in the notebook and does all related stuff
 * finally it returns the index of the created document */
gint document_create_new_sci(const gchar *filename)
{
	ScintillaObject	*sci;
	GtkWidget *hbox, *but;
	GtkWidget *align;
	PangoFontDescription *pfd;
	gchar *title, *fname;
	document this;
	GtkTreeIter iter;
	gint new_idx = document_get_new_idx();

	title = (filename) ? g_path_get_basename(filename) : g_strdup(GEANY_STRING_UNTITLED);
	this.tab_label = gtk_label_new(title);
	//gtk_widget_show(this.tab_label);

	hbox = gtk_hbox_new(FALSE, 0);
	but = gtk_button_new();
	gtk_container_add(GTK_CONTAINER(but), utils_new_image_from_inline(GEANY_IMAGE_SMALL_CROSS, FALSE));
	gtk_container_set_border_width(GTK_CONTAINER(but), 0);
	gtk_widget_set_size_request(but, 17, 15);

	align = gtk_alignment_new(1.0, 0.0, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), but);

	gtk_button_set_relief(GTK_BUTTON(but), GTK_RELIEF_NONE);
	gtk_box_pack_start(GTK_BOX(hbox), this.tab_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), align, TRUE, TRUE, 0);
	gtk_widget_show_all(hbox);


	/* SCI - Code */
	sci = SCINTILLA(scintilla_new());
	scintilla_set_id(sci, new_idx);

	gtk_widget_show(GTK_WIDGET(sci));

#ifdef GEANY_WIN32
	sci_set_codepage(sci, 0);
#else
	sci_set_codepage(sci, SC_CP_UTF8);
#endif
	//SSM(sci, SCI_SETWRAPSTARTINDENT, 4, 0);
	// disable scintilla provided popup menu
	sci_use_popup(sci, FALSE);
	sci_assign_cmdkey(sci, SCK_HOME, SCI_VCHOMEWRAP);
	sci_assign_cmdkey(sci, SCK_END,  SCI_LINEENDWRAP);
	sci_set_mark_long_lines(sci, app->long_line_column, app->long_line_color);
	sci_set_symbol_margin(sci, app->show_markers_margin);
	sci_set_folding_margin_visible(sci, app->pref_editor_folding);
	sci_set_line_numbers(sci, app->show_linenumber_margin, 0);
	sci_set_lines_wrapped(sci, app->pref_editor_line_breaking);
	sci_set_indentionguides(sci, app->pref_editor_show_indent_guide);
	sci_set_visible_white_spaces(sci, app->pref_editor_show_white_space);
	sci_set_visible_eols(sci, app->pref_editor_show_line_endings);
	pfd = pango_font_description_from_string(app->editor_font);
	fname = g_strdup_printf("!%s", pango_font_description_get_family(pfd));
	document_set_font(sci, fname, pango_font_description_get_size(pfd) / PANGO_SCALE);
	pango_font_description_free(pfd);
	g_free(fname);

	// this is only for testing, should be tested seriously, could speed up scrolling
	if (alternative_scrolling)
	{
		SSM(sci, SCI_SETBUFFEREDDRAW, 0, 0);
		SSM(sci, SCI_SETTWOPHASEDRAW, 0, 0);
	}

	this.tabmenu_label = gtk_label_new(title);
	gtk_misc_set_alignment(GTK_MISC(this.tabmenu_label), 0.0, 0);
	if (app->tab_order_ltr)
	{
		gint npage;
		npage = gtk_notebook_append_page_menu(GTK_NOTEBOOK(app->notebook), GTK_WIDGET(sci),
																	hbox, this.tabmenu_label);
		gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), npage);
	}
	else
	{
		gtk_notebook_insert_page_menu(GTK_NOTEBOOK(app->notebook), GTK_WIDGET(sci), hbox,
																		this.tabmenu_label, 0);
		gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
	}
	iter = treeviews_openfiles_add(new_idx, title);
	g_free(title);

	this.tag_store = NULL;
	this.tag_tree = NULL;

	// "the" SCI signal
	g_signal_connect((GtkWidget*) sci, "sci-notify",
					G_CALLBACK(on_editor_notification), GINT_TO_POINTER(new_idx));
	// signal for insert-key(works without too, but to update the right status bar)
/*	g_signal_connect((GtkWidget*) sci, "key-press-event",
					G_CALLBACK(keybindings_got_event), GINT_TO_POINTER(new_idx));
*/	// signal for the popup menu
	g_signal_connect((GtkWidget*) sci, "button-press-event",
					G_CALLBACK(on_editor_button_press_event), GINT_TO_POINTER(new_idx));
	// signal for clicking the tab-close button
	g_signal_connect(G_OBJECT(but), "clicked", G_CALLBACK(on_tab_close_clicked), sci);

	utils_close_buttons_toggle();

	// store important pointers in the tab list
	this.file_name = (filename) ? g_strdup(filename) : NULL;
	this.sci = sci;
	this.encoding = NULL;
	this.tm_file = NULL;
	this.iter = iter;
	this.file_type = NULL;
	this.mtime = 0;
	this.changed = FALSE;
	this.last_check = time(NULL);
	this.do_overwrite = FALSE;
	this.readonly = FALSE;
	this.line_breaking = TRUE;
	this.use_auto_indention = TRUE;
	this.has_tags = FALSE;
	this.is_valid = TRUE;
	doc_list[new_idx] = this;

	return new_idx;
}


/* removes the given notebook tab and clears the related entry in the document list */
gboolean document_remove(guint page_num)
{
	gint idx = document_get_n_idx(page_num);

	if (idx >= 0 && idx <= GEANY_MAX_OPEN_FILES)
	{
		if (doc_list[idx].changed && ! dialogs_show_unsaved_file(idx))
		{
			return FALSE;
		}
		gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page_num);
		treeviews_openfiles_remove(doc_list[idx].iter);
		if (GTK_IS_WIDGET(doc_list[idx].tag_tree))
		{
			//g_object_unref(doc_list[idx].tag_tree); // no need to unref when destroying?
			gtk_widget_destroy(doc_list[idx].tag_tree);
		}
		msgwin_status_add(_("File %s closed."),
				(doc_list[idx].file_name) ? doc_list[idx].file_name : GEANY_STRING_UNTITLED);
		g_free(doc_list[idx].encoding);
		g_free(doc_list[idx].file_name);
		tm_workspace_remove_object(doc_list[idx].tm_file, TRUE);
		doc_list[idx].is_valid = FALSE;
		doc_list[idx].sci = NULL;
		doc_list[idx].file_name = NULL;
		doc_list[idx].file_type = NULL;
		doc_list[idx].encoding = NULL;
		doc_list[idx].tm_file = NULL;
		if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) == 0)
		{
			utils_update_tag_list(-1, FALSE);
			//on_notebook1_switch_page(GTK_NOTEBOOK(app->notebook), NULL, 0, NULL);
			utils_set_window_title(-1);
			utils_save_buttons_toggle(FALSE);
			utils_close_buttons_toggle();
			utils_build_show_hide(-1);
		}
	}
	else geany_debug("Error: idx: %d page_num: %d", idx, page_num);

	return TRUE;
}


/* This creates a new document, by clearing the text widget and setting the
   current filename to NULL. */
void document_new_file(filetype *ft)
{
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) < GEANY_MAX_OPEN_FILES)
	{
		gint idx = document_create_new_sci(NULL);
		gchar *template = document_prepare_template(ft);

		sci_clear_all(doc_list[idx].sci);
		sci_set_text(doc_list[idx].sci, template);
		g_free(template);

		doc_list[idx].encoding = g_strdup("UTF-8");
		document_set_filetype(idx, ft);
		utils_set_window_title(idx);
		utils_build_show_hide(idx);
		utils_update_tag_list(idx, FALSE);
		doc_list[idx].mtime = time(NULL);
		doc_list[idx].changed = FALSE;
		document_set_text_changed(idx);
		sci_set_eol_mode(doc_list[idx].sci, SC_EOL_LF);
		sci_set_line_numbers(doc_list[idx].sci, app->show_linenumber_margin, 0);
		sci_empty_undo_buffer(doc_list[idx].sci);
		sci_goto_pos(doc_list[idx].sci, 0, TRUE);

		msgwin_status_add(_("New file opened."));
	}
	else
	{
		dialogs_show_file_open_error();
	}
}


/* If idx is set to -1, it creates a new tab, opens the file from filename and
 * set the cursor to pos.
 * If idx is greater than -1, it reloads the file in the tab corresponding to
 * idx and set the cursor to position 0. In this case, filename should be NULL
 */
void document_open_file(gint idx, const gchar *filename, gint pos, gboolean readonly, filetype *ft)
{
	gint editor_mode;
	gsize size;
	gboolean reload = (idx == -1) ? FALSE : TRUE;
	struct stat st;
	gchar *enc = NULL;
	gchar *utf8_filename = NULL;
	gchar *locale_filename = NULL;
	GError *err = NULL;
	gchar *data = NULL;

	//struct timeval tv, tv1;
	//struct timezone tz;
	//gettimeofday(&tv, &tz);

	if (reload)
	{
		utf8_filename = g_strdup(doc_list[idx].file_name);
		locale_filename = g_locale_from_utf8(utf8_filename, -1, NULL, NULL, NULL);
	}
	else
	{
		// filename must not be NULL when it is a new file
		if (filename == NULL)
		{
			msgwin_status_add(_("Invalid filename"));
			return;
		}

		// try to get the UTF-8 equivalent for the filename, fallback to filename if error
		locale_filename = g_strdup(filename);
		utf8_filename = g_locale_to_utf8(locale_filename, -1, NULL, NULL, &err);
		if (utf8_filename == NULL)
		{
			if (err != NULL) msgwin_status_add("%s (%s)", _("Invalid filename"), err->message);
			else msgwin_status_add(_("Invalid filename"));
			utf8_filename = g_strdup(locale_filename);
			g_error_free(err);
			err = NULL;	// set to NULL for further usage
		}

		// if file is already open, switch to it and go
		idx = document_find_by_filename(utf8_filename);
		if (idx >= 0)
		{
			gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook),
					gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook),
					(GtkWidget*) doc_list[idx].sci));
			g_free(utf8_filename);
			g_free(locale_filename);
			return;
		}
	}

	if (stat(locale_filename, &st) != 0)
	{
		msgwin_status_add(_("Could not open file %s (%s)"), utf8_filename, g_strerror(errno));
		g_free(utf8_filename);
		g_free(locale_filename);
		return;
	}

#ifdef GEANY_WIN32
	if (! g_file_get_contents(utf8_filename, &data, &size, &err))
#else
	if (! g_file_get_contents(locale_filename, &data, &size, &err))
#endif
	{
		//msgwin_status_add(_("Could not open file %s (%s)"), utf8_filename, g_strerror(err->code));
		msgwin_status_add(err->message);
		g_error_free(err);
		g_free(utf8_filename);
		g_free(locale_filename);
		return;
	}

	/* Determine character encoding and convert to utf-8*/
	if (size > 0)
	{
		if (g_utf8_validate(data, size, NULL))
		{
			enc = g_strdup("UTF-8");
		}
		else
		{
			gchar *converted_text = utils_convert_to_utf8(data, size, &enc);

			if (converted_text == NULL)
			{
				msgwin_status_add(_("The file does not look like a text file or the file encoding is not supported."));
				utils_beep();
				g_free(data);
				g_free(utf8_filename);
				g_free(locale_filename);
				return;
			}
			else
			{
				g_free(data);
				data = (void*)converted_text;
				size = strlen(converted_text);
			}
		}
	}
	else
	{
		enc = g_strdup("UTF-8");
	}

	if (! reload) idx = document_create_new_sci(utf8_filename);

	// set editor mode and add the text to the ScintillaObject
	sci_set_text(doc_list[idx].sci, data); // NULL terminated data; avoids Unsaved
	editor_mode = utils_get_line_endings(data, size);
	sci_set_eol_mode(doc_list[idx].sci, editor_mode);

	sci_set_line_numbers(doc_list[idx].sci, app->show_linenumber_margin, 0);
	sci_set_savepoint(doc_list[idx].sci);
	sci_empty_undo_buffer(doc_list[idx].sci);
	// get the modification time from file and keep it
	doc_list[idx].mtime = st.st_mtime;
	doc_list[idx].changed = FALSE;
	doc_list[idx].file_name = g_strdup(utf8_filename);
	doc_list[idx].encoding = enc;
	if (reload)
	{
		sci_goto_pos(doc_list[idx].sci, 0, FALSE);
		msgwin_status_add(_("File %s reloaded."), utf8_filename);
	}
	else
	{
		filetype *use_ft = (ft != NULL) ? ft : filetypes_get_from_filename(utf8_filename);

		sci_goto_pos(doc_list[idx].sci, pos, TRUE);

		if (readonly) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
							lookup_widget(app->window, "set_file_readonly1")), TRUE);
		/* This is so ugly, but the checkbox in the file menu can be still checked from a previous
		 * read only file, a won't be checked out since notebook-switch-signal is ignored
		 * so we need to check it out by hand and this emits the toggled-signal and therefore
		 * we must set the file to read only and the toggle-callback will set it again to not
		 * read only. It's ugly. Any ideas are welcome. */
		else if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(
								lookup_widget(app->window, "set_file_readonly1"))))
		{
			doc_list[idx].readonly = TRUE;
			sci_set_readonly(doc_list[idx].sci, TRUE);
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
							lookup_widget(app->window, "set_file_readonly1")), FALSE);
		}

		document_set_filetype(idx, use_ft);
		utils_build_show_hide(idx);
		msgwin_status_add(_("File %s opened(%d%s)."),
				utf8_filename, gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)),
				(readonly) ? _(", read-only") : "");
	}
	document_set_text_changed(idx);

	g_free(data);


	// finally add current file to recent files menu, but not the files from the last session
	if (! app->opening_session_files &&
		g_queue_find_custom(app->recent_queue, utf8_filename, (GCompareFunc) strcmp) == NULL)
	{
		g_queue_push_head(app->recent_queue, g_strdup(utf8_filename));
		if (g_queue_get_length(app->recent_queue) > app->mru_length)
		{
			g_free(g_queue_pop_tail(app->recent_queue));
		}
		utils_update_recent_menu();
	}

	g_free(utf8_filename);
	g_free(locale_filename);
	//gettimeofday(&tv1, &tz);
	//geany_debug("%s: %d", filename, (gint)(tv1.tv_usec - tv.tv_usec));
}


/* This saves the file */
void document_save_file(gint idx)
{
	gchar *data;
	FILE *fp;
	gint bytes_written, len;
	gchar *locale_filename = NULL;

	if (idx == -1) return;

	if (doc_list[idx].file_name == NULL)
	{
		msgwin_status_add(_("Error saving file."));
		utils_beep();
		return;
	}

	// strip trailing spaces
	if (app->pref_editor_trail_space) utils_strip_trailing_spaces(idx);
	// ensure the file has a newline at the end
	if (app->pref_editor_new_line) utils_ensure_final_newline(idx);
	// ensure there a really the same EOL chars
	sci_convert_eols(doc_list[idx].sci, sci_get_eol_mode(doc_list[idx].sci));

	len = sci_get_length(doc_list[idx].sci) + 1;
	data = (gchar*) g_malloc(len);
	sci_get_text(doc_list[idx].sci, len, data);

	locale_filename = g_locale_from_utf8(doc_list[idx].file_name, -1, NULL, NULL, NULL);
	fp = fopen(locale_filename, "w");
	if (fp == NULL)
	{
		msgwin_status_add(_("Error saving file (%s)."), strerror(errno));
		utils_beep();
		g_free(data);
		return;
	}

#ifdef GEANY_WIN32
	// ugly hack: on windows '\n' (LF) is taken as CRLF so we must convert it prior to save the doc
	if (sci_get_eol_mode(doc_list[idx].sci) == SC_EOL_CRLF) sci_convert_eols(doc_list[idx].sci, SC_EOL_CRLF);
#endif

	len = strlen(data);
	bytes_written = fwrite(data, sizeof (gchar), len, fp);
	fclose (fp);

	g_free(data);

	if (len != bytes_written)
	{
		msgwin_status_add(_("Error saving file."));
		utils_beep();
		return;
	}

	// ignore the following things if we are quitting
	if (! app->quitting)
	{
		gchar *basename = g_path_get_basename(doc_list[idx].file_name);

		// set line numbers again, to reset the margin width, if
		// there are more lines than before
		sci_set_line_numbers(doc_list[idx].sci, app->show_linenumber_margin, 0);
		sci_set_savepoint(doc_list[idx].sci);
		doc_list[idx].mtime = time(NULL);
		if (doc_list[idx].file_type == NULL || doc_list[idx].file_type->id == GEANY_FILETYPES_ALL)
			doc_list[idx].file_type = filetypes_get_from_filename(doc_list[idx].file_name);
		document_set_filetype(idx, doc_list[idx].file_type);
		tm_workspace_update(TM_WORK_OBJECT(app->tm_workspace), TRUE, TRUE, FALSE);
		gtk_label_set_text(GTK_LABEL(doc_list[idx].tab_label), basename);
		gtk_label_set_text(GTK_LABEL(doc_list[idx].tabmenu_label), basename);
		treeviews_openfiles_update(doc_list[idx].iter, doc_list[idx].file_name);
		msgwin_status_add(_("File %s saved."), doc_list[idx].file_name);
		utils_update_statusbar(idx, -1);
		treeviews_openfiles_update(doc_list[idx].iter, basename);
		g_free(basename);
	}
}


/* special search function, used from the find entry in the toolbar */
void document_find_next(gint idx, const gchar *text, gint flags, gboolean find_button, gboolean inc)
{
	gint selection_end, search_pos;

	if (idx == -1 || ! strlen(text)) return;

	selection_end =  sci_get_selection_end(doc_list[idx].sci);
	if (!inc && sci_can_copy(doc_list[idx].sci))
	{ // there's a selection so go to the end
		sci_goto_pos(doc_list[idx].sci, selection_end, TRUE);
	}

	sci_set_search_anchor(doc_list[idx].sci);
	search_pos = sci_search_next(doc_list[idx].sci, flags, text);
	if (search_pos != -1)
	{
		sci_scroll_caret(doc_list[idx].sci);
	}
	else
	{
		if (find_button)
		{
			if (dialogs_show_not_found(text))
			{
				sci_goto_pos(doc_list[idx].sci, 0, FALSE);
				document_find_next(idx, text, flags, TRUE, inc);
			}
		}
		else
		{
			utils_beep();
			sci_goto_pos(doc_list[idx].sci, 0, FALSE);
		}
	}
}


/* general search function, used from the find dialog */
void document_find_text(gint idx, const gchar *text, gint flags, gboolean search_backwards)
{
	gint selection_end, selection_start, search_pos;

	if (idx == -1 || ! strlen(text)) return;

	selection_start =  sci_get_selection_start(doc_list[idx].sci);
	selection_end =  sci_get_selection_end(doc_list[idx].sci);
	if ((selection_end - selection_start) > 0)
	{ // there's a selection so go to the end
		if (search_backwards)
			sci_goto_pos(doc_list[idx].sci, selection_start, TRUE);
		else
			sci_goto_pos(doc_list[idx].sci, selection_end, TRUE);
	}

	sci_set_search_anchor(doc_list[idx].sci);
	if (search_backwards)
		search_pos = sci_search_prev(doc_list[idx].sci, flags, text);
	else
		search_pos = sci_search_next(doc_list[idx].sci, flags, text);

	if (search_pos != -1)
	{
		sci_scroll_caret(doc_list[idx].sci);
	}
	else
	{
		if (dialogs_show_not_found(text))
		{
			sci_goto_pos(doc_list[idx].sci, (search_backwards) ? sci_get_length(doc_list[idx].sci) : 0, TRUE);
			document_find_text(idx, text, flags, search_backwards);
		}
	}
}


void document_replace_text(gint idx, const gchar *find_text, const gchar *replace_text, gint flags, gboolean search_backwards)
{
	gint selection_end, selection_start, search_pos;
	gint find_text_len = strlen(find_text);

	if (idx == -1 || ! find_text_len) return;

	selection_start =  sci_get_selection_start(doc_list[idx].sci);
	selection_end =  sci_get_selection_end(doc_list[idx].sci);
	if ((selection_end - selection_start) > 0)
	{ // there's a selection so go to the end
		if (search_backwards)
			sci_goto_pos(doc_list[idx].sci, selection_start, TRUE);
		else
			sci_goto_pos(doc_list[idx].sci, selection_end, TRUE);
	}

	sci_set_search_anchor(doc_list[idx].sci);
	if (search_backwards)
		search_pos = sci_search_prev(doc_list[idx].sci, flags, find_text);
	else
		search_pos = sci_search_next(doc_list[idx].sci, flags, find_text);

	if (search_pos != -1)
	{
		sci_target_start(doc_list[idx].sci, search_pos);
		sci_target_end(doc_list[idx].sci, search_pos + find_text_len);
		sci_target_replace(doc_list[idx].sci, replace_text);
		sci_scroll_caret(doc_list[idx].sci);
		sci_set_selection_start(doc_list[idx].sci, search_pos);
		sci_set_selection_end(doc_list[idx].sci, search_pos + strlen(replace_text));
	}
	else
	{
		utils_beep();
	}
}


void document_replace_sel(gint idx, const gchar *find_text, const gchar *replace_text, gint flags)
{
	gint selection_end, selection_start, search_pos;
	gint anchor_pos;
	gint find_text_len = strlen(find_text);
	gint replace_text_len = strlen(replace_text);

	if (idx == -1 || ! find_text_len) return;

	selection_start = sci_get_selection_start(doc_list[idx].sci);
	selection_end = sci_get_selection_end(doc_list[idx].sci);
	if ((selection_end - selection_start) == 0)
	{
		utils_beep();
		return;
	}

	sci_start_undo_action(doc_list[idx].sci);
	anchor_pos = selection_start;
	while (TRUE)
	{
		sci_goto_pos(doc_list[idx].sci, anchor_pos, TRUE);
		sci_set_search_anchor(doc_list[idx].sci);
		search_pos = sci_search_next(doc_list[idx].sci, flags, find_text);

		if (search_pos == -1 ||
			search_pos < selection_start ||
			search_pos + find_text_len > selection_end) break;
		else
		{
			sci_target_start(doc_list[idx].sci, search_pos);
			sci_target_end(doc_list[idx].sci, search_pos + find_text_len);
			sci_target_replace(doc_list[idx].sci, replace_text);
			anchor_pos = search_pos + replace_text_len; //avoid rematch
			// update selection end point for each replacement
			selection_end += replace_text_len - find_text_len;
		}
	}
	sci_end_undo_action(doc_list[idx].sci);
	// set selection again, because it got lost and end may be moved
	sci_set_selection_start(doc_list[idx].sci, selection_start);
	sci_set_selection_end(doc_list[idx].sci, selection_end);

	sci_scroll_caret(doc_list[idx].sci);
	gtk_widget_hide(app->replace_dialog);
}


void document_replace_all(gint idx, const gchar *find_text, const gchar *replace_text, gint flags)
{
	gint search_pos;
	gint find_text_len = strlen(find_text);
	gint replace_text_len = strlen(replace_text);

	if (idx == -1 || ! find_text_len) return;

	sci_start_undo_action(doc_list[idx].sci);
	sci_goto_pos(doc_list[idx].sci, 0, FALSE);
	sci_set_search_anchor(doc_list[idx].sci);

	search_pos = sci_search_next(doc_list[idx].sci, flags, find_text);
	while (search_pos != -1)
	{
		sci_target_start(doc_list[idx].sci, search_pos);
		sci_target_end(doc_list[idx].sci, search_pos + find_text_len);
		sci_target_replace(doc_list[idx].sci, replace_text);
		// next search starts after replaced text (avoids rematching):
		sci_goto_pos(doc_list[idx].sci, search_pos + replace_text_len, TRUE);
		sci_set_search_anchor(doc_list[idx].sci);
		search_pos = sci_search_next(doc_list[idx].sci, flags, find_text);
	}
	sci_end_undo_action(doc_list[idx].sci);
	sci_scroll_caret(doc_list[idx].sci);
	gtk_widget_hide(app->replace_dialog);
}


void document_set_font(ScintillaObject *sci, const gchar *font_name, gint size)
{
	gint style;

	for (style = 0; style <= 127; style++)
		sci_set_font(sci, style, font_name, size);
	// line number and braces
	sci_set_font(sci, STYLE_LINENUMBER, font_name, size);
	sci_set_font(sci, STYLE_BRACELIGHT, font_name, size);
	sci_set_font(sci, STYLE_BRACEBAD, font_name, size);
	// zoom to 100% to prevent confusion
	sci_zoom_off(sci);
	//sci_colourise(sci, 0, sci_get_length(sci));
}


void document_update_tag_list(gint idx, gboolean update)
{
	// if the filetype doesn't has a tag parser or it is a new file, leave
	if (idx == -1 || ! doc_list[idx].file_type->has_tags || ! doc_list[idx].file_name) return;

	if (doc_list[idx].tm_file == NULL)
	{
		gchar *locale_filename = g_locale_from_utf8(doc_list[idx].file_name, -1, NULL, NULL, NULL);
		doc_list[idx].tm_file = tm_source_file_new(locale_filename, FALSE);
		g_free(locale_filename);
		if (! doc_list[idx].tm_file) return;
		tm_workspace_add_object(doc_list[idx].tm_file);
		if (update)
		{
			// parse the file after setting the filetype
			// this is necessary to switch between filetypes
			TM_SOURCE_FILE(doc_list[idx].tm_file)->lang = getNamedLanguage((doc_list[idx].file_type)->name);
			tm_source_file_update(doc_list[idx].tm_file, TRUE, FALSE, TRUE);
		}
		utils_update_tag_list(idx, TRUE);
	}
	else
	{
		gint len = sci_get_length(doc_list[idx].sci) + 1;
		gchar *buf = (gchar*) g_malloc(len);

		sci_get_text(doc_list[idx].sci, len, buf);

		if (tm_source_file_update(doc_list[idx].tm_file, TRUE, FALSE, TRUE))
		//if (tm_source_file_buffer_update(doc_list[idx].tm_file, buf, len, TRUE))
		{
			utils_update_tag_list(idx, TRUE);
		}
		else
		{
			geany_debug(_("tag list updating failed"));
		}
	}
}


/* sets the filetype of the the document (sets syntax highlighting and tagging) */
void document_set_filetype(gint idx, filetype *type)
{
	if (! type || idx < 0) return;
	if (type->id > GEANY_MAX_FILE_TYPES) return;

	doc_list[idx].file_type = type;
	document_update_tag_list(idx, TRUE);
	type->style_func_ptr(doc_list[idx].sci);

	// For C/C++/Java files, get list of typedefs for colourising
	if (sci_get_lexer(doc_list[idx].sci) == SCLEX_CPP)
	{
		guint j, n;

		// assign project keywords
		if ((app->tm_workspace) && (app->tm_workspace->work_object.tags_array))
		{
			GPtrArray *typedefs = tm_tags_extract(app->tm_workspace->work_object.tags_array,
									tm_tag_typedef_t | tm_tag_struct_t | tm_tag_class_t);
			if ((typedefs) && (typedefs->len > 0))
			{
				GString *s = g_string_sized_new(typedefs->len * 10);
				for (j = 0; j < typedefs->len; ++j)
				{
					if (!(TM_TAG(typedefs->pdata[j])->atts.entry.scope))
					{
						if (TM_TAG(typedefs->pdata[j])->name)
						{
							g_string_append(s, TM_TAG(typedefs->pdata[j])->name);
							g_string_append_c(s, ' ');
						}
					}
				}
				for (n = 0; n < GEANY_MAX_OPEN_FILES; n++)
				{
					if (doc_list[n].sci)
					{
						sci_set_keywords(doc_list[n].sci, 3, s->str);
						sci_colourise(doc_list[n].sci, 0, -1);
					}
				}
				//SSM(doc_list[idx].sci, SCI_SETKEYWORDS, 3, (sptr_t) s->str);
				g_string_free(s, TRUE);
			}
			g_ptr_array_free(typedefs, TRUE);
		}
	}
	sci_colourise(doc_list[idx].sci, 0, -1);
	utils_build_show_hide(idx);
	geany_debug("%s : %s (%s)",	(doc_list[idx].file_name) ? doc_list[idx].file_name : "(null)",
								type->name, doc_list[idx].encoding);
}


gchar *document_get_eol_mode(gint idx)
{
	if (idx == -1) return '\0';

	switch (sci_get_eol_mode(doc_list[idx].sci))
	{
		case SC_EOL_CRLF: return _("Win (CRLF)"); break;
		case SC_EOL_CR: return _("Max (CR)"); break;
		case SC_EOL_LF:
		default: return _("Unix (LF)"); break;
	}
}


gchar *document_prepare_template(filetype *ft)
{
	gchar *gpl_notice = NULL;
	gchar *template = NULL;
	gchar *ft_template = NULL;

	if (ft != NULL)
	{
		switch (ft->id)
		{
			case GEANY_FILETYPES_PHP:
			{	// PHP: include the comment in <?php ?> - tags
				gchar *tmp = templates_get_template_fileheader(
						GEANY_TEMPLATE_FILEHEADER, ft->extension, -1);
				gpl_notice = g_strconcat("<?php\n", tmp, "?>\n\n", NULL);
				g_free(tmp);
				break;
			}
			case GEANY_FILETYPES_PASCAL:
			{	// Pascal: comments are in { } brackets
				gpl_notice = templates_get_template_fileheader(
						GEANY_TEMPLATE_FILEHEADER_PASCAL, ft->extension, -1);
				break;
			}
			case GEANY_FILETYPES_PYTHON:
			case GEANY_FILETYPES_RUBY:
			case GEANY_FILETYPES_SH:
			case GEANY_FILETYPES_MAKE:
			case GEANY_FILETYPES_PERL:
			{
				gpl_notice = templates_get_template_fileheader(
						GEANY_TEMPLATE_FILEHEADER_ROUTE, ft->extension, -1);
				break;
			}
			default:
			{	// -> C, C++, Java, ...
				gpl_notice = templates_get_template_fileheader(
						GEANY_TEMPLATE_FILEHEADER, ft->extension, -1);
			}
		}
		ft_template = filetypes_get_template(ft);
		template = g_strconcat(gpl_notice, ft_template, NULL);
		g_free(ft_template);
		g_free(gpl_notice);
		return template;
	}
	else
	{	// new file w/o template
		return templates_get_template_fileheader(GEANY_TEMPLATE_FILETYPE_NONE, NULL, -1);
	}
}


void document_unfold_all(gint idx)
{
	gint lines, pos, i;

	if (idx == -1 || ! doc_list[idx].is_valid) return;

	lines = sci_get_line_count(doc_list[idx].sci);
	pos = sci_get_current_position(doc_list[idx].sci);

	for (i = 0; i < lines; i++)
	{
		sci_ensure_line_is_visible(doc_list[idx].sci, i);
	}
}


void document_fold_all(gint idx)
{
	gint lines, pos, i;

	if (idx == -1 || ! doc_list[idx].is_valid) return;

	lines = sci_get_line_count(doc_list[idx].sci);
	pos = sci_get_current_position(doc_list[idx].sci);

	for (i = 0; i < lines; i++)
	{
		gint level = sci_get_fold_level(doc_list[idx].sci, i);
		if (level & SC_FOLDLEVELHEADERFLAG)
		{
			if (sci_get_fold_expanded(doc_list[idx].sci, i))
					sci_toggle_fold(doc_list[idx].sci, i);
		}
	}
}
