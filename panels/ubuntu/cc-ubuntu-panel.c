/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2017-2022 Canonical Ltd
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
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gio/gdesktopappinfo.h>

#include "cc-ubuntu-panel.h"
#include "cc-ubuntu-dock-dialog.h"
#include "cc-ubuntu-resources.h"
#include "shell/cc-application.h"
#include "shell/cc-object-storage.h"

#include "panels/display/cc-display-config-manager-dbus.h"
#include "panels/display/cc-display-config.h"

#define DING_UUID "ding@rastersoft.com"
#define DING_SCHEMA "org.gnome.shell.extensions.ding"

#define MIN_ICONSIZE 16.0
#define MAX_ICONSIZE 64.0
#define DEFAULT_ICONSIZE 48.0
#define ICONSIZE_KEY "dash-max-icon-size"

#define UBUNTU_DOCK_UUID "ubuntu-dock@ubuntu.com"
#define UBUNTU_DOCK_SCHEMA "org.gnome.shell.extensions.dash-to-dock"
#define UBUNTU_DOCK_ALL_MONITORS_KEY "multi-monitor"
#define UBUNTU_DOCK_PREFERRED_MONITOR_KEY "preferred-monitor"
#define UBUNTU_DOCK_PREFERRED_CONNECTOR_KEY "preferred-monitor-by-connector"

#define TILING_ASSISTANT_SCHEMA "org.gnome.shell.extensions.tiling-assistant"
#define TILING_ASSISTANT_UUID "tiling-assistant@ubuntu.com"

#define GNOME_SHELL_SCHEMA "org.gnome.shell"
#define GNOME_SHELL_ENABLED_EXTENSIONS "enabled-extensions"
#define GNOME_SHELL_DISABLED_EXTENSIONS "disabled-extensions"

#define SHELL_EXTENSION_STATE_UNINSTALLED 99

/*
 * This allows to migrate settings from 'preferred-monitor' to
 * 'preferred-monitor-by-connector', and can be removed after 22.04
 * simplifying all the logic, by relying on connector names only.
 */
#define UBUNTU_DOCK_MONITOR_INDEX_USE_CONNECTOR -2

struct _CcUbuntuPanel {
  CcPanel                 parent_instance;

  AdwPreferencesGroup    *ding_preferences_group;
  GtkSwitch              *ding_switch;
  GtkCenterBox           *no_settings_view;
  AdwComboRow            *ding_icon_size_row;
  AdwComboRow            *ding_icons_new_alignment_row;
  AdwSwitchRow           *ding_showhome_switch;
  AdwPreferencesGroup    *dock_preferences_group;
  GtkSwitch              *dock_switch;
  AdwSwitchRow           *dock_autohide_switch;
  AdwSwitchRow           *dock_extendheight_switch;
  AdwComboRow            *dock_monitor_row;
  GListStore             *dock_monitors_list;
  AdwComboRow            *dock_position_row;
  GtkAdjustment          *icon_size_adjustment;
  GtkScale               *icon_size_scale;
  AdwPreferencesGroup    *tiling_assistant_preferences_group;
  GtkSwitch              *tiling_assistant_switch;
  AdwSwitchRow           *tiling_assistant_popup_switch;
  AdwSwitchRow           *tiling_assistant_tiling_group;

  GSettings              *ding_settings;
  GSettings              *dock_settings;
  GSettings              *interface_settings;
  GSettings              *gedit_settings;
  GSettings              *gnome_shell_settings;
  GSettings              *tiling_assistant_settings;
  CcDisplayConfigManager *display_config_manager;
  CcDisplayConfig        *display_current_config;
  GDBusProxy             *shell_proxy;
  GDBusProxy             *shell_extensions_proxy;

  gboolean                updating;
};

CC_PANEL_REGISTER (CcUbuntuPanel, cc_ubuntu_panel);

static void monitor_labeler_hide (CcUbuntuPanel *self);
static void update_dock_monitor_combo_row_selection (CcUbuntuPanel *self);

static void
cc_ubuntu_panel_dispose (GObject *object)
{
  CcUbuntuPanel *self = CC_UBUNTU_PANEL (object);

  monitor_labeler_hide (self);

  g_clear_object (&self->gnome_shell_settings);
  g_clear_object (&self->tiling_assistant_settings);
  g_clear_object (&self->ding_settings);
  g_clear_object (&self->dock_settings);
  g_clear_object (&self->dock_monitors_list);
  g_clear_object (&self->interface_settings);
  g_clear_object (&self->gedit_settings);
  g_clear_object (&self->display_current_config);
  g_clear_object (&self->display_config_manager);
  g_clear_object (&self->shell_extensions_proxy);
  g_clear_object (&self->shell_proxy);

  G_OBJECT_CLASS (cc_ubuntu_panel_parent_class)->dispose (object);
}

static gboolean
update_panel_visibilty (CcUbuntuPanel *self)
{
  gboolean enabled = FALSE;

  enabled |= gtk_widget_is_visible (GTK_WIDGET (self->dock_preferences_group));
  enabled |= gtk_widget_is_visible (GTK_WIDGET (self->ding_preferences_group));
  enabled |= gtk_widget_is_visible (GTK_WIDGET (self->tiling_assistant_preferences_group));

  gtk_widget_set_visible (GTK_WIDGET (self->no_settings_view), !enabled);

  return enabled;
}

static void
monitor_labeler_hide (CcUbuntuPanel *self)
{
  if (!self->shell_proxy)
    return;

  g_dbus_proxy_call (self->shell_proxy,
                     "HideMonitorLabels",
                     NULL, G_DBUS_CALL_FLAGS_NONE,
                     -1, NULL, NULL, NULL);
}

static void
monitor_labeler_show (CcUbuntuPanel *self)
{
  GList *outputs, *l;
  GVariantBuilder builder;
  gint number = 0;
  guint n_monitors = 0;

  if (!self->shell_proxy || !self->display_current_config)
    return;

  outputs = cc_display_config_get_ui_sorted_monitors (self->display_current_config);
  if (!outputs)
    return;

  if (cc_display_config_is_cloning (self->display_current_config))
    return monitor_labeler_hide (self);

  g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
  g_variant_builder_open (&builder, G_VARIANT_TYPE_ARRAY);

  for (l = outputs; l != NULL; l = l->next)
    {
      CcDisplayMonitor *output = l->data;

      if (!cc_display_monitor_is_active (output))
        continue;

      number = cc_display_monitor_get_ui_number (output);
      if (number == 0)
        continue;

      g_variant_builder_add (&builder, "{sv}",
                             cc_display_monitor_get_connector_name (output),
                             g_variant_new_int32 (number));
      n_monitors++;
    }

  g_variant_builder_close (&builder);

  if (number < 2 || n_monitors < 2)
    {
      g_variant_builder_clear (&builder);
      return monitor_labeler_hide (self);
    }

  g_dbus_proxy_call (self->shell_proxy,
                     "ShowMonitorLabels",
                     g_variant_builder_end (&builder),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1, NULL, NULL, NULL);
}

static void
ensure_monitor_labels (CcUbuntuPanel *self)
{
  g_autoptr(GList) windows = NULL;
  GList *w;

  windows = gtk_window_list_toplevels ();

  for (w = windows; w; w = w->next)
    {
      if (gtk_window_is_active (GTK_WINDOW (w->data)))
        {
          monitor_labeler_show (self);
          break;
        }
    }

  if (!w)
    monitor_labeler_hide (self);
}

