/* cc-multitasking-panel.h
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "cc-multitasking-panel.h"

#include "cc-multitasking-resources.h"
#include "cc-illustrated-row.h"

struct _CcMultitaskingPanel
{
  CcPanel          parent_instance;

  GSettings       *interface_settings;
  GSettings       *mutter_settings;
  GSettings       *shell_settings;
  GSettings       *dock_settings;
  GSettings       *tiling_assistant_settings;
  GSettings       *wm_settings;

  CcIllustratedRow *active_screen_edges_row;
  GtkSwitch       *active_screen_edges_switch;
  GtkCheckButton  *all_workspaces_radio;
  GtkCheckButton  *current_workspace_radio;
  GtkCheckButton  *dynamic_workspaces_radio;
  GtkCheckButton  *fixed_workspaces_radio;
  GtkCheckButton  *dock_monitors_isolation_radio;
  GtkCheckButton  *dock_each_monitor_radio;
  CcIllustratedRow *hot_corner_row;
  GtkSwitch       *hot_corner_switch;
  GtkSpinButton   *number_of_workspaces_spin;
  GtkCheckButton  *workspaces_primary_display_radio;
  GtkCheckButton  *workspaces_span_displays_radio;

};

CC_PANEL_REGISTER (CcMultitaskingPanel, cc_multitasking_panel)

static void
keep_dock_settings_in_sync (CcMultitaskingPanel *self)
{
  gboolean switcher_isolate_workspaces;
  gboolean dock_isolate_workspaces;

  switcher_isolate_workspaces = g_settings_get_boolean (self->shell_settings,
    "current-workspace-only");
  dock_isolate_workspaces = g_settings_get_boolean (self->dock_settings,
    "isolate-workspaces");

  if (switcher_isolate_workspaces != dock_isolate_workspaces)
    {
      g_settings_set_boolean (self->dock_settings, "isolate-workspaces",
                              switcher_isolate_workspaces);
    }
}

static void
keep_tiling_assistant_settings_in_sync (CcMultitaskingPanel *self)
{
  gboolean switcher_isolate_workspaces;
  gboolean tiling_assistant_popup_isolate_workspaces;

  switcher_isolate_workspaces = g_settings_get_boolean (self->shell_settings,
    "current-workspace-only");
  tiling_assistant_popup_isolate_workspaces = !g_settings_get_boolean (
    self->tiling_assistant_settings, "tiling-popup-all-workspace");

  if (switcher_isolate_workspaces != tiling_assistant_popup_isolate_workspaces)
    {
      g_settings_set_boolean (self->tiling_assistant_settings,
                              "tiling-popup-all-workspace",
                              !switcher_isolate_workspaces);
    }
}

static void
load_custom_css (CcMultitaskingPanel *self)
{
  g_autoptr(GtkCssProvider) provider = NULL;

  /* use custom CSS */
  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (provider,
    ".multitasking-assets picture { background: @theme_selected_bg_color; }",
    -1);
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* GObject overrides */

static void
cc_multitasking_panel_finalize (GObject *object)
{
  CcMultitaskingPanel *self = (CcMultitaskingPanel *)object;

  g_clear_object (&self->interface_settings);
  g_clear_object (&self->mutter_settings);
  g_clear_object (&self->shell_settings);
  g_clear_object (&self->dock_settings);
  g_clear_object (&self->tiling_assistant_settings);
  g_clear_object (&self->wm_settings);

  G_OBJECT_CLASS (cc_multitasking_panel_parent_class)->finalize (object);
}

static void
cc_multitasking_panel_class_init (CcMultitaskingPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_multitasking_panel_finalize;

  g_type_ensure (CC_TYPE_ILLUSTRATED_ROW);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/multitasking/cc-multitasking-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, active_screen_edges_row);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, active_screen_edges_switch);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, all_workspaces_radio);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, current_workspace_radio);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, dynamic_workspaces_radio);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, fixed_workspaces_radio);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, hot_corner_row);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, hot_corner_switch);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, number_of_workspaces_spin);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, workspaces_primary_display_radio);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, workspaces_span_displays_radio);

  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, dock_monitors_isolation_radio);
  gtk_widget_class_bind_template_child (widget_class, CcMultitaskingPanel, dock_each_monitor_radio);
}

