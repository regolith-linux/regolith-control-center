/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2018 Red Hat, Inc
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Matthias Clasen <mclasen@redhat.com>
 */

#include <config.h>

#include "cc-diagnostics-page.h"
#include "cc-util.h"
#include "shell/cc-application.h"

#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

#ifdef HAVE_WHOOPSIE
#include <whoopsie-preferences/libwhoopsie-preferences.h>
#endif

struct _CcDiagnosticsPage
{
  AdwNavigationPage    parent_instance;

  AdwPreferencesGroup *diagnostics_group;
  GtkSwitch           *abrt_switch;

  GSettings           *privacy_settings;

#ifdef HAVE_WHOOPSIE
  AdwActionRow        *abrt_row;
  AdwComboRow         *whoopsie_combo_row;
  WhoopsiePreferences *whoopsie;
  GCancellable        *cancellable;
#endif
};

G_DEFINE_TYPE (CcDiagnosticsPage, cc_diagnostics_page, ADW_TYPE_NAVIGATION_PAGE)

/* Static init function */

static void
abrt_appeared_cb (GDBusConnection *connection,
                  const gchar     *name,
                  const gchar     *name_owner,
                  gpointer         user_data)
{
  CcDiagnosticsPage *self = CC_DIAGNOSTICS_PAGE (user_data);

  g_debug ("ABRT appeared");
  gtk_widget_set_visible (GTK_WIDGET (self), TRUE);
}

static void
abrt_vanished_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  CcDiagnosticsPage *self = CC_DIAGNOSTICS_PAGE (user_data);

  g_debug ("ABRT vanished");
#ifndef HAVE_WHOOPSIE
  gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
#endif
}

#ifdef HAVE_WHOOPSIE
typedef enum
{
  WHOOPSIE_BUTTON_SETTING_NEVER,
  WHOOPSIE_BUTTON_SETTING_AUTO,
  WHOOPSIE_BUTTON_SETTING_MANUAL,
} WhoopsieButtonSettingType;

static void
whoopsie_set_report_crashes_done (GObject *source_object,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
  CcDiagnosticsPage *self = CC_DIAGNOSTICS_PAGE (user_data);
  g_autoptr(GError) error = NULL;

  if (!whoopsie_preferences_call_set_report_crashes_finish (self->whoopsie, res, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to toggle crash reporting: %s", error->message);
    }
}

static void
whoopsie_set_report_crashes_mode_done (GObject *source_object,
                                       GAsyncResult *res,
                                       gpointer user_data)
{
  CcDiagnosticsPage *self = CC_DIAGNOSTICS_PAGE (user_data);
  g_autoptr(GError) error = NULL;

  if (!whoopsie_preferences_call_set_automatically_report_crashes_finish (self->whoopsie, res, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to toggle crash reporting mode: %s", error->message);
    }
}

static void
whoopsie_combo_row_changed_cb (CcDiagnosticsPage *self)
{
  g_autoptr (GObject) item = NULL;
  GListModel *model;
  gint selected_index;
  gint value;

  model = adw_combo_row_get_model (self->whoopsie_combo_row);
  selected_index = adw_combo_row_get_selected (self->whoopsie_combo_row);
  if (selected_index == -1)
    return;

  item = g_list_model_get_item (model, selected_index);
  value = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (item), "value"));

  whoopsie_preferences_call_set_report_crashes (self->whoopsie,
                                                value != WHOOPSIE_BUTTON_SETTING_NEVER,
                                                self->cancellable,
                                                whoopsie_set_report_crashes_done,
                                                self);

  whoopsie_preferences_call_set_automatically_report_crashes (self->whoopsie,
                                                              value == WHOOPSIE_BUTTON_SETTING_AUTO,
                                                              self->cancellable,
                                                              whoopsie_set_report_crashes_mode_done,
                                                              self);
}

static void
set_value_for_combo_row (AdwComboRow *combo_row, gint value)
{
  g_autoptr (GObject) new_item = NULL;
  GListModel *model;
  guint i;

  /* try to make the UI match the setting */
  model = adw_combo_row_get_model (combo_row);
  for (i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr (GObject) item = g_list_model_get_item (model, i);
      gint value_tmp = GPOINTER_TO_UINT (g_object_get_data (item, "value"));
      if (value_tmp == value)
        {
          adw_combo_row_set_selected (combo_row, i);
          return;
        }
    }
}

static void
whoopsie_properties_changed (CcDiagnosticsPage *self)
{
  WhoopsieButtonSettingType value = WHOOPSIE_BUTTON_SETTING_NEVER;

  if (whoopsie_preferences_get_automatically_report_crashes (self->whoopsie))
    value = WHOOPSIE_BUTTON_SETTING_AUTO;
  else if (whoopsie_preferences_get_report_crashes (self->whoopsie))
    value = WHOOPSIE_BUTTON_SETTING_MANUAL;

  g_signal_handlers_block_by_func (self->whoopsie_combo_row, whoopsie_combo_row_changed_cb, self);
  set_value_for_combo_row (self->whoopsie_combo_row, value);
  g_signal_handlers_unblock_by_func (self->whoopsie_combo_row, whoopsie_combo_row_changed_cb, self);
}

