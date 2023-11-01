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
 */

#pragma once

#include <glib.h>
#include <adwaita.h>
#include <gdesktop-enums.h>

G_BEGIN_DECLS

#define CC_TYPE_UBUNTU_COLORS_ROW (cc_ubuntu_colors_row_get_type ())
G_DECLARE_FINAL_TYPE (CcUbuntuColorsRow, cc_ubuntu_colors_row, CC, UBUNTU_COLORS_ROW, AdwActionRow)

void cc_ubuntu_colors_row_set_color_scheme (CcUbuntuColorsRow *colors_row,
                                            GDesktopColorScheme color_scheme);

G_END_DECLS
