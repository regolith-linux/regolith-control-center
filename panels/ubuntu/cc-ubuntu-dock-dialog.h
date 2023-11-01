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

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CC_TYPE_UBUNTU_DOCK_DIALOG (cc_ubuntu_dock_dialog_get_type ())
G_DECLARE_FINAL_TYPE (CcUbuntuDockDialog, cc_ubuntu_dock_dialog, CC, UBUNTU_DOCK_DIALOG, GtkDialog)

CcUbuntuDockDialog *cc_ubuntu_dock_dialog_new (GSettings *settings);

G_END_DECLS
