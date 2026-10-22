/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <gtk/gtk.h>
#include "cc-keyboard-item.h"
#include "cc-keyboard-manager.h"

static void
shortcut_added_cb (CcKeyboardManager *manager,
                   CcKeyboardItem    *item,
                   const gchar       *section_id,
                   const gchar       *section_title,
                   GPtrArray         *shortcuts)
{
  g_ptr_array_add (shortcuts, g_strdup (section_title));
}

/* The sections the manager offers when XDG_CURRENT_DESKTOP is @desktop */
static GPtrArray *
load_sections (const gchar *desktop)
{
  g_autoptr(CcKeyboardManager) manager = NULL;
  GPtrArray *sections = g_ptr_array_new_with_free_func (g_free);

  g_setenv ("XDG_CURRENT_DESKTOP", desktop, TRUE);
  manager = cc_keyboard_manager_new ();
  g_signal_connect (manager, "shortcut-added", G_CALLBACK (shortcut_added_cb), sections);
  cc_keyboard_manager_load_shortcuts (manager);

  return sections;
}

static gboolean
has_section (GPtrArray   *sections,
             const gchar *section)
{
  return g_ptr_array_find_with_equal_func (sections, section, g_str_equal, NULL);
}

static gboolean
have_schemas (void)
{
  g_autoptr(GSettingsSchema) schema = NULL;

  /* CcKeyboardManager needs the media-keys schema */
  schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
                                            "org.gnome.settings-daemon.plugins.media-keys",
                                            TRUE);
  if (schema == NULL)
    g_test_skip ("gnome-settings-daemon schemas are not installed");
  return schema != NULL;
}

/* Keybinding files that name a window manager (wm_name) are only shown in
 * sessions where that window manager handles the keys. */
static void
test_wm_keybindings (void)
{
  g_autoptr(GPtrArray) gnome = NULL;
  g_autoptr(GPtrArray) regolith_x11 = NULL;
  g_autoptr(GPtrArray) regolith_wayland = NULL;

  if (!have_schemas ())
    return;

  /* GNOME: GNOME Shell and Mutter, as upstream */
  gnome = load_sections ("GNOME");
  g_assert_true (has_section (gnome, "Test Any Section"));
  g_assert_true (has_section (gnome, "Test Shell Section"));
  g_assert_true (has_section (gnome, "Test Mutter Section"));
  g_assert_false (has_section (gnome, "Test Flashback Section"));

  /* Regolith X11: GNOME Flashback takes the screenshot keys, i3 the rest */
  regolith_x11 = load_sections ("Regolith:GNOME-Flashback:GNOME");
  g_assert_true (has_section (regolith_x11, "Test Any Section"));
  g_assert_true (has_section (regolith_x11, "Test Flashback Section"));
  g_assert_false (has_section (regolith_x11, "Test Shell Section"));
  g_assert_false (has_section (regolith_x11, "Test Mutter Section"));

  /* Regolith Wayland: sway handles all window manager keys */
  regolith_wayland = load_sections ("Regolith-Wayland:GNOME:sway");
  g_assert_true (has_section (regolith_wayland, "Test Any Section"));
  g_assert_false (has_section (regolith_wayland, "Test Flashback Section"));
  g_assert_false (has_section (regolith_wayland, "Test Shell Section"));
  g_assert_false (has_section (regolith_wayland, "Test Mutter Section"));
}

int
main (int argc, char **argv)
{
  /* Only read keybinding files from the test data, so the result does not
   * depend on what is installed. Must be set before anything reads the
   * system data directories; GSettings schemas still come from the system. */
  g_setenv ("XDG_DATA_DIRS", TEST_SRCDIR "/data", TRUE);
  g_setenv ("GSETTINGS_SCHEMA_DIR", GSETTINGS_SCHEMAS_DIR, TRUE);
  g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
  g_setenv ("GDK_BACKEND", "x11", TRUE);
  g_setenv ("LC_ALL", "C", TRUE);

  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func ("/keyboard/manager/wm-keybindings", test_wm_keybindings);

  return g_test_run ();
}