static void
shell_proxy_ready (GObject        *source,
                   GAsyncResult   *res,
                   CcUbuntuPanel *self)
{
  GDBusProxy *proxy;
  g_autoptr(GError) error = NULL;

  proxy = cc_object_storage_create_dbus_proxy_finish (res, &error);
  if (!proxy)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to contact gnome-shell: %s", error->message);
      return;
    }

  self->shell_proxy = g_steal_pointer (&proxy);

  ensure_monitor_labels (self);
}

static GList *
get_valid_monitors (CcUbuntuPanel   *self,
                    gint            *n_monitors,
                    gint            *primary_index)
{
  CcDisplayMonitor *primary_monitor;
  GList *config_monitors = NULL;
  GList *valid_monitors, *l;
  gint n_valid_monitors;

  config_monitors = cc_display_config_get_monitors (self->display_current_config);
  primary_monitor = NULL;
  valid_monitors = NULL;
  n_valid_monitors = 0;

  for (l = config_monitors; l != NULL; l = l->next)
    {
      CcDisplayMonitor *monitor = l->data;

      if (!cc_display_monitor_is_active (monitor))
        continue;

      /* The default monitors list uses reversed order, so prepend to
       * set it back to mutter order */
      valid_monitors = g_list_prepend (valid_monitors, monitor);

      if (cc_display_monitor_is_primary (monitor))
        primary_monitor = monitor;

      n_valid_monitors++;
    }

  if (n_monitors)
    *n_monitors = n_valid_monitors;

  if (primary_index)
    *primary_index = g_list_index (valid_monitors, primary_monitor);

  return valid_monitors;
}

static int
ui_sort_monitor (gconstpointer a, gconstpointer b)
{
  CcDisplayMonitor *monitor_a = (CcDisplayMonitor *) a;
  CcDisplayMonitor *monitor_b = (CcDisplayMonitor *) b;

  return cc_display_monitor_get_ui_number (monitor_a) -
         cc_display_monitor_get_ui_number (monitor_b);
}

static GList *
get_valid_monitors_sorted (CcUbuntuPanel   *self,
                           gint            *n_monitors,
                           gint            *primary_index)
{
  GList *monitors = get_valid_monitors (self, n_monitors, primary_index);

  return g_list_sort (monitors, ui_sort_monitor);
}

static int
dock_monitor_to_id (gint index,
                    gint primary_monitor,
                    gint n_monitors)
{
  if (index < 0)
    return -1;

  /* The The dock uses the Gdk index for monitors, where the primary monitor
   * always has index 0, so let's follow what dash-to-dock does in docking.js
   * (as part of _createDocks) */
  return (primary_monitor + index) % n_monitors;
}

typedef enum
{
  GSD_UBUNTU_DOCK_MONITOR_ALL,
  GSD_UBUNTU_DOCK_MONITOR_PRIMARY,
} GsdUbuntuDockMonitor;

static char *
get_dock_monitor_value_object_name (GObject *object_value,
                                    CcUbuntuPanel *self)
{
  if (GTK_IS_STRING_OBJECT (object_value))
    {
      GtkStringObject *string_object = GTK_STRING_OBJECT (object_value);

      return g_strdup (gtk_string_object_get_string (string_object));
    }

  if (CC_IS_DISPLAY_MONITOR (object_value))
    {
      CcDisplayMonitor *monitor = CC_DISPLAY_MONITOR (object_value);
      int monitor_number = cc_display_monitor_get_ui_number (monitor);
      const char *monitor_name = cc_display_monitor_get_display_name (monitor);

      if (gtk_widget_get_state_flags (GTK_WIDGET (self)) & GTK_STATE_FLAG_DIR_LTR)
        return g_strdup_printf ("%d. %s", monitor_number, monitor_name);
      else
        return g_strdup_printf ("%s .%d", monitor_name, monitor_number);
    }

  g_return_val_if_reached (NULL);
}

static GtkStringObject *
new_take_string_object (char *label)
{
  g_autofree char *string = g_steal_pointer (&label);
  return gtk_string_object_new (string);
}

static void
populate_dock_monitor_combo_row (CcUbuntuPanel *self)
{
  g_autoptr(CcDisplayMonitor) primary_monitor = NULL;
  g_autoptr(GList) valid_monitors = NULL;
  g_autoptr(GtkStringObject) primary_value_object = NULL;
  g_autoptr(GtkStringObject) all_displays_value_object = NULL;
  GList *l;
  gint index;

  if (self->display_config_manager == NULL)
    return;

  g_list_store_remove_all (self->dock_monitors_list);

  valid_monitors = get_valid_monitors_sorted (self, NULL, NULL);
  gtk_widget_set_visible (GTK_WIDGET (self->dock_monitor_row), valid_monitors != NULL);

  if (!valid_monitors)
    {
      gtk_widget_set_sensitive (GTK_WIDGET (self->dock_monitor_row),
                                gtk_switch_get_active (self->dock_switch));
      return;
    }

  all_displays_value_object = gtk_string_object_new (_("All displays"));
  g_list_store_insert (self->dock_monitors_list,
                       GSD_UBUNTU_DOCK_MONITOR_ALL,
                       all_displays_value_object);

  for (l = valid_monitors, index = 0; l != NULL; l = l->next, index++)
    {
      CcDisplayMonitor *monitor = l->data;

      if (cc_display_monitor_is_primary (monitor))
        g_set_object (&primary_monitor, monitor);

      g_list_store_append (self->dock_monitors_list, monitor);
    }

  if (primary_monitor)
    {
      int ui_number = cc_display_monitor_get_ui_number (primary_monitor);

      if (gtk_widget_get_state_flags (GTK_WIDGET (self)) & GTK_STATE_FLAG_DIR_LTR)
        {
          primary_value_object = new_take_string_object (
            g_strdup_printf ("%s (%d)", _("Primary Display"), ui_number));
        }
      else
        {
          primary_value_object = new_take_string_object (
            g_strdup_printf ("(%d) %s", ui_number, _("Primary Display")));
        }
    }
  else
    {
      primary_value_object = gtk_string_object_new (_("Primary Display"));
    }

  g_list_store_insert (self->dock_monitors_list,
                       GSD_UBUNTU_DOCK_MONITOR_PRIMARY,
                       primary_value_object);

  gtk_widget_set_sensitive (GTK_WIDGET (self->dock_monitor_row),
                            gtk_switch_get_active (self->dock_switch));
}

static void
on_screen_changed (CcUbuntuPanel *self)
{
  g_autoptr(CcDisplayConfig) current = NULL;

  if (self->display_config_manager == NULL || !self->dock_settings)
    return;

  current = cc_display_config_manager_get_current (self->display_config_manager);
  if (current == NULL)
    return;

  self->updating = TRUE;

  g_set_object (&self->display_current_config, current);

  populate_dock_monitor_combo_row (self);
  ensure_monitor_labels (self);

  self->updating = FALSE;

  update_dock_monitor_combo_row_selection (self);
}

