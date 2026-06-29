/*
 * batchcolor.c – Batch color palette and range application
 *
 * Copyright (C) 2026 Infineon Technologies.
 * Author: Mohamed Aziz Allani.
 * E-mail: MohamedAziz.Allani@infineon.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * This module allows batch application of color palettes (gradients)
 * and value range settings (fixed range, full range, invert, zero-to-min)
 * to multiple selected data channels across open files.
 * It also supports renaming selected channels and saving the modified
 * files as new .gwy files.
 */


#include <gtk/gtk.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyversion.h>
#include <libgwymodule/gwymodule.h>
#include <libgwydgets/gwydgets.h>
#include <app/gwyapp.h>
#include <libprocess/gwyprocess.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libdraw/gwydraw.h>
#include <libprocess/gwyprocessenums.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwymodule/gwymodule-file.h>
#include <libgwyddion/gwycontainer.h>
#include <libgwydgets/gwyoptionmenus.h>
#include <libgwydgets/gwyinventorystore.h>
#include <libdraw/gwygradient.h>


#define MOD_NAME "batchcolor"

enum {
    COL_CHECK = 0,
    COL_NAME,
    COL_CONTAINER,
    COL_ID,
    COL_IS_CHANNEL,
    COL_CLEAN_TITLE,
    COL_FILENAME,
    N_COLS
};

typedef struct {
    GtkTreeStore *store;
    GtkComboBox *palette_combo;
    GtkEntry *start_entry;
    GtkEntry *end_entry;
    GtkWidget *select_all_check;
    GtkWidget *scrolled;
    GtkComboBox *name_combo;
    GHashTable *name_to_rows;
    gchar *last_selected;
    GtkEntry *rename_entry;
    gint applied_count;
    gdouble temp_min;
    gdouble temp_max;
    GtkWidget *left_vbox;
    GtkWidget *right_vbox;
    GtkListStore *palette_store;
    GtkWidget *apply_range_btn;
    GtkWidget *invert_btn;
    GtkWidget *full_btn;
    GtkWidget *zero_btn;
    GtkWidget *rename_btn;
    GtkWidget *save_btn;
    GtkWidget *status_label;
} TreeViewData;

typedef struct {
    gchar *old_name;
    gchar *new_name;
    GwyContainer *container;
    gint data_id;
} RenameInfo;

typedef enum {
    RANGE_INVERT,
    RANGE_FULL,
    RANGE_ZERO_MIN,
    RANGE_FIXED
} RangeMode;

typedef struct {
    TreeViewData *tvdata;
    RangeMode mode;
    gdouble user_min;
    gdouble user_max;
} RangeParams;

static gboolean module_register(void);

static void batchcolor(GwyContainer *data, GwyRunType run, const gchar *name);

static void set_widget_as_warning_message(GtkWidget *widget);

static gboolean has_selected_channels(TreeViewData *tvdata);

static void update_sensitivity(TreeViewData *tvdata);

static gboolean apply_range_foreach(GtkTreeModel *model,
                                    G_GNUC_UNUSED GtkTreePath *path,
                                    GtkTreeIter *iter,
                                    gpointer data);

static void invert_mapping(G_GNUC_UNUSED GtkWidget *button, gpointer user_data);

static void set_full_range(G_GNUC_UNUSED GtkWidget *button, gpointer user_data);

static void set_zero_to_min(G_GNUC_UNUSED GtkWidget *button, gpointer user_data);

static gboolean check_units_foreach(GtkTreeModel *model,
                                    G_GNUC_UNUSED GtkTreePath *path,
                                    GtkTreeIter *iter,
                                    gpointer data);

static void apply_range(G_GNUC_UNUSED GtkWidget *button, gpointer user_data);

static gboolean select_all_foreach(GtkTreeModel *model,
                                   G_GNUC_UNUSED GtkTreePath *path,
                                   GtkTreeIter *iter,
                                   gpointer data);

static void on_select_all_toggled(GtkToggleButton *button, gpointer user_data);

static gboolean collect_name_foreach(GtkTreeModel *model,
                                     G_GNUC_UNUSED GtkTreePath *path,
                                     GtkTreeIter *iter,
                                     gpointer data);

static void populate_name_combo(TreeViewData *tvdata);

static void on_name_combo_changed(GtkComboBox *combo, gpointer user_data);

static void rename_info_free(RenameInfo *r);

static gboolean collect_selected_foreach(GtkTreeModel *model,
                                         G_GNUC_UNUSED GtkTreePath *path,
                                         GtkTreeIter *iter,
                                         gpointer data);

static gboolean find_row_by_container_id(GtkTreeStore *store, GwyContainer *c, gint id,
                                         GtkTreeIter *iter);

static void apply_rename(GtkWidget *button G_GNUC_UNUSED, gpointer user_data);

static void save_selected_gwy(GtkWidget *button, TreeViewData *tvdata);

static void load_gradients(TreeViewData *tvdata);

static GwyDialogOutcome run_gui(void);

static void collect_containers(GwyContainer *c, gpointer user_data);

static GtkWidget * build_treeview(GtkTreeStore **pstore);

static void
on_check_toggled(G_GNUC_UNUSED GtkCellRendererToggle *cell,
                 gchar *path_str,
                 gpointer user_data);

static gboolean apply_foreach(GtkTreeModel *model,
                              G_GNUC_UNUSED GtkTreePath *path,
                              GtkTreeIter *iter,
                              gpointer data);

static void apply_changes(GtkWidget *button, gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Batch apply color palette and fixed value range to multiple data channels"),
    "Mohamed Aziz Allani",
    "0.1",
    "Infineon",
    "2025",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_process_func_register(MOD_NAME,
                              (GwyProcessFunc)&batchcolor,
                              N_("/M_ultidata/_Batch Color Mapping"),
                              NULL,
                              GWY_RUN_INTERACTIVE,
                              GWY_MENU_FLAG_DATA,
                              N_("Batch apply color palette and fixed value range to selected channels"));
    return TRUE;
}

static void
batchcolor(G_GNUC_UNUSED GwyContainer *data,
           GwyRunType run,
           G_GNUC_UNUSED const gchar *name)
{
    if (run != GWY_RUN_INTERACTIVE) {
        return;
    }
    run_gui();
}

