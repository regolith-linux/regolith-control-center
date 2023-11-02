[![License](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://gitlab.gnome.org/GNOME/gnome-control-center/blob/main/COPYING)

Regolith Settings
====================

Regolith Settings is a partial port of GNOME Settings: GNOME's main interface for configuration of various aspects of your desktop.

The differences from upstream are:

1. Less panels are supported.  Anything relating to `gnome-shell` or `mutter` features are irrelevant to Regolith's i3/Sway window manager.
2. The name changes from `org.gnome.Settings` to `org.regolith.Settings`.
3. Other Ubuntu-specific features are removed or disabled via build flags, such as `whoopsie` and `snap`.
4. Regolith logo is present over Ubuntu logo in info panel.
5. Regolith-specific handling of Wallpaper settings.

By design, Regolith Settings depends on upstream GNOME Settings for graphical assets and other resources.