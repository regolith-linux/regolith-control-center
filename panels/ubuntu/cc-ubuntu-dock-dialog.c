/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2022 Marco Trevisan <marco.trevisan@canonical.com>
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

#include <string.h>
#include <glib/gi18n-lib.h>
#include <glib.h>

#include "cc-list-row.h"
#include "cc-ubuntu-dock-dialog.h"

struct _CcUbuntuDockDialog {
  GtkDialog       parent;

  GSettings      *settings;

  AdwSwitchRow   *volumes_switch;
  GtkCheckButton *unmounted_check;
  GtkCheckButton *network_volumes_check;
  AdwSwitchRow   *trash_switch;
};

G_DEFINE_TYPE (CcUbuntuDockDialog, cc_ubuntu_dock_dialog, GTK_TYPE_DIALOG)

static void
cc_ubuntu_dock_dialog_dispose (GObject *object)
{
  CcUbuntuDockDialog *dialog = CC_UBUNTU_DOCK_DIALOG (object);

  g_clear_object (&dialog->settings);

  G_OBJECT_CLASS (cc_ubuntu_dock_dialog_parent_class)->dispose (object);
}

static void
cc_ubuntu_dock_dialog_class_init (CcUbuntuDockDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = cc_ubuntu_dock_dialog_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/ubuntu/cc-ubuntu-dock-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDockDialog, volumes_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDockDialog, unmounted_check);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDockDialog, network_volumes_check);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDockDialog, trash_switch);
}

void
cc_ubuntu_dock_dialog_init (CcUbuntuDockDialog *dialog)
{
  gtk_widget_init_template (GTK_WIDGET (dialog));
}

CcUbuntuDockDialog *
cc_ubuntu_dock_dialog_new (GSettings *settings)
{
  CcUbuntuDockDialog *dialog;

  dialog = g_object_new (CC_TYPE_UBUNTU_DOCK_DIALOG,
                         "use-header-bar", TRUE,
                         NULL);

  gtk_window_set_title (GTK_WINDOW (dialog), _("Dock"));
  dialog->settings = g_object_ref (settings);

  g_settings_bind (dialog->settings, "show-mounts",
                   dialog->volumes_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (dialog->settings, "show-mounts-only-mounted",
                   dialog->unmounted_check, "active",
                   G_SETTINGS_BIND_INVERT_BOOLEAN);

  g_settings_bind (dialog->settings, "show-mounts-network",
                   dialog->network_volumes_check, "active",
                   G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (dialog->settings, "show-trash",
                   dialog->trash_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);

  return dialog;
}