static void
session_bus_ready (GObject        *source,
                   GAsyncResult   *res,
                   gpointer        user_data)
{
  CcUbuntuPanel *self = user_data;
  GDBusConnection *bus;
  g_autoptr(GError) error = NULL;

  bus = g_bus_get_finish (res, &error);
  if (!bus)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to get session bus: %s", error->message);
      return;
    }

  if (self->dock_settings)
    {
      self->display_config_manager = cc_display_config_manager_dbus_new ();
      g_signal_connect_object (self->display_config_manager, "changed",
                              G_CALLBACK (on_screen_changed),
                              self,
                              G_CONNECT_SWAPPED);
    }
}

static void
update_extension_widgets (GtkWidget  *extension_group,
                          GtkWidget  *extension_switch,
                          const char *uuid,
                          int         extension_state,
                          gboolean    always_show_switch)
{
  enum {
    STATE_ENABLED = 1,
    STATE_DISABLED,
    STATE_ERROR,
    STATE_OUT_OF_DATE,
    STATE_DOWNLOADING,
    STATE_INITIALIZED,
    STATE_DISABLING,
    STATE_ENABLING,

    STATE_UNINSTALLED = SHELL_EXTENSION_STATE_UNINSTALLED,
  };

  switch (extension_state)
    {
      case STATE_ENABLED:
      case STATE_ENABLING:
      case STATE_DOWNLOADING:
        g_debug ("Extension %s is enabled (state %d)", uuid, extension_state);
        gtk_widget_set_visible (extension_switch, always_show_switch);
        break;
      case STATE_INITIALIZED:
      case STATE_DISABLED:
      case STATE_DISABLING:
        g_warning ("Extension %s is not enabled (state %d)", uuid, extension_state);
        gtk_widget_set_visible (extension_switch, TRUE);
        break;
      case STATE_OUT_OF_DATE:
      case STATE_UNINSTALLED:
      case STATE_ERROR:
        g_warning ("Extension %s is not available or invalid (state %d)",
                   uuid, extension_state);
        gtk_widget_set_visible (extension_group, FALSE);
        break;
      default:
        g_warning ("Unknown extension state for %s: %d\n", uuid, extension_state);
    }
}

static void
on_extensions_listed (GObject *source_object,
                      GAsyncResult *res,
                      gpointer user_data)
{
  CcUbuntuPanel *self = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) response = NULL;
  g_autoptr (GVariant) extensions = NULL;
  GVariantIter iter;
  GVariant *value;
  const char *uuid;

  response = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);

  if (error)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to get gnome-shell extensions: %s", error->message);
      return;
    }

  extensions = g_variant_get_child_value (response, 0);

  double ding_state = SHELL_EXTENSION_STATE_UNINSTALLED;
  double dock_state = SHELL_EXTENSION_STATE_UNINSTALLED;
  double tiling_state = SHELL_EXTENSION_STATE_UNINSTALLED;

  g_variant_iter_init (&iter, extensions);
  while (g_variant_iter_loop (&iter, "{&s@a{sv}}", &uuid, &value))
    {
      g_autoptr (GVariantDict) info = NULL;

      if (g_str_equal (uuid, UBUNTU_DOCK_UUID))
        {
          info = g_variant_dict_new (value);
          g_variant_dict_lookup (info, "state", "d", &dock_state);
        }
      else if (g_str_equal (uuid, DING_UUID))
        {
          info = g_variant_dict_new (value);
          g_variant_dict_lookup (info, "state", "d", &ding_state);
        }
      else if (g_str_equal (uuid, TILING_ASSISTANT_UUID))
        {
          info = g_variant_dict_new (value);
          g_variant_dict_lookup (info, "state", "d", &tiling_state);
        }
    }

  update_extension_widgets (GTK_WIDGET (self->ding_preferences_group),
                            GTK_WIDGET (self->ding_switch),
                            DING_UUID, ding_state, FALSE);

  update_extension_widgets (GTK_WIDGET (self->dock_preferences_group),
                            GTK_WIDGET (self->dock_switch),
                            UBUNTU_DOCK_UUID, dock_state, FALSE);

  update_extension_widgets (GTK_WIDGET (self->tiling_assistant_preferences_group),
                            GTK_WIDGET (self->tiling_assistant_switch),
                            TILING_ASSISTANT_UUID, tiling_state, TRUE);

  update_panel_visibilty (self);
}

static void
shell_extensions_update_visibilty (CcUbuntuPanel *self)
{
  g_dbus_proxy_call (self->shell_extensions_proxy,
                     "ListExtensions",
                     NULL, G_DBUS_CALL_FLAGS_NONE,
                     -1, cc_panel_get_cancellable (CC_PANEL (self)),
                     (GAsyncReadyCallback) on_extensions_listed, self);
}

static void
shell_extensions_proxy_ready (GObject        *source,
                              GAsyncResult   *res,
                              CcUbuntuPanel *self)
{
  GDBusProxy *proxy;
  g_autoptr(GError) error = NULL;

  proxy = cc_object_storage_create_dbus_proxy_finish (res, &error);
  if (!proxy)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to contact gnome-shell extensions: %s", error->message);
      return;
    }

  self->shell_extensions_proxy = g_steal_pointer (&proxy);
  shell_extensions_update_visibilty (self);
}

static void
icon_size_widget_refresh (CcUbuntuPanel *self)
{
  gint value = g_settings_get_int (self->dock_settings, ICONSIZE_KEY);
  gtk_adjustment_set_value (self->icon_size_adjustment, (gdouble) value / 2);
}

static gchar *
on_icon_size_format_value (GtkScale *scale,
                           gdouble value,
                           gpointer data)
{
  return g_strdup_printf ("%d", (int)value * 2);
}

static void
on_icon_size_adjustment_value_changed (CcUbuntuPanel *self)
{
  if (!self->dock_settings)
    return;

  gint value = (gint)gtk_adjustment_get_value (self->icon_size_adjustment) * 2;
  if (g_settings_get_int (self->dock_settings, ICONSIZE_KEY) != value)
    g_settings_set_int (self->dock_settings, ICONSIZE_KEY, value);
}

static gboolean
is_extension_disabled (CcUbuntuPanel *self,
                       const char    *uuid)
{
  g_auto(GStrv) disabled_extensions = NULL;

  disabled_extensions = g_settings_get_strv (self->gnome_shell_settings,
                                             GNOME_SHELL_DISABLED_EXTENSIONS);

  return g_strv_contains ((const gchar **) disabled_extensions, uuid);
}

