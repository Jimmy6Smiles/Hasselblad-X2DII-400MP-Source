#!/system/bin/sh
# Common-launcher child only. Owns Pixel Shift RAM/data paths, never services.
set -eu
umask 077
ROOT=${PS_FEATURE_ROOT:?}
test "${CM_SHARED_BACKEND:-}" = 1
test "$ROOT" = /system/camera-mods/pixel-shift-400mp/1.2.4-rc4
cd "$ROOT"
sha256sum -c manifest.sha256

make_ram_dir() {
 P=$1
 if test ! -e "$P"; then mkdir -m 700 "$P"; chcon u:object_r:sel_device:s0 "$P"; fi
 test -d "$P" && test ! -L "$P"
}
make_data_dir() {
 P=$1
 if test ! -e "$P"; then mkdir -m 700 "$P"; fi
 test -d "$P" && test ! -L "$P"
}

R=/dev/x2d2-integrated-v1
S=/dev/x2d2-shutter-v1
N=/dev/x2d2-pregdc-trial
A=/dev/x2d2-album-refresh-v1
D=/data/x2d2-integrated-v1
make_ram_dir "$R"
make_ram_dir "$R/jobs"
make_ram_dir "$R/cache"
make_ram_dir "$R/scratch"
make_ram_dir "$S"
make_ram_dir "$N"
make_ram_dir "$A"
make_data_dir "$D"
make_data_dir "$D/jobs"

# Publish only this feature's immutable tools into its historical private ABI.
# /dev is new each boot, so any pre-existing name indicates a foreign owner.
while read HASH NAME; do
 test -n "$HASH" && test -n "$NAME"
 test ! -e "$R/$NAME"
 ln -s "$ROOT/tools/$NAME" "$R/$NAME"
done < "$ROOT/tools.list"

PID=$(pidof camera-service)
case "$PID" in ''|*[!0-9]*) exit 3;; esac
grep -Fq " $ROOT/capture-plugin.so" "/proc/$PID/maps"
exec "$ROOT/backend-entry" --services