static void
populate_whoopsie_button_row (AdwComboRow *combo_row)
{
  g_autoptr (GtkStringList) string_list = NULL;
  struct {
    char *name;
    WhoopsieButtonSettingType value;
  } actions[] = {
    { NC_("Whoopsie error reporting", "Never"), WHOOPSIE_BUTTON_SETTING_NEVER },
    { NC_("Whoopsie error reporting", "Automatic"), WHOOPSIE_BUTTON_SETTING_AUTO },
    { NC_("Whoopsie error reporting", "Manual"), WHOOPSIE_BUTTON_SETTING_MANUAL },
  };
  guint item_index = 0;
  guint i;

  string_list = gtk_string_list_new (NULL);
  for (i = 0; i < G_N_ELEMENTS (actions); i++)
    {
      g_autoptr (GObject) item = NULL;

      gtk_string_list_append (string_list, _(actions[i].name));

      item = g_list_model_get_item (G_LIST_MODEL (string_list), item_index++);
      g_object_set_data (item, "value", GUINT_TO_POINTER (actions[i].value));
    }

  adw_combo_row_set_model (combo_row, G_LIST_MODEL (string_list));
}

static void
on_new_whoopsie_proxy_cb (GObject *object,
                          GAsyncResult *res,
                          gpointer data)
{
  CcDiagnosticsPage *self = CC_DIAGNOSTICS_PAGE (data);
  g_autoptr(GError) error = NULL;

  self->whoopsie = whoopsie_preferences_proxy_new_for_bus_finish (res, &error);

  if (error)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to get whoopsie proxy: %s", error->message);

      gtk_widget_set_visible (GTK_WIDGET (self->whoopsie_combo_row), FALSE);
      gtk_widget_set_visible (GTK_WIDGET (self->abrt_row), TRUE);
      return;
    }

  g_debug ("Whoopsie available");
  gtk_widget_set_visible (GTK_WIDGET (self), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->whoopsie_combo_row), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->whoopsie_combo_row), TRUE);


  g_signal_handlers_block_by_func (self->whoopsie_combo_row,
                                   whoopsie_combo_row_changed_cb, self);
  populate_whoopsie_button_row (self->whoopsie_combo_row);
  g_signal_handlers_unblock_by_func (self->whoopsie_combo_row,
                                     whoopsie_combo_row_changed_cb, self);

  g_signal_connect_object (self->whoopsie, "g-properties-changed",
                           G_CALLBACK (whoopsie_properties_changed),
                           self, G_CONNECT_SWAPPED);

  whoopsie_properties_changed (self);
}
#else
static void
whoopsie_combo_row_changed_cb (CcDiagnosticsPage *self)
{}
#endif

static void
cc_diagnostics_page_finalize (GObject *object)
{
  CcDiagnosticsPage *self = CC_DIAGNOSTICS_PAGE (object);

  g_clear_object (&self->privacy_settings);

  G_OBJECT_CLASS (cc_diagnostics_page_parent_class)->finalize (object);

#if HAVE_WHOOPISE
  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->whoopsie);
#endif
}

static void
cc_diagnostics_page_class_init (CcDiagnosticsPageClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  oclass->finalize = cc_diagnostics_page_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/privacy/cc-diagnostics-page.ui");

  gtk_widget_class_bind_template_child (widget_class, CcDiagnosticsPage, diagnostics_group);
  gtk_widget_class_bind_template_child (widget_class, CcDiagnosticsPage, abrt_switch);

#ifdef HAVE_WHOOPSIE
  gtk_widget_class_bind_template_child (widget_class, CcDiagnosticsPage, abrt_row);
  gtk_widget_class_bind_template_child (widget_class, CcDiagnosticsPage, whoopsie_combo_row);
#endif
  gtk_widget_class_bind_template_callback (widget_class, whoopsie_combo_row_changed_cb);
}

static void
cc_diagnostics_page_init (CcDiagnosticsPage *self)
{
  g_autofree gchar *os_name = NULL;
  g_autofree gchar *url = NULL;
  g_autofree gchar *msg = NULL;
  g_autofree gchar *link = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                    "org.freedesktop.problems.daemon",
                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                    abrt_appeared_cb,
                    abrt_vanished_cb,
                    self,
                    NULL);

  gtk_widget_set_visible (GTK_WIDGET (self), FALSE);

  self->privacy_settings = g_settings_new ("org.gnome.desktop.privacy");

  g_settings_bind (self->privacy_settings, "report-technical-problems",
                   self->abrt_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);

#ifdef HAVE_WHOOPSIE
  /* check for whoopsie */
  self->cancellable = g_cancellable_new ();
  whoopsie_preferences_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          "com.ubuntu.WhoopsiePreferences",
                                          "/com/ubuntu/WhoopsiePreferences",
                                          self->cancellable,
                                          on_new_whoopsie_proxy_cb,
                                          self);

  gtk_widget_set_visible (GTK_WIDGET (self), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->whoopsie_combo_row), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->abrt_row), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->whoopsie_combo_row), FALSE);
#endif

  os_name = g_get_os_info (G_OS_INFO_KEY_NAME);
  if (!os_name)
    os_name = g_strdup ("GNOME");
  url = g_get_os_info (G_OS_INFO_KEY_PRIVACY_POLICY_URL);
  if (!url)
    url = g_strdup ("http://www.gnome.org/privacy-policy");
  /* translators: Text used in link to privacy policy */
  link = g_strdup_printf ("<a href=\"%s\">%s</a>", url, _("Learn more"));
  /* translators: The first '%s' is the distributor's name, such as 'Fedora', the second '%s' is a link to the privacy policy */
  msg = g_strdup_printf (_("Sending reports of technical problems helps us improve %s. Reports "
                           "are sent anonymously and are scrubbed of personal data. %s"),
                         os_name, link);
  adw_preferences_group_set_description (self->diagnostics_group, msg);
}
