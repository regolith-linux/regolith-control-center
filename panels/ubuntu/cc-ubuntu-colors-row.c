/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2022 Canonical Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Marco Trevisan <marco.trevisan@canonical.com>
 *
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "cc-ubuntu-colors-row.h"
#include "cc-ubuntu-resources.h"

#include "gdesktop-enums.h"

#define INTERFACE_SCHEMA "org.gnome.desktop.interface"
#define GTK_THEME_KEY "gtk-theme"
#define CURSOR_THEME_KEY "cursor-theme"
#define ICON_THEME_KEY "icon-theme"
#define COLOR_SCHEME_KEY "color-scheme"

#define GEDIT_PREFRENCES_SCHEMA "org.gnome.gedit.preferences.editor"
#define GEDIT_THEME_KEY "scheme"

#define DEFAULT_ACCENT_COLOR "default"

struct _CcUbuntuColorsRow {
  AdwActionRow parent_instance;

  gboolean startup;

  GtkFlowBox      *color_box;
  GtkFlowBoxChild *accent_bark;
  GtkFlowBoxChild *accent_blue;
  GtkFlowBoxChild *accent_olive;
  GtkFlowBoxChild *accent_default;
  GtkFlowBoxChild *accent_magenta;
  GtkFlowBoxChild *accent_purple;
  GtkFlowBoxChild *accent_prussiangreen;
  GtkFlowBoxChild *accent_sage;
  GtkFlowBoxChild *accent_red;
  GtkFlowBoxChild *accent_viridian;

  GSettings *interface_settings;
  GSettings *gedit_settings;

  GDesktopColorScheme color_scheme;
};

G_DEFINE_FINAL_TYPE (CcUbuntuColorsRow, cc_ubuntu_colors_row, ADW_TYPE_ACTION_ROW)

enum
{
  CHANGED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS];

static GQuark accent_quark (void);
G_DEFINE_QUARK (accent-quark, accent);

static void
on_selected_color_changed (CcUbuntuColorsRow *self)
{
  GtkWidget *accent_color;
  g_autoptr(GString) gtk_theme = NULL;
  g_autoptr(GString) icon_theme = NULL;
  g_autoptr(GString) gedit_theme = NULL;
  g_autofree char *current_gtk_theme = NULL;
  g_autofree char *current_icon_theme = NULL;

  gtk_theme = g_string_new ("Yaru");
  icon_theme = g_string_new ("Yaru");
  gedit_theme = g_string_new ("Yaru");

  for (accent_color = gtk_widget_get_first_child (GTK_WIDGET (self->color_box));
       accent_color != NULL;
       accent_color = gtk_widget_get_next_sibling (accent_color))
    {
      const char *color = g_object_get_qdata (G_OBJECT (accent_color), accent_quark ());

      if (!gtk_widget_is_visible (accent_color))
        continue;

      if (color && gtk_flow_box_child_is_selected (GTK_FLOW_BOX_CHILD (accent_color)))
        {
          if (g_str_equal (color, DEFAULT_ACCENT_COLOR))
            break;

          g_string_append_printf (gtk_theme, "-%s", color);
          g_string_append_printf (icon_theme, "-%s", color);
          break;
        }
    }

  if (accent_color == NULL)
    {
      /* No selection, do not change anything */
      return;
    }

  if (self->color_scheme == G_DESKTOP_COLOR_SCHEME_PREFER_DARK)
    {
      g_string_append (gtk_theme, "-dark");
      g_string_append (gedit_theme, "-dark");
    }

  g_settings_delay (self->interface_settings);

  current_gtk_theme = g_settings_get_string (self->interface_settings, GTK_THEME_KEY);

  if (g_strcmp0 (current_gtk_theme, gtk_theme->str))
    {
      g_signal_emit (self, signals[CHANGED], 0);
      g_settings_set_string (self->interface_settings, GTK_THEME_KEY, gtk_theme->str);
    }

  current_icon_theme = g_settings_get_string (self->interface_settings, ICON_THEME_KEY);

  if (!current_icon_theme || !self->startup ||
      g_str_has_prefix (current_icon_theme, "Yaru"))
    g_settings_set_string (self->interface_settings, ICON_THEME_KEY, icon_theme->str);

  if (self->gedit_settings)
    {
      g_autoptr(GVariant) gedit_user_value = NULL;
      const char *gedit_theme_name = NULL;

      g_settings_delay (self->gedit_settings);
      gedit_user_value = g_settings_get_user_value (self->gedit_settings,
                                                    GEDIT_THEME_KEY);

      if (gedit_user_value)
        gedit_theme_name = g_variant_get_string (gedit_user_value, NULL);

      if (!gedit_theme_name || *gedit_theme_name == '\0' ||
          g_str_has_prefix (gedit_theme_name, "Yaru"))
        {
          /* Only change the gedit setting if user is using a Yaru theme, or unset */
          g_settings_set_string (self->gedit_settings, GEDIT_THEME_KEY,
                                 gedit_theme->str);
        }

        g_settings_apply (self->gedit_settings);
    }

  g_settings_apply (self->interface_settings);
}

