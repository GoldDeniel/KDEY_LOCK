#!/bin/sh
set -eu

cd "$(dirname "$0")"
PREFIX=${PREFIX:-/usr/local}
SUDO=${SUDO:-sudo}
INSTALL_TRAY=${INSTALL_TRAY:-0}

for arg in "$@"; do
    case "$arg" in
        --tray) INSTALL_TRAY=1 ;;
        --no-tray) INSTALL_TRAY=0 ;;
        -h|--help)
            echo "usage: ./install.sh [--tray]"
            echo "  --tray  install the system-tray helper and autostart it"
            exit 0
            ;;
    esac
done

make

$SUDO install -Dm755 keyclean "$PREFIX/bin/keyclean"
$SUDO install -Dm644 data/70-keyclean.rules /etc/udev/rules.d/70-keyclean.rules
$SUDO udevadm control --reload-rules
$SUDO udevadm trigger --subsystem-match=input --action=change

mkdir -p "$HOME/.local/share/plasma/plasmoids"
mkdir -p "$HOME/.local/share/gnome-shell/extensions"
rm -rf "$HOME/.local/share/plasma/plasmoids/org.keyclean"
rm -rf "$HOME/.local/share/plasma/plasmoids/org.keyclean.plasma5"
rm -rf "$HOME/.local/share/gnome-shell/extensions/keyclean@local"

KDE_MAJOR=""
if command -v kwin_wayland >/dev/null 2>&1; then
    KDE_MAJOR=$(kwin_wayland --version 2>/dev/null | awk '/^kwin / {print $2}' | cut -d. -f1)
fi

if [ "$KDE_MAJOR" = "5" ]; then
    cp -a kde5/org.keyclean.plasma5 "$HOME/.local/share/plasma/plasmoids/"
    PLASMA=5
elif [ "$KDE_MAJOR" = "6" ]; then
    cp -a kde/org.keyclean "$HOME/.local/share/plasma/plasmoids/"
    PLASMA=6
elif command -v kpackagetool6 >/dev/null 2>&1; then
    cp -a kde/org.keyclean "$HOME/.local/share/plasma/plasmoids/"
    PLASMA=6
elif command -v kpackagetool5 >/dev/null 2>&1; then
    cp -a kde5/org.keyclean.plasma5 "$HOME/.local/share/plasma/plasmoids/"
    PLASMA=5
else
    PLASMA=0
fi
cp -a gnome/keyclean@local "$HOME/.local/share/gnome-shell/extensions/"

TRAY_OK=0
if command -v python3 >/dev/null 2>&1 && python3 -c 'import dbus, gi' >/dev/null 2>&1; then
    $SUDO install -Dm755 src/keyclean-tray "$PREFIX/bin/keyclean-tray"
    TRAY_OK=1
fi

TRAY_FAIL=0
if [ "$INSTALL_TRAY" = 1 ]; then
    if [ "$TRAY_OK" = 1 ]; then
        mkdir -p "$HOME/.config/autostart"
        cat > "$HOME/.config/autostart/keyclean-tray.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Keyboard Cleaning Tray
Comment=Tray icon to toggle keyboard cleaning
Exec=$PREFIX/bin/keyclean-tray
Terminal=false
NoDisplay=true
X-GNOME-Autostart-enabled=true
EOF
        chmod 644 "$HOME/.config/autostart/keyclean-tray.desktop"
    else
        echo "Tray requested, but python3-dbus and python3-gi/python3-gobject are missing." >&2
        echo "Install them and rerun: ./install.sh --tray" >&2
        TRAY_FAIL=1
    fi
fi

cat <<EOF
Installed keyclean.

Safety: click the panel icon again with the mouse to restore the keyboard,
press Esc five times quickly, or wait for the 10 minute auto-release.
EOF

if [ "$PLASMA" != 0 ]; then
    echo "KDE Plasma $PLASMA: right-click the panel -> Add Widgets -> search \"Keyboard Cleaning\"."
fi
cat <<EOF
GNOME: log out and back in, then run:
         gnome-extensions enable keyclean@local
EOF

if [ "$INSTALL_TRAY" = 1 ] && [ "$TRAY_OK" = 1 ]; then
    echo "System tray: enabled at login (keyclean-tray)."
elif [ "$TRAY_OK" = 1 ]; then
    echo "System tray: run keyclean-tray now, or rerun './install.sh --tray' to autostart it."
fi

exit "$TRAY_FAIL"