static void
set_widget_as_warning_message(GtkWidget *widget)
{
    GdkColor gdkcolor_warning = { 0, 45056, 20480, 0 };
    gtk_widget_modify_fg(widget, GTK_STATE_NORMAL, &gdkcolor_warning);
}

static gboolean
has_selected_channels(TreeViewData *tvdata)
{
    gboolean found = FALSE;
    GtkTreeModel *model = GTK_TREE_MODEL(tvdata->store);
    GtkTreeIter file_iter, chan_iter;

    gboolean valid_file = gtk_tree_model_get_iter_first(model, &file_iter);
    while (valid_file && !found) {
        if (gtk_tree_model_iter_children(model, &chan_iter, &file_iter)) {
            do {
                gboolean checked;
                gboolean is_channel;
                gtk_tree_model_get(model, &chan_iter,
                                   COL_CHECK, &checked,
                                   COL_IS_CHANNEL, &is_channel,
                                   -1);

                if (checked && is_channel) {
                    found = TRUE;
                }
            } while (!found && gtk_tree_model_iter_next(model, &chan_iter));
        }
        valid_file = gtk_tree_model_iter_next(model, &file_iter);
    }
    return found;
}

static void
update_sensitivity(TreeViewData *tvdata)
{
    gboolean has_sel = has_selected_channels(tvdata);
    gboolean units_ok = TRUE;
    GwySIUnit *ref_unit = NULL;

    if (!has_sel) {
        units_ok = TRUE;
        goto set_ui;
    }

    GtkTreeModel *model = GTK_TREE_MODEL(tvdata->store);
    GtkTreeIter file_iter, chan_iter;
    gboolean valid_file = gtk_tree_model_get_iter_first(model, &file_iter);

    while (valid_file && units_ok) {
        if (gtk_tree_model_iter_children(model, &chan_iter, &file_iter)) {
            do {
                gboolean checked, is_channel;
                GwyContainer *c;
                gint id;

                gtk_tree_model_get(model, &chan_iter,
                                   COL_CHECK, &checked,
                                   COL_IS_CHANNEL, &is_channel,
                                   COL_CONTAINER, &c,
                                   COL_ID, &id,
                                   -1);

                if (checked && is_channel && c && id >= 0) {
                    GwyDataField *df = gwy_container_get_object(c,
                                                                gwy_app_get_data_key_for_id(id));
                    GwySIUnit *unit = gwy_data_field_get_si_unit_z(df);

                    if (!ref_unit) {
                        ref_unit = g_object_ref(unit);
                    }
                    else if (!gwy_si_unit_equal(unit, ref_unit)) {
                        units_ok = FALSE;
                        break;
                    }
                }
            } while (gtk_tree_model_iter_next(model, &chan_iter));
        }
        valid_file = gtk_tree_model_iter_next(model, &file_iter);
    }

set_ui:
    gboolean valid_range = has_sel && units_ok;

    gtk_widget_set_sensitive(tvdata->apply_range_btn, valid_range);
    gtk_widget_set_sensitive(tvdata->invert_btn, has_sel);
    gtk_widget_set_sensitive(tvdata->full_btn, has_sel);
    gtk_widget_set_sensitive(tvdata->zero_btn, has_sel);
    gtk_widget_set_sensitive(tvdata->rename_btn, has_sel);
    gtk_widget_set_sensitive(tvdata->save_btn, has_sel);
    gtk_widget_set_sensitive(GTK_WIDGET(tvdata->palette_combo), has_sel);

    if (!has_sel)
        gtk_label_set_text(GTK_LABEL(tvdata->status_label), _("No channels selected"));
    else if (!units_ok)
        gtk_label_set_text(GTK_LABEL(tvdata->status_label), _("Selected channels have inconsistent units"));
    else
        gtk_label_set_text(GTK_LABEL(tvdata->status_label), "");

    if (ref_unit)
        g_object_unref(ref_unit);
}


static gboolean
apply_range_foreach(GtkTreeModel *model,
                    G_GNUC_UNUSED GtkTreePath *path,
                    GtkTreeIter *iter,
                    gpointer data)
{
    RangeParams *params = data;
    TreeViewData *tvdata = params->tvdata;
    gboolean checked, is_channel;
    GwyContainer *c;
    gint id;

    gtk_tree_model_get(model, iter,
                       COL_CHECK, &checked,
                       COL_IS_CHANNEL, &is_channel,
                       COL_CONTAINER, &c,
                       COL_ID, &id,
                       -1);

    if (!checked || !is_channel || !c || id < 0)
        return FALSE;

    GwyDataField *df = gwy_container_get_object(c, gwy_app_get_data_key_for_id(id));
    if (!df)
        return FALSE;

    gchar *min_key = g_strdup_printf("/%d/base/min", id);
    gchar *max_key = g_strdup_printf("/%d/base/max", id);
    gchar *type_key = g_strdup_printf("/%d/base/range-type", id);

    switch (params->mode) {
        case RANGE_INVERT: {
            gdouble cur_min = gwy_container_contains_by_name(c, min_key)
                              ? gwy_container_get_double_by_name(c, min_key) : gwy_data_field_get_min(df);
            gdouble cur_max = gwy_container_contains_by_name(c, max_key)
                              ? gwy_container_get_double_by_name(c, max_key) : gwy_data_field_get_max(df);
            gwy_container_set_double_by_name(c, min_key, cur_max);
            gwy_container_set_double_by_name(c, max_key, cur_min);
            gwy_container_set_enum_by_name(c, type_key, GWY_LAYER_BASIC_RANGE_FIXED);
            break;
        }
        case RANGE_FULL: {
            if (gwy_container_contains_by_name(c, g_strdup_printf("/%d/base/original_min", id))) {
                gdouble orig_min = gwy_container_get_double_by_name(c, g_strdup_printf("/%d/base/original_min", id));
                gdouble real_min = gwy_data_field_get_min(df);
                if (orig_min != real_min) {
                    gwy_data_field_add(df, orig_min - real_min);
                    gwy_data_field_data_changed(df);
                }
                gwy_container_remove_by_name(c, g_strdup_printf("/%d/base/original_min", id));
                gwy_container_remove_by_name(c, g_strdup_printf("/%d/base/original_max", id));
            }
            gwy_container_set_enum_by_name(c, type_key, GWY_LAYER_BASIC_RANGE_FULL);
            gwy_container_remove_by_name(c, min_key);
            gwy_container_remove_by_name(c, max_key);
            break;
        }
        case RANGE_ZERO_MIN: {
            gdouble min_val = gwy_data_field_get_min(df);
            gdouble max_val = gwy_data_field_get_max(df);
            if (!gwy_container_contains_by_name(c, g_strdup_printf("/%d/base/original_min", id))) {
                gwy_container_set_double_by_name(c, g_strdup_printf("/%d/base/original_min", id), min_val);
                gwy_container_set_double_by_name(c, g_strdup_printf("/%d/base/original_max", id), max_val);
            }
            gwy_data_field_add(df, -min_val);
            gwy_data_field_data_changed(df);
            gwy_container_set_double_by_name(c, min_key, 0.0);
            gwy_container_set_double_by_name(c, max_key, max_val - min_val);
            gwy_container_set_enum_by_name(c, type_key, GWY_LAYER_BASIC_RANGE_FIXED);
            break;
        }
        case RANGE_FIXED: {
            gwy_container_set_enum_by_name(c, type_key, GWY_LAYER_BASIC_RANGE_FIXED);
            gwy_container_set_double_by_name(c, min_key, params->user_min);
            gwy_container_set_double_by_name(c, max_key, params->user_max);
            gwy_data_field_data_changed(df);
            break;
        }
    }

    g_free(min_key);
    g_free(max_key);
    g_free(type_key);
    tvdata->applied_count++;
    return FALSE;
}

