#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

make clean all test

export XDG_RUNTIME_DIR=/tmp/keyclean-runtime
rm -rf "$XDG_RUNTIME_DIR"
mkdir -m 700 "$XDG_RUNTIME_DIR"
export KEYCLEAN_TIMEOUT=4

mkdir -p /dev/input
if [ ! -e /dev/uinput ]; then
    mknod /dev/uinput c 10 223
fi

FIFO=/tmp/keyclean-test.fifo
rm -f "$FIFO"
mkfifo "$FIFO"
exec 3<>"$FIFO"

./test/fakekbd <"$FIFO" >/tmp/keyclean-kbdpath 2>/tmp/keyclean-fakekbd.err &
FAKE=$!

cleanup() {
    ./keyclean stop >/dev/null 2>&1 || true
    printf 'q\n' >&3 2>/dev/null || true
    exec 3>&- 2>/dev/null || true
    wait "$FAKE" 2>/dev/null || true
}
trap cleanup EXIT

i=0
while [ "$i" -lt 100 ] && [ ! -s /tmp/keyclean-kbdpath ]; do
    if ! kill -0 "$FAKE" 2>/dev/null; then
        cat /tmp/keyclean-fakekbd.err
        exit 1
    fi
    i=$((i + 1))
    sleep 0.05
done

DEV=$(head -n 1 /tmp/keyclean-kbdpath)
[ -n "$DEV" ] || { echo "FAIL: no virtual event node"; exit 1; }

if [ ! -e "$DEV" ]; then
    MAJMIN=$(cat "/sys/class/input/$(basename "$DEV")/dev")
    MAJ=${MAJMIN%%:*}
    MIN=${MAJMIN#*:}
    mknod "$DEV" c "$MAJ" "$MIN"
fi

fail() { echo "FAIL: $*"; exit 1; }
emit() { printf '%s\n' "$1" >&3; sleep 0.2; }

echo "helper auto-start / status"
[ "$(./keyclean status)" = "off" ] || fail "initial status"

echo "baseline: key event reaches a normal reader"
./test/evmon "$DEV" 800 >/tmp/mon.out &
MON=$!
sleep 0.15
emit a
wait "$MON" || fail "baseline key event was not delivered"
grep -q . /tmp/mon.out || fail "baseline reader saw no key"

echo "on: grabbed keyboard must not leak events to the compositor"
./keyclean on >/dev/null
[ "$(./keyclean status)" = "on" ] || fail "status after on"
./test/evmon "$DEV" 800 >/tmp/mon.out &
MON=$!
sleep 0.15
emit a
if wait "$MON"; then fail "key event leaked while cleaning mode was on"; fi
[ ! -s /tmp/mon.out ] || fail "reader received events while grabbed"

echo "off: events flow again"
./keyclean off >/dev/null
[ "$(./keyclean status)" = "off" ] || fail "status after off"
./test/evmon "$DEV" 800 >/tmp/mon.out &
MON=$!
sleep 0.15
emit a
wait "$MON" || fail "event did not return after off"

echo "toggle: on then off"
./keyclean toggle >/dev/null
[ "$(./keyclean status)" = "on" ] || fail "toggle did not enable"
./keyclean is-on || fail "is-on should succeed while enabled"
./keyclean toggle >/dev/null
[ "$(./keyclean status)" = "off" ] || fail "toggle did not disable"
if ./keyclean is-on; then fail "is-on should fail while disabled"; fi

echo "panic: Esc five times releases the keyboard"
./keyclean on >/dev/null
i=0
while [ "$i" -lt 5 ]; do
    emit esc
    i=$((i + 1))
done
sleep 0.3
[ "$(./keyclean status)" = "off" ] || fail "panic sequence did not release"

echo "timeout: auto-release after ${KEYCLEAN_TIMEOUT}s"
./keyclean on >/dev/null
sleep 5
[ "$(./keyclean status)" = "off" ] || fail "auto timeout did not release"

echo "stop: daemon releases and removes its socket"
./keyclean stop >/dev/null
sleep 0.5
[ ! -S "$XDG_RUNTIME_DIR/keyclean.sock" ] || fail "socket still present after stop"

echo "unprivileged user can grab without root"
chmod 666 "$DEV"
NRT=/tmp/keyclean-nobody
rm -rf "$NRT"
mkdir -m 700 "$NRT"
chown 65534:65534 "$NRT"
RUNAS="setpriv --reuid=65534 --regid=65534 --clear-groups"
$RUNAS env XDG_RUNTIME_DIR="$NRT" KEYCLEAN_TIMEOUT=10 ./keyclean on >/dev/null
[ "$($RUNAS env XDG_RUNTIME_DIR="$NRT" ./keyclean status)" = "on" ] || fail "unprivileged on failed"
./test/evmon "$DEV" 800 >/tmp/mon.out &
MON=$!
sleep 0.15
emit a
if wait "$MON"; then fail "unprivileged grab leaked events"; fi
$RUNAS env XDG_RUNTIME_DIR="$NRT" ./keyclean off >/dev/null
$RUNAS env XDG_RUNTIME_DIR="$NRT" ./keyclean stop >/dev/null 2>&1 || true

python3 -m json.tool kde/org.keyclean/metadata.json >/dev/null
python3 -m json.tool gnome/keyclean@local/metadata.json >/dev/null
cp gnome/keyclean@local/extension.js /tmp/extension.mjs
node --check /tmp/extension.mjs

echo "ALL SANDBOX TESTS PASSED"