static void
cc_multitasking_panel_init (CcMultitaskingPanel *self)
{
  GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default ();
  g_autoptr(GSettingsSchema) schema = NULL;

  g_resources_register (cc_multitasking_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (self));

  self->interface_settings = g_settings_new ("org.gnome.desktop.interface");
  g_settings_bind (self->interface_settings,
                   "enable-hot-corners",
                   self->hot_corner_switch,
                   "active",
                   G_SETTINGS_BIND_DEFAULT);

  self->mutter_settings = g_settings_new ("org.gnome.mutter");

  if (g_settings_get_boolean (self->mutter_settings, "workspaces-only-on-primary"))
    gtk_check_button_set_active (self->workspaces_primary_display_radio, TRUE);
  else
    gtk_check_button_set_active (self->workspaces_span_displays_radio, TRUE);

  g_settings_bind (self->mutter_settings,
                   "workspaces-only-on-primary",
                   self->workspaces_primary_display_radio,
                   "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->mutter_settings,
                   "edge-tiling",
                   self->active_screen_edges_switch,
                   "active",
                   G_SETTINGS_BIND_DEFAULT);

  if (g_settings_get_boolean (self->mutter_settings, "dynamic-workspaces"))
    gtk_check_button_set_active (self->dynamic_workspaces_radio, TRUE);
  else
    gtk_check_button_set_active (self->fixed_workspaces_radio, TRUE);

  g_settings_bind (self->mutter_settings,
                   "dynamic-workspaces",
                   self->dynamic_workspaces_radio,
                   "active",
                   G_SETTINGS_BIND_DEFAULT);

  self->wm_settings = g_settings_new ("org.gnome.desktop.wm.preferences");
  g_settings_bind (self->wm_settings,
                   "num-workspaces",
                   self->number_of_workspaces_spin,
                   "value",
                   G_SETTINGS_BIND_DEFAULT);

  self->shell_settings = g_settings_new ("org.gnome.shell.app-switcher");

  if (g_settings_get_boolean (self->shell_settings, "current-workspace-only"))
    gtk_check_button_set_active (self->current_workspace_radio, TRUE);
  else
    gtk_check_button_set_active (self->all_workspaces_radio, TRUE);

  g_settings_bind (self->shell_settings,
                   "current-workspace-only",
                   self->current_workspace_radio,
                   "active",
                   G_SETTINGS_BIND_DEFAULT);

  if (gtk_widget_get_default_direction () == GTK_TEXT_DIR_RTL)
    {
      cc_illustrated_row_set_resource (self->hot_corner_row,
                                       "/org/gnome/control-center/multitasking/assets/hot-corner-rtl.svg");
      cc_illustrated_row_set_resource (self->active_screen_edges_row,
                                       "/org/gnome/control-center/multitasking/assets/active-screen-edges-rtl.svg");
    }

  schema = g_settings_schema_source_lookup (schema_source,
                                            "org.gnome.shell.extensions.dash-to-dock",
                                            TRUE);
  if (schema)
    {
      self->dock_settings = g_settings_new_full (schema, NULL, NULL);

      g_signal_connect_object (self->shell_settings, "changed::current-workspace-only",
                               G_CALLBACK (keep_dock_settings_in_sync), self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (self->dock_settings, "changed::isolate-workspaces",
                               G_CALLBACK (keep_dock_settings_in_sync), self,
                               G_CONNECT_SWAPPED);

      keep_dock_settings_in_sync (self);

      if (g_settings_get_boolean (self->dock_settings, "isolate-monitors"))
        gtk_check_button_set_active (self->dock_each_monitor_radio, TRUE);
      else
        gtk_check_button_set_active (self->dock_monitors_isolation_radio, TRUE);

      g_settings_bind (self->dock_settings,
                       "isolate-monitors",
                       self->dock_each_monitor_radio,
                       "active",
                       G_SETTINGS_BIND_DEFAULT);

      g_clear_pointer (&schema, g_settings_schema_unref);
    }

  schema = g_settings_schema_source_lookup (schema_source,
                                            "org.gnome.shell.extensions.tiling-assistant",
                                            TRUE);
  if (schema)
    {
      self->tiling_assistant_settings = g_settings_new_full (schema, NULL, NULL);

      g_signal_connect_object (self->shell_settings, "changed::current-workspace-only",
                               G_CALLBACK (keep_tiling_assistant_settings_in_sync), self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (self->tiling_assistant_settings, "changed::tiling-popup-all-workspace",
                               G_CALLBACK (keep_tiling_assistant_settings_in_sync), self,
                               G_CONNECT_SWAPPED);

      keep_tiling_assistant_settings_in_sync (self);

      g_clear_pointer (&schema, g_settings_schema_unref);
    }

  load_custom_css (self);
}
