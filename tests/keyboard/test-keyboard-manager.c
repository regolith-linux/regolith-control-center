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
  g_ptr_array_add (shortcuts,
                   g_strdup_printf ("%s: %s", section_title,
                                    cc_keyboard_item_get_description (item)));
}

static gboolean
has_shortcut (GPtrArray   *shortcuts,
              const gchar *shortcut)
{
  return g_ptr_array_find_with_equal_func (shortcuts, shortcut, g_str_equal, NULL);
}

/* Keybinding files are read from regolith-control-center/keybindings and from
 * gnome-control-center/keybindings, where other packages install theirs. A file
 * name found in both directories is only loaded from the first one. */
static void
test_keybinding_dirs (void)
{
  g_autoptr(CcKeyboardManager) manager = NULL;
  g_autoptr(GPtrArray) shortcuts = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GSettingsSchema) schema = NULL;

  /* CcKeyboardManager needs the media-keys schema */
  schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
                                            "org.gnome.settings-daemon.plugins.media-keys",
                                            TRUE);
  if (schema == NULL)
    {
      g_test_skip ("gnome-settings-daemon schemas are not installed");
      return;
    }

  manager = cc_keyboard_manager_new ();
  g_signal_connect (manager, "shortcut-added", G_CALLBACK (shortcut_added_cb), shortcuts);
  cc_keyboard_manager_load_shortcuts (manager);

  g_assert_true (has_shortcut (shortcuts, "Test Own Section: Test own shortcut"));
  g_assert_true (has_shortcut (shortcuts, "Test Other Package Section: Test other package shortcut"));
  g_assert_false (has_shortcut (shortcuts, "Test Duplicate Section: Test duplicate shortcut"));
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

  g_test_add_func ("/keyboard/manager/keybinding-dirs", test_keybinding_dirs);

  return g_test_run ();
}