static void
set_extension_enabled (CcUbuntuPanel *self,
                       const char    *uuid,
                       gboolean       enabled)
{
  g_auto(GStrv) disabled_extensions = NULL;
  g_auto(GStrv) enabled_extensions = NULL;
  g_autoptr(GPtrArray) enabled_extensions_list = NULL;
  g_autoptr(GPtrArray) disabled_extensions_list = NULL;
  guint idx;

  g_settings_delay (self->gnome_shell_settings);

  enabled_extensions = g_settings_get_strv (self->gnome_shell_settings,
                                            GNOME_SHELL_ENABLED_EXTENSIONS);

  disabled_extensions = g_settings_get_strv (self->gnome_shell_settings,
                                            GNOME_SHELL_DISABLED_EXTENSIONS);

  enabled_extensions_list = g_ptr_array_new_take_null_terminated (
        (gpointer *) g_steal_pointer (&enabled_extensions), g_free);
  disabled_extensions_list = g_ptr_array_new_take_null_terminated (
        (gpointer *) g_steal_pointer (&disabled_extensions), g_free);

  if (enabled)
    {
      if (!g_ptr_array_find_with_equal_func (enabled_extensions_list, uuid,
                                             g_str_equal, &idx))
        {
          g_ptr_array_add (enabled_extensions_list, g_strdup (uuid));

          g_settings_set_strv (self->gnome_shell_settings,
                               GNOME_SHELL_ENABLED_EXTENSIONS,
                               (const char **) enabled_extensions_list->pdata);
        }

      if (g_ptr_array_find_with_equal_func (disabled_extensions_list, uuid,
                                            g_str_equal, &idx))
        {
          g_ptr_array_remove_index (disabled_extensions_list, idx);

          g_settings_set_strv (self->gnome_shell_settings,
                               GNOME_SHELL_DISABLED_EXTENSIONS,
                               (const char **) disabled_extensions_list->pdata);
        }
    }
  else
    {
      if (g_ptr_array_find_with_equal_func (enabled_extensions_list, uuid,
                                            g_str_equal, &idx))
        {
          g_ptr_array_remove_index (enabled_extensions_list, idx);

          g_settings_set_strv (self->gnome_shell_settings,
                               GNOME_SHELL_ENABLED_EXTENSIONS,
                               (const char **) enabled_extensions_list->pdata);
        }

      if (!g_ptr_array_find_with_equal_func (disabled_extensions_list, uuid,
                                             g_str_equal, &idx))
        {
          g_ptr_array_add (disabled_extensions_list, g_strdup (uuid));

          g_settings_set_strv (self->gnome_shell_settings,
                               GNOME_SHELL_DISABLED_EXTENSIONS,
                               (const char **) disabled_extensions_list->pdata);
        }
    }

  g_settings_apply (self->gnome_shell_settings);
}

static void
update_dock_status (CcUbuntuPanel *self)
{
  gboolean extension_enabled = TRUE;

  if (is_extension_disabled (self, UBUNTU_DOCK_UUID))
    extension_enabled = FALSE;

  if (!extension_enabled &&
      !gtk_widget_is_visible (GTK_WIDGET (self->dock_switch)))
    {
      gtk_widget_set_visible (GTK_WIDGET (self->dock_switch), TRUE);
    }

  gtk_switch_set_active (self->dock_switch, extension_enabled);
}

static void
on_dock_switch_changed (CcUbuntuPanel *self)
{
  set_extension_enabled (self, UBUNTU_DOCK_UUID,
                         gtk_switch_get_active (self->dock_switch));
}

static void
update_ding_status (CcUbuntuPanel *self)
{
  gboolean extension_enabled = TRUE;

  if (is_extension_disabled (self, DING_UUID))
    extension_enabled = FALSE;

  if (!extension_enabled &&
      !gtk_widget_is_visible (GTK_WIDGET (self->ding_switch)))
    {
      gtk_widget_set_visible (GTK_WIDGET (self->ding_switch), TRUE);
    }

  gtk_switch_set_active (self->ding_switch, extension_enabled);
}

static void
on_ding_switch_changed (CcUbuntuPanel *self)
{
  set_extension_enabled (self, DING_UUID,
                         gtk_switch_get_active (self->ding_switch));
}

static void
update_tiling_assistant_status (CcUbuntuPanel *self)
{
  gboolean extension_enabled = TRUE;

  if (is_extension_disabled (self, TILING_ASSISTANT_UUID))
    extension_enabled = FALSE;

  gtk_switch_set_active (self->tiling_assistant_switch, extension_enabled);
}

static void
on_tiling_assistant_switch_changed (CcUbuntuPanel *self)
{
  set_extension_enabled (self, TILING_ASSISTANT_UUID,
                         gtk_switch_get_active (self->tiling_assistant_switch));
}

static void
on_dock_monitor_row_changed (CcUbuntuPanel *self)
{
  gboolean ubuntu_dock_on_all_monitors;
  g_autofree char *ubuntu_dock_current_connector = NULL;
  int selected;

  if (self->updating || !self->dock_settings)
    return;

  selected = adw_combo_row_get_selected (self->dock_monitor_row);
  if (selected < 0)
    return;

  ubuntu_dock_on_all_monitors =
    g_settings_get_boolean (self->dock_settings, UBUNTU_DOCK_ALL_MONITORS_KEY);
  ubuntu_dock_current_connector =
    g_settings_get_string (self->dock_settings, UBUNTU_DOCK_PREFERRED_CONNECTOR_KEY);
  if (selected == GSD_UBUNTU_DOCK_MONITOR_ALL)
    {
      if (!ubuntu_dock_on_all_monitors)
        {
          g_settings_set_boolean (self->dock_settings,
                                  UBUNTU_DOCK_ALL_MONITORS_KEY,
                                  TRUE);
          g_settings_apply (self->dock_settings);
        }
    }
  else
    {
      g_autoptr(GSettings) delayed_settings = g_settings_new (UBUNTU_DOCK_SCHEMA);
      g_settings_delay (delayed_settings);
      g_autofree char *connector_name = NULL;

      if (ubuntu_dock_on_all_monitors)
        g_settings_set_boolean (delayed_settings, UBUNTU_DOCK_ALL_MONITORS_KEY, FALSE);

      if (selected == GSD_UBUNTU_DOCK_MONITOR_PRIMARY)
        {
          connector_name = g_strdup ("primary");
        }
      else
        {
          g_autoptr(GObject) value_object = NULL;
          g_autoptr(CcDisplayMonitor) monitor = NULL;

          monitor = g_list_model_get_item (G_LIST_MODEL (self->dock_monitors_list),
                                           selected);
          connector_name = g_strdup (cc_display_monitor_get_connector_name (monitor));
        }

      if (g_strcmp0 (ubuntu_dock_current_connector, connector_name) != 0)
        {
          g_settings_set_int (delayed_settings, UBUNTU_DOCK_PREFERRED_MONITOR_KEY,
                                                UBUNTU_DOCK_MONITOR_INDEX_USE_CONNECTOR);
          g_settings_set_string (delayed_settings, UBUNTU_DOCK_PREFERRED_CONNECTOR_KEY,
                                 connector_name);
        }

      g_settings_apply (delayed_settings);
    }
}

static void
on_dock_behavior_row_activated (CcUbuntuPanel *self)
{
  CcShell *shell = cc_panel_get_shell (CC_PANEL (self));
  CcUbuntuDockDialog *dialog;

  dialog = cc_ubuntu_dock_dialog_new (self->dock_settings);
  gtk_window_set_transient_for (GTK_WINDOW (dialog),
                                GTK_WINDOW (cc_shell_get_toplevel (shell)));
  gtk_widget_set_visible (GTK_WIDGET (dialog), TRUE);
}