static void
on_interface_settings_changed (CcUbuntuColorsRow *self)
{
  GtkWidget *color_item;
  g_autofree gchar *gtk_theme = NULL;
  g_autofree gchar *cursor_theme = NULL;
  g_autofree gchar *icon_theme = NULL;
  g_auto(GStrv) parts = NULL;
  const char *accent_color = DEFAULT_ACCENT_COLOR;
  gboolean is_dark = FALSE;

  gtk_theme = g_settings_get_string (self->interface_settings, GTK_THEME_KEY);
  cursor_theme = g_settings_get_string (self->interface_settings, CURSOR_THEME_KEY);
  icon_theme = g_settings_get_string (self->interface_settings, ICON_THEME_KEY);

  if (!g_str_has_prefix (gtk_theme, "Yaru"))
    {
      g_warning ("No yaru theme selected!");

      gtk_flow_box_unselect_all (self->color_box);
      return;
    }

  is_dark = g_str_has_suffix (gtk_theme, "-dark");
  parts = g_strsplit (gtk_theme, "-", 3);

  switch (g_strv_length (parts))
    {
      case 3:
        g_return_if_fail (is_dark);
        accent_color = parts[1];
        break;
      case 2:
        if (!is_dark)
          accent_color = parts[1];
        break;
    }

  for (color_item = gtk_widget_get_first_child (GTK_WIDGET (self->color_box));
       color_item != NULL;
       color_item = gtk_widget_get_next_sibling (color_item))
    {
      GtkFlowBoxChild *item = GTK_FLOW_BOX_CHILD (color_item);
      const char *color = g_object_get_qdata (G_OBJECT (item), accent_quark ());

      if (!gtk_widget_is_visible (color_item))
        continue;

      if (g_strcmp0 (color, accent_color) == 0)
        {
          gtk_flow_box_select_child (self->color_box, item);
          break;
        }
    }
}

static gchar *
get_theme_dir (void)
{
  const gchar *var;

  var = g_getenv ("GTK_DATA_PREFIX");
  if (var == NULL)
    var = "/usr";

  return g_build_filename (var, "share", "themes", NULL);
}


#if (GTK_MINOR_VERSION % 2)
#define MINOR (GTK_MINOR_VERSION + 1)
#else
#define MINOR GTK_MINOR_VERSION
#endif


static gchar *
find_theme_dir (const gchar *dir,
                const gchar *subdir,
                const gchar *name,
                const gchar *variant)
{
  g_autofree gchar *file = NULL;
  g_autofree gchar *base = NULL;
  gchar *path;
  gint i;

  if (variant)
    file = g_strconcat ("gtk-", variant, ".css", NULL);
  else
    file = g_strdup ("gtk.css");

  if (subdir)
    base = g_build_filename (dir, subdir, name, NULL);
  else
    base = g_build_filename (dir, name, NULL);

  for (i = MINOR; i >= 0; i = i - 2) {
    g_autofree gchar *subsubdir = NULL;

    if (i < 14)
      i = 0;

    subsubdir = g_strdup_printf ("gtk-4.%d", i);
    path = g_build_filename (base, subsubdir, file, NULL);

    if (g_file_test (path, G_FILE_TEST_EXISTS))
      break;

    g_free (path);
    path = NULL;
  }

  return path;
}