static void
invert_mapping(G_GNUC_UNUSED GtkWidget *button, gpointer user_data)
{
    TreeViewData *tvdata = (TreeViewData *)user_data;
    if (!has_selected_channels(tvdata))
        return;

    tvdata->applied_count = 0;

    RangeParams params = { tvdata, RANGE_INVERT, 0.0, 0.0 };
    gtk_tree_model_foreach(GTK_TREE_MODEL(tvdata->store), apply_range_foreach, &params);

    const gchar *start_text = gtk_entry_get_text(tvdata->start_entry);
    const gchar *end_text = gtk_entry_get_text(tvdata->end_entry);
    if (start_text && *start_text && end_text && *end_text) {
        gchar *start_clean = g_strstrip(g_strdup(start_text));
        gchar *end_clean = g_strstrip(g_strdup(end_text));
        if (*start_clean && *end_clean) {
            gchar *endptr1, *endptr2;
            gdouble start_val = g_strtod(start_clean, &endptr1);
            gdouble end_val = g_strtod(end_clean, &endptr2);
            if ((endptr1 != start_clean && *endptr1 == '\0')
                && (endptr2 != end_clean && *endptr2 == '\0')) {
                gchar start_buf[32];
                gchar end_buf[32];
                g_snprintf(start_buf, sizeof(start_buf), "%.10g", end_val);
                g_snprintf(end_buf, sizeof(end_buf), "%.10g", start_val);
                gtk_entry_set_text(tvdata->start_entry, start_buf);
                gtk_entry_set_text(tvdata->end_entry, end_buf);
            }
        }
        g_free(start_clean);
        g_free(end_clean);
    }
}

static void
set_full_range(G_GNUC_UNUSED GtkWidget *button, gpointer user_data)
{
    TreeViewData *tvdata = (TreeViewData *)user_data;
    tvdata->applied_count = 0;
    RangeParams params = { tvdata, RANGE_FULL, 0.0, 0.0 };
    gtk_tree_model_foreach(GTK_TREE_MODEL(tvdata->store), apply_range_foreach, &params);
    gtk_entry_set_text(tvdata->start_entry, "");
    gtk_entry_set_text(tvdata->end_entry, "");
}

static void
set_zero_to_min(G_GNUC_UNUSED GtkWidget *button, gpointer user_data)
{
    TreeViewData *tvdata = (TreeViewData *)user_data;

    tvdata->applied_count = 0;
    RangeParams params = { tvdata, RANGE_ZERO_MIN, 0.0, 0.0 };
    gtk_tree_model_foreach(GTK_TREE_MODEL(tvdata->store), apply_range_foreach, &params);
    gtk_entry_set_text(tvdata->start_entry, "");
    gtk_entry_set_text(tvdata->end_entry, "");
}

static gboolean
check_units_foreach(GtkTreeModel *model,
                    G_GNUC_UNUSED GtkTreePath *path,
                    GtkTreeIter *iter,
                    gpointer data)
{
    struct {
        GwySIUnit **ref;
        gboolean *consistent;
    } *p = data;
    gboolean checked, is_channel;
    GwyContainer *c;
    gint id;

    gtk_tree_model_get(model, iter,
                       COL_CHECK, &checked,
                       COL_IS_CHANNEL, &is_channel,
                       COL_CONTAINER, &c,
                       COL_ID, &id,
                       -1);

    if (!checked || !is_channel || !c || id < 0)
        return FALSE;

    GwyDataField *df = gwy_container_get_object(c,
                                                gwy_app_get_data_key_for_id(id));
    if (!df)
        return FALSE;

    GwySIUnit *unit = gwy_data_field_get_si_unit_z(df);

    if (*p->ref == NULL) {
        *p->ref = gwy_si_unit_duplicate(unit);
    } else if (!gwy_si_unit_equal(*p->ref, unit)) {
        *p->consistent = FALSE;
    }

    return FALSE;
}

static void
apply_range(G_GNUC_UNUSED GtkWidget *button, gpointer user_data)
{
    TreeViewData *tvdata = (TreeViewData *)user_data;
    const gchar *start_text;
    const gchar *end_text;
    gchar *start_clean, *end_clean;
    gchar *endptr;
    gdouble user_min, user_max;
    GwySIUnit *ref_unit = NULL;
    gboolean units_consistent = TRUE;
    struct {
        GwySIUnit **ref;
        gboolean *consistent;
    } check_params = { &ref_unit, &units_consistent };

    start_text = gtk_entry_get_text(tvdata->start_entry);
    end_text = gtk_entry_get_text(tvdata->end_entry);
    start_clean = g_strstrip(g_strdup(start_text ? start_text : ""));
    end_clean = g_strstrip(g_strdup(end_text ? end_text : ""));

    if (!*start_clean || !*end_clean) {
        goto cleanup;
    }

    user_min = g_strtod(start_clean, &endptr);
    if (endptr == start_clean || *endptr != '\0') {
        goto cleanup;
    }
    user_max = g_strtod(end_clean, &endptr);
    if (endptr == end_clean || *endptr != '\0') {
        goto cleanup;
    }

    gtk_tree_model_foreach(GTK_TREE_MODEL(tvdata->store),
                           check_units_foreach, &check_params);

    if (!units_consistent) {
        if (ref_unit) {
        g_object_unref(ref_unit);
        }
        goto cleanup;
    }
    if (ref_unit) {
    g_object_unref(ref_unit);
    }

    /* Apply the range */
    tvdata->applied_count = 0;
    RangeParams params = { tvdata, RANGE_FIXED, user_min, user_max };
    gtk_tree_model_foreach(GTK_TREE_MODEL(tvdata->store),
                           apply_range_foreach, &params);

cleanup:
    g_free(start_clean);
    g_free(end_clean);
}