static CcDisplayMonitor *
get_dock_monitor (CcUbuntuPanel *self)
{
  g_autoptr(GList) monitors = NULL;
  int index;
  int n_monitors;
  int primary_monitor;

  monitors = get_valid_monitors_sorted (self, &n_monitors, &primary_monitor);
  index = g_settings_get_int (self->dock_settings, UBUNTU_DOCK_PREFERRED_MONITOR_KEY);

  if (index == UBUNTU_DOCK_MONITOR_INDEX_USE_CONNECTOR)
    {
      g_autofree char *connector = NULL;
      GList *l;
      int i;

      connector = g_settings_get_string (self->dock_settings,
                                         UBUNTU_DOCK_PREFERRED_CONNECTOR_KEY);

      for (l = monitors, i = 0; l; l = l->next, i++)
        {
          CcDisplayMonitor *monitor = l->data;
          const char *monitor_connector = cc_display_monitor_get_connector_name (monitor);
          if (g_strcmp0 (monitor_connector, connector) == 0)
            return g_object_ref (monitor);
        }
    }

  if (index < 0 || index >= n_monitors)
    return NULL;

  index = dock_monitor_to_id (index, primary_monitor, n_monitors);

  return g_object_ref (g_list_nth_data (monitors, index));
}

static gboolean
dock_placement_row_compare (gconstpointer a, gconstpointer b)
{
  GObject *row_object_a = G_OBJECT (a);
  GObject *row_object_b = G_OBJECT (b);

  if (row_object_a == NULL || row_object_b == NULL)
    return row_object_a == row_object_b;

  if (G_OBJECT_TYPE (row_object_a) != G_OBJECT_TYPE (row_object_b))
    return FALSE;

  if (CC_IS_DISPLAY_MONITOR (row_object_a))
    {
      return cc_display_monitor_get_ui_number (CC_DISPLAY_MONITOR (row_object_a)) ==
             cc_display_monitor_get_ui_number (CC_DISPLAY_MONITOR (row_object_b));
    }

  if (GTK_IS_STRING_OBJECT (row_object_a))
    {
      return g_strcmp0 (gtk_string_object_get_string (GTK_STRING_OBJECT (row_object_a)),
                        gtk_string_object_get_string (GTK_STRING_OBJECT (row_object_b))) == 0;
    }

  g_return_val_if_reached (FALSE);
}

static void
update_dock_monitor_combo_row_selection (CcUbuntuPanel *self)
{
  guint selection = GSD_UBUNTU_DOCK_MONITOR_PRIMARY;

  if (g_settings_get_boolean (self->dock_settings, UBUNTU_DOCK_ALL_MONITORS_KEY))
    {
      selection = GSD_UBUNTU_DOCK_MONITOR_ALL;
    }
  else
    {
      g_autoptr (CcDisplayMonitor) monitor = get_dock_monitor (self);

      if (monitor)
        {
          if (!g_list_store_find_with_equal_func (self->dock_monitors_list,
                                                  monitor,
                                                  dock_placement_row_compare,
                                                  &selection))
            selection = GSD_UBUNTU_DOCK_MONITOR_PRIMARY;
        }
    }

  adw_combo_row_set_selected (self->dock_monitor_row, selection);
}

static void
cc_ubuntu_panel_class_init (CcUbuntuPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_ubuntu_panel_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/ubuntu/cc-ubuntu-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, no_settings_view);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, ding_preferences_group);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, ding_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, ding_icon_size_row);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, ding_icons_new_alignment_row);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, ding_showhome_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_preferences_group);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_autohide_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_extendheight_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_monitor_row);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_position_row);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, icon_size_adjustment);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, icon_size_scale);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, tiling_assistant_preferences_group);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, tiling_assistant_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, tiling_assistant_popup_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, tiling_assistant_tiling_group);

  gtk_widget_class_bind_template_callback (widget_class, on_dock_monitor_row_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_dock_switch_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_dock_behavior_row_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_icon_size_adjustment_value_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_ding_switch_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_tiling_assistant_switch_changed);
}

static void
mapped_cb (CcUbuntuPanel *self)
{
  CcShell *shell;
  GtkWidget *toplevel;

  if (!self->dock_settings)
    return;

  shell = cc_panel_get_shell (CC_PANEL (self));
  toplevel = cc_shell_get_toplevel (shell);

  g_signal_handlers_disconnect_by_func (toplevel, mapped_cb, self);
  g_signal_connect_object (toplevel, "notify::has-toplevel-focus",
                           G_CALLBACK (ensure_monitor_labels), self,
                           G_CONNECT_SWAPPED);
}

typedef enum
{
  GSD_UBUNTU_DOCK_POSITION_TOP,
  GSD_UBUNTU_DOCK_POSITION_RIGHT,
  GSD_UBUNTU_DOCK_POSITION_BOTTOM,
  GSD_UBUNTU_DOCK_POSITION_LEFT,

  GSD_UBUNTU_DOCK_POSITION_FIRST = GSD_UBUNTU_DOCK_POSITION_RIGHT,
} GsdUbuntuDockPosition;

static GsdUbuntuDockPosition
get_dock_position_for_direction (CcUbuntuPanel         *self,
                                 GsdUbuntuDockPosition  position)
{
  if (gtk_widget_get_state_flags (GTK_WIDGET (self)) & GTK_STATE_FLAG_DIR_RTL)
    {
      switch (position)
        {
          case GSD_UBUNTU_DOCK_POSITION_RIGHT:
            position = GSD_UBUNTU_DOCK_POSITION_LEFT;
            break;
          case GSD_UBUNTU_DOCK_POSITION_LEFT:
            position = GSD_UBUNTU_DOCK_POSITION_LEFT;
            break;
          default:
            break;
        }
    }

  return position;
}

static const char *
get_dock_position_string (GsdUbuntuDockPosition  position)
{
  switch (position)
    {
      case GSD_UBUNTU_DOCK_POSITION_TOP:
        return "TOP";
      case GSD_UBUNTU_DOCK_POSITION_RIGHT:
        return "RIGHT";
      case GSD_UBUNTU_DOCK_POSITION_BOTTOM:
        return "BOTTOM";
      case GSD_UBUNTU_DOCK_POSITION_LEFT:
        return "LEFT";
      default:
        g_return_val_if_reached ("LEFT");
    }
}

static GsdUbuntuDockPosition
get_dock_position_from_string (const char *position)
{
  if (g_str_equal (position, "TOP"))
    return GSD_UBUNTU_DOCK_POSITION_TOP;

  if (g_str_equal (position, "RIGHT"))
    return GSD_UBUNTU_DOCK_POSITION_RIGHT;

  if (g_str_equal (position, "BOTTOM"))
    return GSD_UBUNTU_DOCK_POSITION_BOTTOM;

  if (g_str_equal (position, "LEFT"))
    return GSD_UBUNTU_DOCK_POSITION_LEFT;

  g_return_val_if_reached (GSD_UBUNTU_DOCK_POSITION_LEFT);
}

static GsdUbuntuDockPosition
get_dock_position_row_position (CcUbuntuPanel *self,
                                int            index)
{
  GListModel *model = adw_combo_row_get_model (self->dock_position_row);
  g_autoptr(GObject) value_object = g_list_model_get_item (model, index);

  return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (value_object), "position"));
}

static int
get_dock_position_row_index (CcUbuntuPanel         *self,
                             GsdUbuntuDockPosition  position)
{
  GListModel *model = adw_combo_row_get_model (self->dock_position_row);
  guint n_items;
  guint i;

  n_items = g_list_model_get_n_items (model);

  if (position == GSD_UBUNTU_DOCK_POSITION_TOP)
    return n_items;

  for (i = 0; i < n_items; i++)
    {
      g_autoptr(GObject) value_object = g_list_model_get_item (model, i);
      GsdUbuntuDockPosition item_position;

      item_position = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (value_object), "position"));

      if (item_position == position)
        return i;
    }

  g_return_val_if_reached (n_items);
}