#undef MINOR

static gchar *
find_theme (const gchar *name,
            const gchar *variant)
{
  g_autofree gchar *dir = NULL;
  const gchar *const *dirs;
  gchar *path;
  gint i;

  /* First look in the user's data directory */
  path = find_theme_dir (g_get_user_data_dir (), "themes", name, variant);
  if (path)
    return path;

  /* Next look in the user's home directory */
  path = find_theme_dir (g_get_home_dir (), ".themes", name, variant);
  if (path)
    return path;

  /* Look in system data directories */
  dirs = g_get_system_data_dirs ();
  for (i = 0; dirs[i]; i++) {
    path = find_theme_dir (dirs[i], "themes", name, variant);
    if (path)
      return path;
  }

  /* Finally, try in the default theme directory */
  dir = get_theme_dir ();
  path = find_theme_dir (dir, NULL, name, variant);

  return path;
}

/* Courtesy of libhandy... */
static gboolean
check_theme_exists (const gchar *name,
                    const gchar *variant)
{
  g_autofree gchar *resource_path = NULL;
  g_autofree gchar *path = NULL;

  /* try loading the resource for the theme. This is mostly meant for built-in
   * themes.
   */
  if (variant)
    resource_path = g_strdup_printf ("/org/gtk/libgtk/theme/%s/gtk-%s.css", name, variant);
  else
    resource_path = g_strdup_printf ("/org/gtk/libgtk/theme/%s/gtk.css", name);

  if (g_resources_get_info (resource_path, 0, NULL, NULL, NULL))
    return TRUE;

  g_clear_pointer (&resource_path, g_free);

  if (variant)
    resource_path = g_strdup_printf ("/com/ubuntu/themes/%s/4.0/gtk-%s.css", name, variant);
  else
    resource_path = g_strdup_printf ("/com/ubuntu/themes/%s/4.0/gtk.css", name);

  if (g_resources_get_info (resource_path, 0, NULL, NULL, NULL))
    return TRUE;

  /* Next try looking for files in the various theme directories. */
  path = find_theme (name, variant);

  return path != NULL;
}

static void
check_theme_accents_availability (CcUbuntuColorsRow *self)
{
  GtkWidget *accent_color;
  gint available_accents = 0;

  if (!check_theme_exists ("Yaru", NULL))
    g_critical ("No Yaru theme found");

  if (!check_theme_exists ("Yaru", "dark"))
    g_critical ("No Yaru-dark theme found");

  for (accent_color = gtk_widget_get_first_child (GTK_WIDGET (self->color_box));
       accent_color != NULL;
       accent_color = gtk_widget_get_next_sibling (accent_color))
    {
      GtkFlowBoxChild *item = GTK_FLOW_BOX_CHILD (accent_color);
      const char *accent = g_object_get_qdata (G_OBJECT (item), accent_quark ());
      g_autofree char *theme_name = NULL;

      if (g_strcmp0 (accent, DEFAULT_ACCENT_COLOR) == 0)
        continue;

      theme_name = g_strdup_printf("Yaru-%s", accent);

      if (check_theme_exists (theme_name, NULL) && check_theme_exists (theme_name, "dark"))
        available_accents++;
      else
        gtk_widget_set_visible (GTK_WIDGET (item), FALSE);
    }

  if (!available_accents)
    gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
}

