To port a GNOME settings version to Regolith, the following workflow is used:

1. Take source and deb tarball from the Ubuntu package for the version to port. (eg: https://packages.ubuntu.com/mantic/gnome-control-center)
2. Apply source tarballs to a new branch of this repo
3. Apply all upstream patches, commit the changes, and remove the patches (eg: `quilt push -a`)
4. Apply commits from previous Regolith version: build refactor, name change, logo, wallpaper
5. Fix problems until building via Voulage