static gboolean
dock_position_get_mapping (GValue   *value,
                           GVariant *variant,
                           gpointer  user_data)
{
  CcUbuntuPanel *self = user_data;
  GsdUbuntuDockPosition position;

  position = get_dock_position_from_string (g_variant_get_string (variant, NULL));
  position = get_dock_position_for_direction (self, position);

  if (G_VALUE_TYPE (value) == G_TYPE_UINT)
    {
      g_value_set_uint (value, get_dock_position_row_index (self, position));
      return TRUE;
    }
  else if (G_VALUE_TYPE (value) == G_TYPE_STRING)
    {
      g_value_set_string (value, get_dock_position_string (position));
      return TRUE;
    }

  return FALSE;
}

static GVariant *
dock_position_set_mapping (const GValue       *value,
                           const GVariantType *type,
                           gpointer            user_data)
{
  CcUbuntuPanel *self = user_data;
  GsdUbuntuDockPosition position;

  position = get_dock_position_row_position (self, g_value_get_uint (value));
  position = get_dock_position_for_direction (self, position);

  return g_variant_new_string (get_dock_position_string (position));
}

static void
populate_dock_position_row (AdwComboRow *combo_row)
{
  g_autoptr (GtkStringList) string_list = NULL;
  struct {
    char *name;
    GsdUbuntuDockPosition position;
  } positions[] = {
    {
      NC_("Position on screen for the Ubuntu dock", "Left"),
          GSD_UBUNTU_DOCK_POSITION_LEFT,
    },
    {
      NC_("Position on screen for the Ubuntu dock", "Bottom"),
          GSD_UBUNTU_DOCK_POSITION_BOTTOM,
    },
    {
      NC_("Position on screen for the Ubuntu dock", "Right"),
          GSD_UBUNTU_DOCK_POSITION_RIGHT,
    },
  };
  guint i;

  string_list = gtk_string_list_new (NULL);
  for (i = 0; i < G_N_ELEMENTS (positions); i++)
    {
      g_autoptr (GObject) value_object = NULL;

      gtk_string_list_append (string_list, g_dpgettext2 (NULL, "Position on screen for the Ubuntu dock", positions[i].name));
      value_object = g_list_model_get_item (G_LIST_MODEL (string_list), i);
      g_object_set_data (value_object, "position",
                         GUINT_TO_POINTER (positions[i].position));
    }

  adw_combo_row_set_model (combo_row, G_LIST_MODEL (string_list));
}

typedef enum
{
  UBUNTU_DING_ICONS_SIZE_SMALL,
  UBUNTU_DING_ICONS_SIZE_STANDARD,
  UBUNTU_DING_ICONS_SIZE_LARGE,
  UBUNTU_DING_ICONS_SIZE_TINY,
} UbuntuDingIconsSize;

static const char *
get_ding_size_string (UbuntuDingIconsSize size)
{
  switch (size)
    {
      case UBUNTU_DING_ICONS_SIZE_TINY:
        return "tiny";
      case UBUNTU_DING_ICONS_SIZE_SMALL:
        return "small";
      case UBUNTU_DING_ICONS_SIZE_STANDARD:
        return "standard";
      case UBUNTU_DING_ICONS_SIZE_LARGE:
        return "large";
      default:
        g_return_val_if_reached ("standard");
    }
}

static UbuntuDingIconsSize
get_ding_size_from_string (const char *size)
{
  if (g_str_equal (size, "tiny"))
    return UBUNTU_DING_ICONS_SIZE_TINY;

  if (g_str_equal (size, "small"))
    return UBUNTU_DING_ICONS_SIZE_SMALL;

  if (g_str_equal (size, "standard"))
    return UBUNTU_DING_ICONS_SIZE_STANDARD;

  if (g_str_equal (size, "large"))
    return UBUNTU_DING_ICONS_SIZE_LARGE;

  g_return_val_if_reached (UBUNTU_DING_ICONS_SIZE_STANDARD);
}

static int
get_ding_size_row_index (CcUbuntuPanel       *self,
                         UbuntuDingIconsSize  size)
{
  GListModel *model = adw_combo_row_get_model (self->ding_icon_size_row);
  guint n_items;
  guint i;

  n_items = g_list_model_get_n_items (model);

  for (i = 0; i < n_items; i++)
    {
      g_autoptr(GObject) value_object = g_list_model_get_item (model, i);
      UbuntuDingIconsSize item_size;

      item_size = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (value_object), "size"));

      if (item_size == size)
        return i;
    }

  g_return_val_if_reached (0);
}

static gboolean
ding_size_get_mapping (GValue   *value,
                       GVariant *variant,
                       gpointer  user_data)
{
  CcUbuntuPanel *self = user_data;
  UbuntuDingIconsSize size = 0;

  size = get_ding_size_from_string (g_variant_get_string (variant, NULL));

  if (G_VALUE_TYPE (value) == G_TYPE_UINT)
    {
      g_value_set_uint (value, get_ding_size_row_index (self, size));
      return TRUE;
    }
  else if (G_VALUE_TYPE (value) == G_TYPE_STRING)
    {
      g_value_set_string (value, get_ding_size_string (size));
      return TRUE;
    }

  return FALSE;
}

static GVariant *
ding_size_set_mapping (const GValue           *value,
                           const GVariantType *type,
                           gpointer            user_data)
{
  UbuntuDingIconsSize size = g_value_get_uint (value);

  return g_variant_new_string (get_ding_size_string (size));
}

static void
populate_ding_size_row (AdwComboRow *combo_row)
{
  g_autoptr (GtkStringList) string_list = NULL;
  struct {
    char *name;
    UbuntuDingIconsSize size;
  } sizes[] = {
    {
      NC_("Size of the desktop icons", "Small"),
          UBUNTU_DING_ICONS_SIZE_SMALL,
    },
    {
      NC_("Size of the desktop icons", "Normal"),
          UBUNTU_DING_ICONS_SIZE_STANDARD,
    },
    {
      NC_("Size of the desktop icons", "Large"),
          UBUNTU_DING_ICONS_SIZE_LARGE,
    },
    {
      NC_("Size of the desktop icons", "Tiny"),
          UBUNTU_DING_ICONS_SIZE_TINY,
    },
  };
  guint i;

  string_list = gtk_string_list_new (NULL);
  for (i = 0; i < G_N_ELEMENTS (sizes); i++)
    {
      g_autoptr (GObject) value_object = NULL;

      gtk_string_list_append (string_list, g_dpgettext2 (NULL, "Size of the desktop icons", sizes[i].name));
      value_object = g_list_model_get_item (G_LIST_MODEL (string_list), i);
      g_object_set_data (value_object, "size",
                         GUINT_TO_POINTER (sizes[i].size));
    }

  adw_combo_row_set_model (combo_row, G_LIST_MODEL (string_list));
}

typedef enum
{
  UBUNTU_DING_START_CORNER_TOP_LEFT,
  UBUNTU_DING_START_CORNER_TOP_RIGHT,
  UBUNTU_DING_START_CORNER_BOTTOM_LEFT,
  UBUNTU_DING_START_CORNER_BOTTOM_RIGHT,
} UbuntuDingStartCorner;