static void
load_custom_css (CcUbuntuColorsRow *self)
{
  g_autoptr(GtkCssProvider) provider = NULL;

  /* use custom CSS */
  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider,
    "/org/gnome/control-center/ubuntu/cc-ubuntu-colors-row.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
cc_ubuntu_colors_row_init (CcUbuntuColorsRow *self)
{
  GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default ();
  g_autoptr(GSettingsSchema) schema = NULL;
  const gchar *desktop_list;
  g_auto(GStrv) desktops = NULL;

  desktop_list = g_getenv ("XDG_CURRENT_DESKTOP");
  if (desktop_list != NULL)
    desktops = g_strsplit (desktop_list, ":", -1);

  if (desktops == NULL ||
      !g_strv_contains ((const gchar * const *) desktops, "ubuntu"))
    {
      gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
      return;
    }

  self->startup = TRUE;

  g_resources_register (cc_ubuntu_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (self));

  g_object_set_qdata (G_OBJECT (self->accent_bark), accent_quark (), "bark");
  g_object_set_qdata (G_OBJECT (self->accent_blue), accent_quark (), "blue");
  g_object_set_qdata (G_OBJECT (self->accent_magenta), accent_quark (), "magenta");
  g_object_set_qdata (G_OBJECT (self->accent_olive), accent_quark (), "olive");
  g_object_set_qdata (G_OBJECT (self->accent_default), accent_quark (), "default");
  g_object_set_qdata (G_OBJECT (self->accent_purple), accent_quark (), "purple");
  g_object_set_qdata (G_OBJECT (self->accent_prussiangreen), accent_quark (), "prussiangreen");
  g_object_set_qdata (G_OBJECT (self->accent_red), accent_quark (), "red");
  g_object_set_qdata (G_OBJECT (self->accent_sage), accent_quark (), "sage");
  g_object_set_qdata (G_OBJECT (self->accent_viridian), accent_quark (), "viridian");
  check_theme_accents_availability (self);

  self->interface_settings = g_settings_new (INTERFACE_SCHEMA);
  g_signal_connect_object (self->interface_settings, "changed::" GTK_THEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->interface_settings, "changed::" CURSOR_THEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->interface_settings, "changed::" ICON_THEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->interface_settings, "changed::" COLOR_SCHEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self,
                           G_CONNECT_SWAPPED);

  schema = g_settings_schema_source_lookup (schema_source, GEDIT_PREFRENCES_SCHEMA, TRUE);
  if (schema)
    {
      self->gedit_settings = g_settings_new (GEDIT_PREFRENCES_SCHEMA);
      g_signal_connect_object (self->gedit_settings, "changed::" GEDIT_THEME_KEY,
                               G_CALLBACK (on_interface_settings_changed), self, G_CONNECT_SWAPPED);
    }
  else
    {
      g_warning ("No gedit is installed here. Colors won't be updated. Please fix your installation.");
    }

  load_custom_css (self);
  on_interface_settings_changed (self);

  self->startup = FALSE;
}

static void
cc_ubuntu_colors_row_dispose (GObject *object)
{
  CcUbuntuColorsRow *self = CC_UBUNTU_COLORS_ROW (object);

  g_clear_object (&self->interface_settings);
  g_clear_object (&self->gedit_settings);

  G_OBJECT_CLASS (cc_ubuntu_colors_row_parent_class)->dispose (object);
}

static void
cc_ubuntu_colors_row_class_init (CcUbuntuColorsRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_ubuntu_colors_row_dispose;

  signals[CHANGED] = g_signal_new ("changed",
                                   CC_TYPE_UBUNTU_COLORS_ROW,
                                   G_SIGNAL_RUN_FIRST,
                                   0, NULL, NULL, NULL,
                                   G_TYPE_NONE,
                                   0);

  gtk_widget_class_set_template_from_resource (widget_class,
    "/org/gnome/control-center/ubuntu/cc-ubuntu-colors-row.ui");

  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, color_box);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, accent_bark);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, accent_blue);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, accent_olive);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, accent_default);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, accent_magenta);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, accent_purple);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, accent_prussiangreen);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, accent_red);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, accent_sage);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuColorsRow, accent_viridian);

  gtk_widget_class_bind_template_callback (widget_class, on_selected_color_changed);
}

void
cc_ubuntu_colors_row_set_color_scheme (CcUbuntuColorsRow *self,
                                       GDesktopColorScheme color_scheme)
{
  if (self->color_scheme == color_scheme)
    return;

  self->color_scheme = color_scheme;

  if (self->interface_settings)
    on_selected_color_changed (self);
}