static gboolean
select_all_foreach(GtkTreeModel *model,
                   G_GNUC_UNUSED GtkTreePath *path,
                   GtkTreeIter *iter,
                   gpointer data)
{
    gboolean active = GPOINTER_TO_UINT(data);
    gboolean is_channel;

    gtk_tree_model_get(model, iter, COL_IS_CHANNEL, &is_channel, -1);

    if (is_channel) {
        gtk_tree_store_set(GTK_TREE_STORE(model), iter, COL_CHECK, active, -1);
    }

    return FALSE;
}

static void
on_select_all_toggled(GtkToggleButton *button, gpointer user_data)
{
    TreeViewData *tvdata = (TreeViewData *)user_data;
    GtkTreeStore *store = tvdata->store;
    gboolean active = gtk_toggle_button_get_active(button);

    if (active) {
        gtk_tree_model_foreach(GTK_TREE_MODEL(store),
                               (GtkTreeModelForeachFunc)select_all_foreach,
                               GUINT_TO_POINTER(TRUE));
        gtk_tree_view_expand_all(GTK_TREE_VIEW(gtk_bin_get_child(GTK_BIN(tvdata->scrolled))));
    } else {
        gtk_tree_model_foreach(GTK_TREE_MODEL(store),
                               (GtkTreeModelForeachFunc)select_all_foreach,
                               GUINT_TO_POINTER(FALSE));
    }

    update_sensitivity(tvdata);
}

static gboolean
collect_name_foreach(GtkTreeModel *model,
                     G_GNUC_UNUSED GtkTreePath *path,
                     GtkTreeIter *iter,
                     gpointer data)
{
    GHashTable *name_to_rows = (GHashTable *)data;
    gboolean is_channel;
    gchar *clean_title = NULL;

    gtk_tree_model_get(model, iter,
                       COL_IS_CHANNEL, &is_channel,
                       COL_CLEAN_TITLE, &clean_title,
                       -1);

    if (is_channel && clean_title && *clean_title) {
        gchar *key = g_strstrip(g_strdup(clean_title));

        GList *list = g_hash_table_lookup(name_to_rows, key);
        GtkTreeIter *copy = g_memdup(iter, sizeof(GtkTreeIter));
        list = g_list_prepend(list, copy);
        g_hash_table_replace(name_to_rows, key, list);
    }

    g_free(clean_title);
    return FALSE;
}

static void
populate_name_combo(TreeViewData *tvdata)
{
    GtkTreeStore *store = tvdata->store;
    GHashTable *name_to_rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GList *names;
    GList *l;
    GtkTreeIter iter;

    tvdata->name_to_rows = name_to_rows;
    tvdata->last_selected = NULL;

    /* Collect names (key: name, value: list of tree iterators) */
    gtk_tree_model_foreach(GTK_TREE_MODEL(store),
                           (GtkTreeModelForeachFunc)collect_name_foreach,
                           name_to_rows);

    /* Populate the combo box */
    GtkComboBox *combo = tvdata->name_combo;
    GtkTreeModel *model = gtk_combo_box_get_model(combo);
    if (GTK_IS_LIST_STORE(model)) {
        gtk_list_store_clear(GTK_LIST_STORE(model));
    }

    gtk_list_store_append(GTK_LIST_STORE(model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, _("Select by name"), -1);

    names = g_hash_table_get_keys(name_to_rows);
    names = g_list_sort(names, (GCompareFunc)strcmp);

    for (l = names; l; l = l->next) {
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, l->data, -1);
    }

    gtk_combo_box_set_active(combo, 0);
    g_list_free(names);
}

static void
on_name_combo_changed(GtkComboBox *combo, gpointer user_data)
{
    TreeViewData *tvdata = (TreeViewData *)user_data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    gchar *selected = NULL;
    GList *rows;
    GList *l;

    if (!tvdata->name_to_rows) {
        return;
   }

    if (!gtk_combo_box_get_active_iter(combo, &iter)) {
        return;
    }

    model = gtk_combo_box_get_model(combo);
    gtk_tree_model_get(model, &iter, 0, &selected, -1);

    if (!selected || g_strcmp0(selected, _("Select by name")) == 0) {
        g_free(selected);
        return;
    }

    rows = g_hash_table_lookup(tvdata->name_to_rows, selected);
    if (!rows) {
        g_free(selected);
        return;
    }

    for (l = rows; l; l = l->next) {
        GtkTreeIter *iter_ptr = (GtkTreeIter *)l->data;
        gtk_tree_store_set(tvdata->store, iter_ptr, COL_CHECK, TRUE, -1);
    }

    g_free(selected);
    gtk_combo_box_set_active(combo, 0);
    update_sensitivity(tvdata);
}

static void
rename_info_free(RenameInfo *r)
{
    g_free(r->old_name);
    g_free(r->new_name);
    g_free(r);
}

static gboolean
collect_selected_foreach(GtkTreeModel *model,
                         G_GNUC_UNUSED GtkTreePath *path,
                         GtkTreeIter *iter,
                         gpointer data)
{
    GHashTable **unique_containers = (GHashTable **)data;

    gboolean checked;
    gboolean is_channel;
    GwyContainer *container = NULL;

    gtk_tree_model_get(model, iter,
                       COL_CHECK, &checked,
                       COL_IS_CHANNEL, &is_channel,
                       COL_CONTAINER, &container,
                       -1);

    /* Only process checked channels */
    if (!checked || !is_channel || !container) {
        return FALSE;
    }

    if (!g_hash_table_contains(*unique_containers, container)) {
        g_hash_table_insert(*unique_containers, container, g_object_ref(container));
    }

    return FALSE;
}