static const char *
get_ding_start_corner_string (UbuntuDingStartCorner corner)
{
  switch (corner)
    {
      case UBUNTU_DING_START_CORNER_TOP_LEFT:
        return "top-left";
      case UBUNTU_DING_START_CORNER_TOP_RIGHT:
        return "top-right";
      case UBUNTU_DING_START_CORNER_BOTTOM_LEFT:
        return "bottom-left";
      case UBUNTU_DING_START_CORNER_BOTTOM_RIGHT:
        return "bottom-right";
      default:
        g_return_val_if_reached ("standard");
    }
}

static UbuntuDingStartCorner
get_ding_start_corner_from_string (const char *corner)
{
  if (g_str_equal (corner, "top-left"))
    return UBUNTU_DING_START_CORNER_TOP_LEFT;

  if (g_str_equal (corner, "top-right"))
    return UBUNTU_DING_START_CORNER_TOP_RIGHT;

  if (g_str_equal (corner, "bottom-left"))
    return UBUNTU_DING_START_CORNER_BOTTOM_LEFT;

  if (g_str_equal (corner, "bottom-right"))
    return UBUNTU_DING_START_CORNER_BOTTOM_RIGHT;

  g_warning("Value is %s", corner);

  g_return_val_if_reached (UBUNTU_DING_START_CORNER_BOTTOM_RIGHT);
}

static int
ding_start_corner_get_row_index (CcUbuntuPanel         *self,
                                 UbuntuDingStartCorner  corner)
{
  GListModel *model = adw_combo_row_get_model (self->ding_icons_new_alignment_row);
  guint n_items;
  guint i;

  n_items = g_list_model_get_n_items (model);

  for (i = 0; i < n_items; i++)
    {
      g_autoptr(GObject) value_object = g_list_model_get_item (model, i);
      UbuntuDingStartCorner item_corner;

      item_corner = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (value_object), "corner"));

      if (item_corner == corner)
        return i;
    }

  g_return_val_if_reached (0);
}

static gboolean
ding_start_corner_get_mapping (GValue   *value,
                               GVariant *variant,
                               gpointer  user_data)
{
  CcUbuntuPanel *self = user_data;
  UbuntuDingStartCorner corner = 0;

  corner = get_ding_start_corner_from_string (g_variant_get_string (variant, NULL));

  if (G_VALUE_TYPE (value) == G_TYPE_UINT)
    {
      g_value_set_uint (value, ding_start_corner_get_row_index (self, corner));
      return TRUE;
    }
  else if (G_VALUE_TYPE (value) == G_TYPE_STRING)
    {
      g_value_set_string (value, get_ding_start_corner_string (corner));
      return TRUE;
    }

  return FALSE;
}

static GVariant *
ding_start_corner_set_mapping (const GValue       *value,
                               const GVariantType *type,
                               gpointer            user_data)
{
  UbuntuDingStartCorner corner = g_value_get_uint (value);

  return g_variant_new_string (get_ding_start_corner_string (corner));
}

static void
populate_ding_start_corner_row (AdwComboRow *combo_row)
{
  g_autoptr (GtkStringList) string_list = NULL;
  struct {
    char *name;
    UbuntuDingStartCorner corner;
  } corners[] = {
    {
      NC_("Alignment of new desktop icons", "Top Left"),
          UBUNTU_DING_START_CORNER_TOP_LEFT,
    },
    {
      NC_("Alignment of new desktop icons", "Top Right"),
          UBUNTU_DING_START_CORNER_TOP_RIGHT,
    },
    {
      NC_("Alignment of new desktop icons", "Bottom Left"),
          UBUNTU_DING_START_CORNER_BOTTOM_LEFT,
    },
    {
      NC_("Alignment of new desktop icons", "Bottom Right"),
          UBUNTU_DING_START_CORNER_BOTTOM_RIGHT,
    },
  };
  guint i;

  string_list = gtk_string_list_new (NULL);
  for (i = 0; i < G_N_ELEMENTS (corners); i++)
    {
      g_autoptr(GObject) value_object = NULL;

      gtk_string_list_append (string_list, g_dpgettext2 (NULL, "Alignment of new desktop icons", corners[i].name));
      value_object = g_list_model_get_item (G_LIST_MODEL (string_list), i);
      g_object_set_data (value_object, "corner",
                         GUINT_TO_POINTER (corners[i].corner));
    }

  adw_combo_row_set_model (combo_row, G_LIST_MODEL (string_list));
}

