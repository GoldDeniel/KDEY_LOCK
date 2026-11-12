#!/bin/sh
# Run the functional tests in a throwaway container. The container only gets
# /dev/uinput plus the ability to create the one virtual event node; host
# keyboards are not mounted into it.
#
# uinput devices still show up in the host kernel, so this script installs a
# temporary udev rule telling libinput to ignore the test keyboard. Without it
# the synthetic key presses could reach the live desktop. The rule is removed
# on exit. If sudo is unavailable, ALLOW_UNSAFE=1 runs without that protection.
set -eu

cd "$(dirname "$0")/.."
ENGINE=${CONTAINER_ENGINE:-docker}
IMAGE=${IMAGE:-fedora:44}
SUDO=${SUDO:-sudo}
RULE=/run/udev/rules.d/99-keyclean-test-ignore.rules

cleanup() {
    $SUDO rm -f "$RULE" 2>/dev/null || true
    $SUDO udevadm control --reload-rules 2>/dev/null || true
}

if [ "${ALLOW_UNSAFE:-0}" = "1" ]; then
    echo "WARNING: running without the udev ignore rule; test keys may reach the live desktop."
else
    echo "Installing temporary udev rule so the live compositor ignores the test keyboard"
    printf '%s\n' 'ACTION!="remove", SUBSYSTEM=="input", KERNEL=="event*", ATTRS{name}=="keyclean-test-kbd", ENV{LIBINPUT_IGNORE_DEVICE}="1", ENV{ID_INPUT}="", ENV{ID_INPUT_KEY}="", ENV{ID_INPUT_KEYBOARD}=""' \
        > /tmp/keyclean-test-ignore.rules
    if ! $SUDO install -Dm644 /tmp/keyclean-test-ignore.rules "$RULE"; then
        rm -f /tmp/keyclean-test-ignore.rules
        echo "Could not install the udev rule. Re-run with ALLOW_UNSAFE=1 if you accept that" >&2
        echo "synthetic key presses may reach the live desktop." >&2
        exit 1
    fi
    rm -f /tmp/keyclean-test-ignore.rules
    $SUDO udevadm control --reload-rules
    trap cleanup EXIT INT TERM
fi

echo "Building/running test container ($ENGINE, $IMAGE)"
"$ENGINE" run --rm \
    --cap-add MKNOD \
    --device /dev/uinput \
    --device-cgroup-rule 'c 13:* rwm' \
    --security-opt label=disable \
    -v "$PWD":/src \
    -w /src \
    "$IMAGE" \
    sh -c "dnf -y install gcc make python3 nodejs util-linux >/dev/null && ./test/run.sh; rc=\$?; chown -R $(id -u):$(id -g) /src; exit \$rc"