static gboolean
find_row_by_container_id(GtkTreeStore *store, GwyContainer *c, gint id, GtkTreeIter *iter)
{
    GtkTreeIter file_iter, chan_iter;
    gboolean valid_file = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &file_iter);
    while (valid_file) {
        if (gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &chan_iter, &file_iter)) {
            do {
                GwyContainer *row_c;
                gint row_id;
                gtk_tree_model_get(GTK_TREE_MODEL(store), &chan_iter,
                                   COL_CONTAINER, &row_c,
                                   COL_ID, &row_id,
                                   -1);
                if (row_c == c && row_id == id) {
                    *iter = chan_iter;
                    return TRUE;
                }
            } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &chan_iter));
        }
        valid_file = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &file_iter);
    }
    return FALSE;
}

static void
apply_rename(GtkWidget *button G_GNUC_UNUSED, gpointer user_data)
{
    TreeViewData *tvdata = (TreeViewData *)user_data;
    GtkTreeModel *model = GTK_TREE_MODEL(tvdata->store);
    GtkTreeIter file_iter, chan_iter;
    gboolean valid_file;
    const gchar *raw;
    gchar *new_name;
    GList *renames = NULL;
    GList *l;

    raw = gtk_entry_get_text(tvdata->rename_entry);
    new_name = g_strstrip(g_strdup(raw ? raw : ""));
    if (!new_name || *new_name == '\0') {
        g_free(new_name);
        return;
    }

    valid_file = gtk_tree_model_get_iter_first(model, &file_iter);
    while (valid_file) {
        if (gtk_tree_model_iter_children(model, &chan_iter, &file_iter)) {
            do {
                gboolean checked, is_channel;
                GwyContainer *container;
                gint id;
                gchar *old_title = NULL;

                gtk_tree_model_get(model, &chan_iter,
                                   COL_CHECK, &checked,
                                   COL_IS_CHANNEL, &is_channel,
                                   COL_CONTAINER, &container,
                                   COL_ID, &id,
                                   COL_CLEAN_TITLE, &old_title,
                                   -1);

                if (checked && is_channel && id >= 0 && old_title) {
                    RenameInfo *rename = g_new0(RenameInfo, 1);
                    rename->old_name = old_title;
                    rename->new_name = g_strdup(new_name);
                    rename->container = container;
                    rename->data_id = id;
                    renames = g_list_prepend(renames, rename);
                } else {
                    g_free(old_title);
                }
            } while (gtk_tree_model_iter_next(model, &chan_iter));
        }
        valid_file = gtk_tree_model_iter_next(model, &file_iter);
    }

    if (!renames) {
        g_free(new_name);
        return;
    }

    renames = g_list_reverse(renames);

    for (l = renames; l; l = l->next) {
        RenameInfo *rename = l->data;
        GQuark key = gwy_app_get_data_title_key_for_id(rename->data_id);
        gwy_container_set_string_by_name(rename->container,
                                         g_quark_to_string(key),
                                         (const guchar*)g_strdup(rename->new_name));

        /* Update treeview row in place */
        GtkTreeIter iter;
        if (find_row_by_container_id(tvdata->store, rename->container, rename->data_id, &iter)) {
            gchar *new_display = g_markup_printf_escaped("  %s", rename->new_name);
            gtk_tree_store_set(tvdata->store, &iter,
                               COL_NAME, new_display,
                               COL_CLEAN_TITLE, g_strdup(rename->new_name),
                               -1);
            g_free(new_display);
        }
    }

    populate_name_combo(tvdata);
    update_sensitivity(tvdata);

    g_list_free_full(renames, (GDestroyNotify)rename_info_free);
    gtk_entry_set_text(tvdata->rename_entry, "");
    g_free(new_name);
}

static void
save_selected_gwy(GtkWidget *button, TreeViewData *tvdata)
{
    GHashTable *unique_containers = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                          NULL, g_object_unref);

    /* Collect unique containers from selected channels */
    gtk_tree_model_foreach(GTK_TREE_MODEL(tvdata->store),
                           collect_selected_foreach,
                           &unique_containers);

    if (g_hash_table_size(unique_containers) == 0) {
        g_hash_table_destroy(unique_containers);
        return;
    }

    /* Choose output directory – start in last open folder */
    GtkWidget *chooser = gtk_file_chooser_dialog_new(_("Save Selected Files As .GWY"),
                                                     GTK_WINDOW(gtk_widget_get_toplevel(button)),
                                                     GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                     GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

    const gchar *last_dir = gwy_app_get_current_directory();
    if (last_dir && g_file_test(last_dir, G_FILE_TEST_IS_DIR)) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), last_dir);
    }

    if (gtk_dialog_run(GTK_DIALOG(chooser)) != GTK_RESPONSE_OK) {
        gtk_widget_destroy(chooser);
        g_hash_table_destroy(unique_containers);
        return;
    }

    gchar *save_dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    gtk_widget_destroy(chooser);

    /* Save files with unique filenames */
    gint total_files = 0;
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, unique_containers);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        GwyContainer *container = GWY_CONTAINER(value);
        const gchar *orig_filename = (const gchar *)
        gwy_container_get_string_by_name(container, "/filename");
        if (!orig_filename)
            continue;

        gchar *base_name = g_path_get_basename(orig_filename);
        gchar *dot = g_strrstr(base_name, ".");
        if (dot)
            *dot = '\0';

        gchar *safe_name = g_strdup(base_name);
        gchar *full_path = g_strdup_printf("%s/%s.gwy", save_dir, safe_name);

        /* Avoid filename conflicts */
        gint counter = 1;
        while (g_file_test(full_path, G_FILE_TEST_EXISTS)) {
            g_free(full_path);
            g_free(safe_name);
            safe_name = g_strdup_printf("%s_processed_%d", base_name, counter);
            full_path = g_strdup_printf("%s/%s.gwy", save_dir, safe_name);
            counter++;
        }

        GError *err = NULL;
        if (gwy_file_save(container, full_path, GWY_RUN_NONINTERACTIVE, &err)) {
            total_files++;
        } else {
            if (err)
                g_error_free(err);
        }

        g_free(full_path);
        g_free(safe_name);
        g_free(base_name);
    }

    g_hash_table_destroy(unique_containers);
    g_free(save_dir);
}

