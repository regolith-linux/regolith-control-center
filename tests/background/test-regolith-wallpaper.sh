#!/bin/bash
# test-regolith-wallpaper.sh <path to regolith-wallpaper>
# Checks the resources the helper writes and that it updates sway without
# reloading it. swaymsg, trawldb, regolith-set-swaybg and xrdb are stand-ins.
set -eu

helper=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

check() { # check <description> <command...>
    local what=$1
    shift
    if "$@"; then
        echo "ok: $what"
    else
        echo "FAIL: $what"
        fail=1
    fi
}

mkdir -p "$work/bin"
for tool in swaymsg trawldb regolith-set-swaybg xrdb; do
    cat > "$work/bin/$tool" <<STUB
#!/bin/bash
echo "$tool \$*" >> "$work/calls"
if [ "$tool" = trawldb ] && [ "\${1-}" = --merge ]; then cat "\$2" >> "$work/trawl"; fi
if [ "$tool" = xrdb ]; then cat >> "$work/xrdb"; fi
exit 0
STUB
    chmod +x "$work/bin/$tool"
done

run() { # run <session: none|wayland|x11> <args...>
    local session=$1
    shift
    local vars=()
    case $session in
        wayland) vars=(WAYLAND_DISPLAY=wayland-1 DISPLAY=:0) ;;
        x11) vars=(DISPLAY=:0) ;;
    esac
    rm -f "$work/calls" "$work/trawl" "$work/xrdb"
    env -i HOME="$work/home" PATH="$work/bin:/usr/bin:/bin" "${vars[@]}" \
        bash "$helper" "$@"
}
override=$work/home/.config/regolith3/Xresources

# A first choice, outside a session: the resources are written, nothing runs
run none --options zoom /usr/share/backgrounds/a.png /usr/share/backgrounds/b.png
check "first choice is written" diff -u - "$override" <<OUT
regolith.wallpaper.file: /usr/share/backgrounds/a.png
regolith.lockscreen.wallpaper.file: /usr/share/backgrounds/b.png
regolith.wallpaper.options: zoom
regolith.lockscreen.wallpaper.options: zoom
OUT
check "nothing runs outside a session" test ! -e "$work/calls"

# The user's earlier values are commented out, older comments dropped,
# other resources kept
cat > "$override" <<IN
regolith.look.path: /usr/share/regolith-look/lascaille
!regolith.wallpaper.file: /older.png
regolith.wallpaper.file: /old.png
regolith.wallpaper.color.primary: #112233
IN
run none file:///home/u/My%20Pictures/c.jpg
check "earlier values are replaced" diff -u - "$override" <<OUT
regolith.look.path: /usr/share/regolith-look/lascaille
!regolith.wallpaper.file: /old.png
regolith.wallpaper.color.primary: #112233
regolith.wallpaper.file: /home/u/My Pictures/c.jpg
regolith.lockscreen.wallpaper.file: /home/u/My Pictures/c.jpg
OUT

# Wayland: the running session's resources and sway's background are updated,
# sway is not reloaded
run wayland --options zoom /usr/share/backgrounds/d.png
check "trawl gets the new resources" grep -qx 'regolith.wallpaper.file: /usr/share/backgrounds/d.png' "$work/trawl"
check "sway background is set again" grep -q '^regolith-set-swaybg' "$work/calls"
check "sway is not reloaded" bash -c "! grep -q reload '$work/calls'"
check "one value per resource" test "$(grep -c '^regolith.wallpaper.file:' "$override")" = 1

# X11: only the resource database is updated
run x11 /usr/share/backgrounds/e.png
check "xrdb gets the new resources" grep -qx 'regolith.wallpaper.file: /usr/share/backgrounds/e.png' "$work/xrdb"
check "sway is left alone in X11" bash -c "! grep -q -e swaymsg -e regolith-set-swaybg '$work/calls'"

# Not a local file: refused, the override file is left as it was
cp "$override" "$work/before"
check "a non-local URI is refused" bash -c "! env -i HOME='$work/home' PATH='$work/bin:/usr/bin:/bin' bash '$helper' https://example.org/x.png 2>/dev/null"
check "the file is unchanged after a refusal" cmp -s "$work/before" "$override"

exit $fail
