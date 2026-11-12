# keyclean

A tiny KDE Plasma 5.27/6 / GNOME panel button that temporarily ignores the
physical keyboard so you can wipe it while the machine is on. Click again with
the mouse to restore it. It is the Linux, Wayland-friendly equivalent of
OneMenu's "keyboard cleaning mode", nothing more.

- Works on Wayland and X11 because it grabs evdev keyboards directly.
- KDE: add the "Keyboard Cleaning" widget to the panel.
- Optional KDE system-tray icon (`./install.sh --tray`).
- GNOME: enable the extension; it appears in the top bar.
- Mouse/touchpad keep working the whole time.

## Safety

- Click the panel icon again to restore the keyboard.
- Press **Esc five times quickly** while cleaning to restore it from the keyboard
  itself (works even though all other keys are ignored).
- The daemon restores the keyboard automatically after **10 minutes**.
- The daemon releases the keyboard on logout (it exits when its runtime socket
  is removed) and on SIGTERM/SIGINT.
- Devices that combine a keyboard and a mouse in one evdev node are skipped on
  purpose: grabbing one would also stop the mouse, leaving no way to click the
  button again.

## Install

```sh
./install.sh
```

This builds the helper, installs it to `/usr/local/bin`, installs a udev
`uaccess` rule, and copies the matching KDE widget (Plasma 5 or 6 by detection)
and the GNOME extension into your home directory. It needs sudo for the binary
and udev rule only.

To also put the toggle in the KDE system tray:

```sh
./install.sh --tray
```

That installs a small StatusNotifierItem helper and an autostart entry. It
needs `python3-dbus` and `python3-gi` (Debian/Ubuntu) or `python3-dbus` and
`python3-gobject` (Fedora/Nobara). On GNOME the tray item needs an AppIndicator
extension; the GNOME panel extension already gives you a button without it.
You can also run `keyclean-tray` manually for the current session.

Then:

- **KDE**: right-click the panel → Add Widgets → search "Keyboard Cleaning".
- **GNOME**: log out/in, then `gnome-extensions enable keyclean@local`.

If your distributions blocks access to `/dev/input` even after the udev rule,
add yourself to the `input` group and log out/in:

```sh
sudo usermod -aG input "$USER"
```

## Use

Click the icon: the keyboard is ignored and the icon changes to a "blocked"
symbol. Click again to restore. The same helper can be used from a terminal:

```sh
keyclean toggle   # on/off
keyclean status   # prints on or off
keyclean is-on    # exit code 0 if on, 1 if off
keyclean on
keyclean off
keyclean stop     # stop the background helper
```

Change the auto-release time (seconds, 0 = never) by exporting
`KEYCLEAN_TIMEOUT` before the helper first starts, e.g. in your shell profile:

```sh
export KEYCLEAN_TIMEOUT=300
```

The panel button usually starts the helper first, so set it in the session
environment rather than only in an interactive shell.

## How it works

`keyclean` is a small C helper. On `on` it opens every `/dev/input/event*`
device that looks like a keyboard and calls `EVIOCGRAB`. While grabbed, the
compositor (KWin, Mutter, any X11 server) receives no key events; the helper
receives them and discards them. It listens for the Esc panic sequence and for
commands on a per-user Unix socket in `$XDG_RUNTIME_DIR`. The panel applets are
thin front-ends that run `keyclean toggle`.

No kernel module, no X11-only tooling, no compositor-specific API.

## Files

```
src/keyclean.c                 helper daemon + CLI
src/keyclean-tray              optional StatusNotifierItem tray front-end
data/70-keyclean.rules         udev uaccess rule
kde/org.keyclean/              Plasma 6 applet
kde5/org.keyclean.plasma5/     Plasma 5.27 applet
gnome/keyclean@local/          GNOME Shell 45-49 extension
test/                          sandbox tests with a virtual uinput keyboard
```

## Test (isolated)

The tests run in a throwaway Fedora container. The container gets `/dev/uinput`
and a cgroup rule for creating its own virtual event node; the host keyboards
are not mounted into it.

```sh
./test/sandbox.sh
```

Because uinput devices are still visible to the host kernel, the script asks
for sudo once to install a temporary udev rule that makes libinput ignore the
test keyboard, then removes the rule on exit. That keeps the synthetic key
presses away from the live desktop. If you cannot use sudo and accept the risk,
run `ALLOW_UNSAFE=1 ./test/sandbox.sh`.

It verifies that events reach a reader when off, that no events leak while
grabbed, toggle/status, the Esc panic sequence, auto-timeout, daemon shutdown,
and that an unprivileged user can grab the keyboard. It also validates the JSON
metadata and the extension's JavaScript syntax. This was tested in that
container only, not on a live desktop.

The Plasma 5.27 applet and the helper were also tested on a Debian 12 /
Plasma 5.27 Wayland VM with the real virtual keyboard and an injected mouse:
the panel icon appears, clicking it toggles the grab, while grabbed the Meta
key no longer reaches KWin, clicking again restores it, Esc five times releases
it, and the auto-timeout returns the icon to its off state. The optional
system-tray helper was tested on the same VM: it appears in the KDE system tray,
a left click toggles the grab, and its icon follows the daemon state. The
Plasma 6 applet and the GNOME extension were not run in a live shell; their
metadata and JavaScript were validated, and the Plasma 6 executable data engine
was checked against the KDE 6.7.5 source.

## Uninstall

```sh
keyclean stop
sudo rm -f /usr/local/bin/keyclean /usr/local/bin/keyclean-tray
sudo rm -f /etc/udev/rules.d/70-keyclean.rules
rm -f ~/.config/autostart/keyclean-tray.desktop
rm -rf ~/.local/share/plasma/plasmoids/org.keyclean
rm -rf ~/.local/share/plasma/plasmoids/org.keyclean.plasma5
rm -rf ~/.local/share/gnome-shell/extensions/keyclean@local
```

## Limitations

- Only grabs devices that report the normal letter keys. A keyboard/touchpad
  combo exposed as one evdev node is intentionally left alone (see Safety).
- If a key is physically held down at the exact moment cleaning mode starts,
  its release may be missed by the compositor. Release it and press it again.
- The 10 minute timer is fixed per helper run unless `KEYCLEAN_TIMEOUT` is set
  in the environment that starts the helper.