static void
load_gradients(TreeViewData *tvdata)
{
    GtkTreeIter iter;
    GwyInventory *inventory = gwy_gradients();
    gint n = gwy_inventory_get_n_items(inventory);
    gint i;

    gtk_list_store_clear(tvdata->palette_store);

    for (i = 0; i < n; i++) {
        GwyResource *res = gwy_inventory_get_nth_item(inventory, i);
        if (!gwy_resource_get_is_preferred(res))
            continue;
        const gchar *name = gwy_resource_get_name(res);
        GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 80, 16);
        if (pixbuf) {
            GwyGradient *gradient = gwy_gradients_get_gradient(name);
            if (gradient) {
                gwy_gradient_sample_to_pixbuf(gradient, pixbuf);
            }
            gtk_list_store_append(tvdata->palette_store, &iter);
            gtk_list_store_set(tvdata->palette_store, &iter,
                               0, pixbuf, 1, name, 2, name, -1);
            g_object_unref(pixbuf);
        }
    }
    gtk_combo_box_set_active(tvdata->palette_combo, 0);
}

static GwyDialogOutcome
run_gui(void)
{
    GtkWidget *dialog, *main_hbox, *left_vbox, *right_vbox;
    GtkWidget *treeview_right;
    TreeViewData *tvdata = g_new0(TreeViewData, 1);

    /* ---- DIALOG ---- */
    dialog = gwy_dialog_new(_("Batch Color Mapping"));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 600);
    gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_OK, GTK_RESPONSE_OK);

    main_hbox = gtk_hbox_new(FALSE, 5);
    gwy_dialog_add_content(GWY_DIALOG(dialog), main_hbox, TRUE, TRUE, 0);

    /* ---- LEFT PANE ---- */
    left_vbox = gtk_vbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(main_hbox), left_vbox, FALSE, FALSE, 2);
    tvdata->left_vbox = left_vbox;

    /* ---- RIGHT PANE ---- */
    right_vbox = gtk_vbox_new(FALSE, 5);
    gtk_widget_set_size_request(right_vbox, 465, -1);
    gtk_box_pack_start(GTK_BOX(main_hbox), right_vbox, TRUE, TRUE, 2);
    tvdata->right_vbox = right_vbox;

    /* ======================================== */
    /* LEFT: FIXED COLOR RANGE                  */
    /* ======================================== */
    GtkWidget *color_range_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(color_range_label), "<b>Fixed Color Range</b>");
    gtk_misc_set_alignment(GTK_MISC(color_range_label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(left_vbox), color_range_label, FALSE, FALSE, 2);

    GtkWidget *range_vbox = gtk_vbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(left_vbox), range_vbox, FALSE, FALSE, 0);

    /* Start */
    GtkWidget *hbox_min = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(range_vbox), hbox_min, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_min), gtk_label_new(_("Start:")), FALSE, FALSE, 0);
    tvdata->start_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_width_chars(tvdata->start_entry, 10);
    gtk_box_pack_start(GTK_BOX(hbox_min), GTK_WIDGET(tvdata->start_entry), TRUE, TRUE, 0);

    /* End */
    GtkWidget *hbox_max = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(range_vbox), hbox_max, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_max), gtk_label_new(_("End:")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_max), gtk_hbox_new(FALSE, 14), FALSE, FALSE, 0);
    tvdata->end_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_width_chars(tvdata->end_entry, 10);
    gtk_box_pack_start(GTK_BOX(hbox_max), GTK_WIDGET(tvdata->end_entry), TRUE, TRUE, 0);

    /* Buttons 1 */
    GtkWidget *hbox_btn1 = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(range_vbox), hbox_btn1, TRUE, TRUE, 0);
    GtkWidget *apply_range_btn = gtk_button_new_with_label(_("Apply Fixed Range"));
    gtk_box_pack_start(GTK_BOX(hbox_btn1), apply_range_btn, TRUE, TRUE, 0);
    g_signal_connect(apply_range_btn, "clicked", G_CALLBACK(apply_range), tvdata);
    GtkWidget *invert_btn = gtk_button_new_with_label(_("Invert Mapping"));
    gtk_box_pack_start(GTK_BOX(hbox_btn1), invert_btn, TRUE, TRUE, 0);
    g_signal_connect(invert_btn, "clicked", G_CALLBACK(invert_mapping), tvdata);

    /* Buttons 2 */
    GtkWidget *hbox_btn2 = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(range_vbox), hbox_btn2, TRUE, TRUE, 0);
    GtkWidget *full_btn = gtk_button_new_with_label(_("Set Full Range"));
    gtk_box_pack_start(GTK_BOX(hbox_btn2), full_btn, TRUE, TRUE, 0);
    g_signal_connect(full_btn, "clicked", G_CALLBACK(set_full_range), tvdata);
    GtkWidget *zero_btn = gtk_button_new_with_label(_("Zero to Min"));
    gtk_box_pack_start(GTK_BOX(hbox_btn2), zero_btn, TRUE, TRUE, 0);
    g_signal_connect(zero_btn, "clicked", G_CALLBACK(set_zero_to_min), tvdata);

    gtk_box_pack_start(GTK_BOX(left_vbox), gtk_vbox_new(FALSE, 0), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(left_vbox), gtk_vbox_new(FALSE, 0), FALSE, FALSE, 2);

    /* ======================================== */
    /* LEFT: COLOR + RENAME                     */
    /* ======================================== */
    GtkWidget *color_rename_hbox = gtk_hbox_new(FALSE, 7);
    gtk_box_pack_start(GTK_BOX(left_vbox), color_rename_hbox, FALSE, FALSE, 2);

    /* --- COLOR --- */
    GtkWidget *vbox_color = gtk_vbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(color_rename_hbox), vbox_color, TRUE, TRUE, 5);

    GtkWidget *color_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(color_label), "<b>Change Color</b>");
    gtk_misc_set_alignment(GTK_MISC(color_label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox_color), color_label, FALSE, FALSE, 2);

    /* ------------------------------------------------------------------
     *  FAVORITE PALETTE COMBO
     * ------------------------------------------------------------------ */
    tvdata->palette_store = gtk_list_store_new(3,
                                               GDK_TYPE_PIXBUF,
                                               G_TYPE_STRING,
                                               G_TYPE_STRING);

    tvdata->palette_combo = GTK_COMBO_BOX(gtk_combo_box_new_with_model(GTK_TREE_MODEL(tvdata->palette_store)));

    /* ---- cell renderers ---- */
    GtkCellRenderer *pix = gtk_cell_renderer_pixbuf_new();
    GtkCellRenderer *txt = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(tvdata->palette_combo), pix, FALSE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(tvdata->palette_combo), pix, "pixbuf", 0);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(tvdata->palette_combo), txt, TRUE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(tvdata->palette_combo), txt, "text", 1);

    load_gradients(tvdata);
    g_signal_connect(gwy_gradients(), "item-updated", G_CALLBACK(load_gradients), tvdata);
    gtk_combo_box_set_active(tvdata->palette_combo, 0);


    /* ---- connect the “changed” signal --- */
    g_signal_connect(tvdata->palette_combo, "changed",
                     G_CALLBACK(apply_changes), tvdata);

    GtkWidget *hbox_palette = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox_color), hbox_palette, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(hbox_palette), GTK_WIDGET(tvdata->palette_combo), TRUE, TRUE, 0);


    /* --- RENAME --- */
    GtkWidget *vbox_rename = gtk_vbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(color_rename_hbox), vbox_rename, TRUE, TRUE, 5);

    GtkWidget *rename_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(rename_label), "<b>Rename Channels</b>");
    gtk_misc_set_alignment(GTK_MISC(rename_label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox_rename), rename_label, FALSE, FALSE, 2);

    GtkWidget *hbox_rename = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox_rename), hbox_rename, FALSE, FALSE, 0);

    tvdata->rename_entry = GTK_ENTRY(gtk_entry_new());
    gtk_box_pack_start(GTK_BOX(hbox_rename), GTK_WIDGET(tvdata->rename_entry), TRUE, TRUE, 0);

    GtkWidget *rename_btn = gtk_button_new_with_label(_("Apply"));
    gtk_box_pack_start(GTK_BOX(vbox_rename), rename_btn, FALSE, FALSE, 0);
    g_signal_connect(rename_btn, "clicked", G_CALLBACK(apply_rename), tvdata);

    /* ======================================== */
    /* RIGHT: FILE LIST                         */
    /* ======================================== */
    GtkWidget *files_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(files_label), "<b>List of Open SPM Files</b>");
    gtk_misc_set_alignment(GTK_MISC(files_label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(right_vbox), files_label, FALSE, FALSE, 2);

    GtkWidget *top_hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(right_vbox), top_hbox, FALSE, FALSE, 0);

    tvdata->select_all_check = gtk_check_button_new_with_label(_("Select All"));
    gtk_box_pack_start(GTK_BOX(top_hbox), tvdata->select_all_check, FALSE, FALSE, 5);
    g_signal_connect(tvdata->select_all_check, "toggled", G_CALLBACK(on_select_all_toggled), tvdata);

    /* ---- NAME COMBO ---- */
    GtkListStore *name_store = gtk_list_store_new(1, G_TYPE_STRING);
    tvdata->name_combo = GTK_COMBO_BOX(gtk_combo_box_new_with_entry());
    gtk_combo_box_set_model(tvdata->name_combo, GTK_TREE_MODEL(name_store));
    g_object_unref(name_store);
    gtk_combo_box_set_entry_text_column(tvdata->name_combo, 0);
    gtk_box_pack_start(GTK_BOX(top_hbox), GTK_WIDGET(tvdata->name_combo), TRUE, TRUE, 5);
    g_signal_connect(tvdata->name_combo, "changed", G_CALLBACK(on_name_combo_changed), tvdata);

    GtkWidget *save_btn = gtk_button_new_with_label(_("Save As .GWY"));
    gtk_box_pack_end(GTK_BOX(top_hbox), save_btn, FALSE, FALSE, 0);
    g_signal_connect(save_btn, "clicked", G_CALLBACK(save_selected_gwy), tvdata);

    tvdata->apply_range_btn = apply_range_btn;
    tvdata->invert_btn = invert_btn;
    tvdata->full_btn = full_btn;
    tvdata->zero_btn = zero_btn;
    tvdata->rename_btn = rename_btn;
    tvdata->save_btn = save_btn;

    /* ---- TREEVIEW ---- */
    tvdata->scrolled = build_treeview(&tvdata->store);
    g_object_ref_sink(tvdata->scrolled);
    gtk_box_pack_start(GTK_BOX(right_vbox), tvdata->scrolled, TRUE, TRUE, 2);
    treeview_right = gtk_bin_get_child(GTK_BIN(tvdata->scrolled));

    tvdata->status_label = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(tvdata->status_label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(right_vbox), tvdata->status_label, FALSE, FALSE, 8);
    set_widget_as_warning_message(tvdata->status_label);

    /* ---- CONNECT CHECKBOX ---- */
    GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(treeview_right), 0);
    GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(col));
    GtkCellRenderer *toggle = GTK_CELL_RENDERER(cells->data);
    g_signal_connect(toggle, "toggled", G_CALLBACK(on_check_toggled), tvdata);
    g_list_free(cells);

    gtk_tree_view_expand_all(GTK_TREE_VIEW(treeview_right));

    /* ---- POPULATE NAME COMBO ---- */
    populate_name_combo(tvdata);
    update_sensitivity(tvdata);

    /* ---- RUN ---- */
    GwyDialogOutcome outcome = gwy_dialog_run(GWY_DIALOG(dialog));

    /* ---- CLEANUP ---- */
    if (tvdata->name_to_rows) {
       GList *keys = g_hash_table_get_keys(tvdata->name_to_rows);
       GList *l;

       for (l = keys; l; l = l->next) {
           GList *rows = g_hash_table_lookup(tvdata->name_to_rows, l->data);
           g_list_free_full(rows, g_free);
       }
       g_list_free(keys);
       g_hash_table_destroy(tvdata->name_to_rows);
    }

    g_signal_handlers_disconnect_by_func(gwy_gradients(),
                                         G_CALLBACK(load_gradients),
                                         tvdata);

    g_free(tvdata->last_selected);

    g_object_unref(tvdata->palette_store);
    g_free(tvdata);

    return outcome;
}