static void
cc_ubuntu_panel_init (CcUbuntuPanel *self)
{
  GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default ();
  g_autoptr(GSettingsSchema) schema = NULL;

  g_resources_register (cc_ubuntu_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (self));

  schema = g_settings_schema_source_lookup (schema_source, GNOME_SHELL_SCHEMA, TRUE);
  if (schema)
    {
      self->gnome_shell_settings = g_settings_new_full (schema, NULL, NULL);
    }
  else
    {
      g_warning ("No GNOME Shell is installed here. Please fix your installation.");
    }

  /* Only load if we ding installed */
  schema = g_settings_schema_source_lookup (schema_source, DING_SCHEMA, TRUE);
  if (schema)
    {
      self->ding_settings = g_settings_new_full (schema, NULL, NULL);

      populate_ding_size_row (self->ding_icon_size_row);
      gtk_widget_set_sensitive (GTK_WIDGET (self->ding_icon_size_row),
                                gtk_switch_get_active (self->ding_switch));

      g_settings_bind_with_mapping (self->ding_settings, "icon-size",
                                    self->ding_icon_size_row, "selected",
                                    G_SETTINGS_BIND_NO_SENSITIVITY,
                                    ding_size_get_mapping,
                                    ding_size_set_mapping,
                                    self, NULL);

      populate_ding_start_corner_row (self->ding_icons_new_alignment_row);
      g_settings_bind_with_mapping (self->ding_settings, "start-corner",
                                    self->ding_icons_new_alignment_row, "selected",
                                    G_SETTINGS_BIND_NO_SENSITIVITY,
                                    ding_start_corner_get_mapping,
                                    ding_start_corner_set_mapping,
                                    self, NULL);

      g_settings_bind (self->ding_settings, "show-home",
                       self->ding_showhome_switch, "active",
                       G_SETTINGS_BIND_NO_SENSITIVITY);

      g_signal_connect_object (self->gnome_shell_settings,
                               "changed::" GNOME_SHELL_ENABLED_EXTENSIONS,
                               G_CALLBACK (update_ding_status),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (self->gnome_shell_settings,
                               "changed::" GNOME_SHELL_DISABLED_EXTENSIONS,
                               G_CALLBACK (update_ding_status),
                               self,
                               G_CONNECT_SWAPPED);

      update_ding_status (self);

      g_clear_pointer (&schema, g_settings_schema_unref);
    }
  else
    {
      g_warning ("No Ding is installed here. Please fix your installation.");
      gtk_widget_set_visible (GTK_WIDGET (self->ding_preferences_group), FALSE);
    }

  /* Only load if we have ubuntu dock or dash to dock installed */
  schema = g_settings_schema_source_lookup (schema_source, UBUNTU_DOCK_SCHEMA, TRUE);
  if (schema)
    {
      GtkExpression *expression;

      self->dock_settings = g_settings_new_full (schema, NULL, NULL);
      self->dock_monitors_list = g_list_store_new (G_TYPE_OBJECT);

      expression = gtk_cclosure_expression_new (G_TYPE_STRING,
                                                NULL, 0, NULL,
                                                G_CALLBACK (get_dock_monitor_value_object_name),
                                                self, NULL);
      adw_combo_row_set_expression (self->dock_monitor_row, expression);

      self->updating = TRUE;
      adw_combo_row_set_model (self->dock_monitor_row,
                               G_LIST_MODEL (self->dock_monitors_list));
      gtk_widget_set_sensitive (GTK_WIDGET (self->dock_monitor_row),
                                gtk_switch_get_active (self->dock_switch));
      self->updating = FALSE;

      populate_dock_position_row (self->dock_position_row);
      gtk_widget_set_sensitive (GTK_WIDGET (self->dock_position_row),
                                gtk_switch_get_active (self->dock_switch));

      g_signal_connect_object (self->dock_settings,
                               "changed::" ICONSIZE_KEY,
                               G_CALLBACK (icon_size_widget_refresh),
                               self, G_CONNECT_SWAPPED);
      g_signal_connect_object (self->dock_settings,
                               "changed::" UBUNTU_DOCK_ALL_MONITORS_KEY,
                               G_CALLBACK (update_dock_monitor_combo_row_selection),
                               self, G_CONNECT_SWAPPED);
      g_signal_connect_object (self->dock_settings,
                               "changed::" UBUNTU_DOCK_PREFERRED_MONITOR_KEY,
                               G_CALLBACK (update_dock_monitor_combo_row_selection),
                               self, G_CONNECT_SWAPPED);
      g_signal_connect_object (self->dock_settings,
                               "changed::" UBUNTU_DOCK_PREFERRED_CONNECTOR_KEY,
                               G_CALLBACK (update_dock_monitor_combo_row_selection),
                               self, G_CONNECT_SWAPPED);
      g_settings_bind_with_mapping (self->dock_settings, "dock-position",
                                    self->dock_position_row, "selected",
                                    G_SETTINGS_BIND_NO_SENSITIVITY,
                                    dock_position_get_mapping,
                                    dock_position_set_mapping,
                                    self, NULL);
      g_settings_bind (self->dock_settings, "dock-fixed",
                       self->dock_autohide_switch, "active",
                       G_SETTINGS_BIND_INVERT_BOOLEAN |
                       G_SETTINGS_BIND_NO_SENSITIVITY);
      g_settings_bind (self->dock_settings, "extend-height",
                       self->dock_extendheight_switch, "active",
                       G_SETTINGS_BIND_NO_SENSITIVITY);

      g_signal_connect_object (self->gnome_shell_settings,
                               "changed::" GNOME_SHELL_ENABLED_EXTENSIONS,
                               G_CALLBACK (update_dock_status),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (self->gnome_shell_settings,
                               "changed::" GNOME_SHELL_DISABLED_EXTENSIONS,
                               G_CALLBACK (update_dock_status),
                               self,
                               G_CONNECT_SWAPPED);

      update_dock_status (self);

      /* Icon size change - we halve the sizes so we can only get even values */
      gtk_adjustment_set_value (self->icon_size_adjustment, DEFAULT_ICONSIZE / 2);
      gtk_adjustment_set_lower (self->icon_size_adjustment, MIN_ICONSIZE / 2);
      gtk_adjustment_set_upper (self->icon_size_adjustment, MAX_ICONSIZE / 2);
      gtk_scale_add_mark (self->icon_size_scale, DEFAULT_ICONSIZE / 2, GTK_POS_BOTTOM, NULL);
      gtk_scale_set_format_value_func (self->icon_size_scale,
                                       on_icon_size_format_value,
                                       NULL, NULL);

      icon_size_widget_refresh (self);

      g_bus_get (G_BUS_TYPE_SESSION,
                 cc_panel_get_cancellable (CC_PANEL (self)),
                 session_bus_ready, self);

      cc_object_storage_create_dbus_proxy (G_BUS_TYPE_SESSION,
                                           G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                           G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
                                           G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                           "org.gnome.Shell",
                                           "/org/gnome/Shell",
                                           "org.gnome.Shell",
                                           cc_panel_get_cancellable (CC_PANEL (self)),
                                           (GAsyncReadyCallback) shell_proxy_ready,
                                           self);

      g_clear_pointer (&schema, g_settings_schema_unref);
    }
  else
    {
      g_warning ("No Ubuntu Dock is installed here. Panel disabled. Please fix your installation.");
      gtk_widget_set_visible (GTK_WIDGET (self->dock_preferences_group), FALSE);
    }

  schema = g_settings_schema_source_lookup (schema_source, TILING_ASSISTANT_SCHEMA, TRUE);
  if (schema && self->gnome_shell_settings)
    {
      self->tiling_assistant_settings = g_settings_new_full (schema, NULL, NULL);

      g_signal_connect_object (self->gnome_shell_settings,
                               "changed::" GNOME_SHELL_ENABLED_EXTENSIONS,
                               G_CALLBACK (update_tiling_assistant_status),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (self->gnome_shell_settings,
                               "changed::" GNOME_SHELL_DISABLED_EXTENSIONS,
                               G_CALLBACK (update_tiling_assistant_status),
                               self,
                               G_CONNECT_SWAPPED);

      g_settings_bind (self->tiling_assistant_settings, "enable-tiling-popup",
                       self->tiling_assistant_popup_switch, "active",
                       G_SETTINGS_BIND_NO_SENSITIVITY);
      g_settings_bind (self->tiling_assistant_settings, "disable-tile-groups",
                       self->tiling_assistant_tiling_group, "active",
                       G_SETTINGS_BIND_INVERT_BOOLEAN |
                       G_SETTINGS_BIND_NO_SENSITIVITY);

      update_tiling_assistant_status (self);

      g_clear_pointer (&schema, g_settings_schema_unref);
    }
  else
    {
      g_debug ("No Tiling Assistant is installed here. Panel disabled.");
      gtk_widget_set_visible (GTK_WIDGET (self->tiling_assistant_preferences_group), FALSE);
    }


  if (!update_panel_visibilty (self))
    return;

  cc_object_storage_create_dbus_proxy (G_BUS_TYPE_SESSION,
                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                       "org.gnome.Shell.Extensions",
                                       "/org/gnome/Shell/Extensions",
                                       "org.gnome.Shell.Extensions",
                                       cc_panel_get_cancellable (CC_PANEL (self)),
                                       (GAsyncReadyCallback) shell_extensions_proxy_ready,
                                       self);

  g_signal_connect (self, "map", G_CALLBACK (mapped_cb), NULL);
}

void
cc_ubuntu_panel_static_init_func (void)
{
  CcApplication *application;
  const gchar *desktop_list;
  g_auto(GStrv) desktops = NULL;

  desktop_list = g_getenv ("XDG_CURRENT_DESKTOP");
  if (desktop_list != NULL)
    desktops = g_strsplit (desktop_list, ":", -1);

  if (desktops == NULL || !g_strv_contains ((const gchar * const *) desktops, "ubuntu")) {
    application = CC_APPLICATION (g_application_get_default ());
    cc_shell_model_set_panel_visibility (cc_application_get_model (application),
                                         "ubuntu",
                                         CC_PANEL_HIDDEN);
  }
}