static void
collect_containers(GwyContainer *c, gpointer user_data)
{
    GList **list = (GList **)user_data;
    *list = g_list_prepend(*list, g_object_ref(c));
}

static GtkWidget *
build_treeview(GtkTreeStore **pstore)
{
    GtkWidget *scrolled, *treeview;
    GtkTreeStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;
    GtkTreeIter file_iter, chan_iter;
    GList *containers = NULL;
    GList *l;
    gint i;

    store = gtk_tree_store_new(N_COLS,
                               G_TYPE_BOOLEAN,
                               G_TYPE_STRING,
                               G_TYPE_POINTER,
                               G_TYPE_INT,
                               G_TYPE_BOOLEAN,
                               G_TYPE_STRING,
                               G_TYPE_STRING);

    /* Iterate over all Gwyddion containers */
    gwy_app_data_browser_foreach(collect_containers, &containers);

    for (l = containers; l; l = l->next) {
        GwyContainer *c = GWY_CONTAINER(l->data);
        const gchar *filename = (const gchar*)gwy_container_get_string_by_name(c, "/filename");
        if (!filename) {
            continue;
        }

        gtk_tree_store_append(store, &file_iter, NULL);
        gtk_tree_store_set(store, &file_iter,
                           COL_CHECK, FALSE,
                           COL_NAME, g_markup_printf_escaped("<b>%s</b>", filename),
                           COL_CONTAINER, g_object_ref(c),
                           COL_ID, -1,
                           COL_IS_CHANNEL, FALSE,
                           COL_CLEAN_TITLE, g_strdup(filename),
                           COL_FILENAME, g_strdup(filename),
                           -1);

        gint *ids = gwy_app_data_browser_get_data_ids(c);
        for (i = 0; ids && ids[i] != -1; i++) {
            GQuark title_quark = gwy_app_get_data_title_key_for_id(ids[i]);
            const gchar *title_key = g_quark_to_string(title_quark);
            const gchar *title = (const gchar*)gwy_container_get_string_by_name(c, title_key);
            if (!title) {
                title = _("Untitled");
            }

            gtk_tree_store_append(store, &chan_iter, &file_iter);
            gtk_tree_store_set(store, &chan_iter,
                               COL_CHECK, FALSE,
                               COL_NAME, g_markup_printf_escaped(" %s", title),
                               COL_CONTAINER, c,
                               COL_ID, ids[i],
                               COL_IS_CHANNEL, TRUE,
                               COL_CLEAN_TITLE, g_strdup(title),
                               COL_FILENAME, g_strdup(filename),
                               -1);
        }
        g_free(ids);
    }

    g_list_free_full(containers, g_object_unref);

    treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    *pstore = store;
    g_object_unref(store);

    renderer = gtk_cell_renderer_toggle_new();
    col = gtk_tree_view_column_new_with_attributes("", renderer,
                                                   "active", COL_CHECK,
                                                   "visible", COL_IS_CHANNEL,
                                                   NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(_("Channel"), renderer,
                                                   "markup", COL_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);

    gtk_tree_view_expand_all(GTK_TREE_VIEW(treeview));

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), treeview);

    return scrolled;
}

static void
on_check_toggled(G_GNUC_UNUSED GtkCellRendererToggle *cell,
                 gchar *path_str,
                 gpointer user_data)
{
    TreeViewData *tvdata = (TreeViewData *)user_data;
    GtkTreeStore *store = tvdata->store;
    GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
    GtkTreeIter iter;
    gboolean all_checked = TRUE;
    GtkTreeIter file_iter, chan_iter;
    gboolean valid_file = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &file_iter);

    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, path)) {
        gtk_tree_path_free(path);
        return;
    }

    gboolean is_channel;
    gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_IS_CHANNEL, &is_channel, -1);
    if (!is_channel) {
        gtk_tree_path_free(path);
        return;
    }

    gboolean checked;
    gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_CHECK, &checked, -1);
    checked = !checked;
    gtk_tree_store_set(store, &iter, COL_CHECK, checked, -1);

    /* Block signal to avoid recursion */
    g_signal_handlers_block_by_func(tvdata->select_all_check,
                                    G_CALLBACK(on_select_all_toggled),
                                    tvdata);

    /* Check if all channels are selected */
    while (valid_file && all_checked) {
        if (gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &chan_iter, &file_iter)) {
            do {
                gboolean chan_checked, chan_is_channel;
                gtk_tree_model_get(GTK_TREE_MODEL(store), &chan_iter,
                                   COL_CHECK, &chan_checked,
                                   COL_IS_CHANNEL, &chan_is_channel,
                                   -1);
                if (chan_is_channel && !chan_checked) {
                    all_checked = FALSE;
                    break;
                }
            } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &chan_iter));
        }
        valid_file = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &file_iter);
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tvdata->select_all_check), all_checked);

    /* Unblock signal */
    g_signal_handlers_unblock_by_func(tvdata->select_all_check,
                                      G_CALLBACK(on_select_all_toggled),
                                      tvdata);

    gtk_tree_path_free(path);
    update_sensitivity(tvdata);
}

static gboolean
apply_foreach(GtkTreeModel *model,
              G_GNUC_UNUSED GtkTreePath *path,
              GtkTreeIter *iter,
              gpointer data)
{
    TreeViewData *tvdata = (TreeViewData *)data;
    gboolean checked, is_channel;
    GwyContainer *c;
    gint id;

    gtk_tree_model_get(model, iter,
                       COL_CHECK, &checked,
                       COL_IS_CHANNEL, &is_channel,
                       COL_CONTAINER, &c,
                       COL_ID, &id,
                       -1);

    if (checked && is_channel && c && id >= 0) {
        /* Retrieve selected gradient */
        gchar *selected = NULL;
        GtkTreeIter combo_iter;

        if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(tvdata->palette_combo), &combo_iter)) {
            gtk_tree_model_get(gtk_combo_box_get_model(GTK_COMBO_BOX(tvdata->palette_combo)),
                               &combo_iter, 2, &selected, -1);
        }
        if (!selected || !*selected) {
            selected = g_strdup("Gwyddion.net");
        }

        /* Set selected palette for this channel */
        gchar *key = g_strdup_printf("/%d/base/palette", id);
        gwy_container_set_string_by_name(c, key, (const guchar *)g_strdup(selected));
        g_free(key);

        tvdata->applied_count++;
        g_free(selected);
    }

    return FALSE;
}

static void
apply_changes(G_GNUC_UNUSED GtkWidget *button, gpointer user_data)
{
    TreeViewData *tvdata = (TreeViewData *)user_data;

    tvdata->applied_count = 0;
    gtk_tree_model_foreach(GTK_TREE_MODEL(tvdata->store), apply_foreach, tvdata);
}