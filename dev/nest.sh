#!/usr/bin/env bash
# A throwaway Hyprland, nested in the running one, with hyprture loaded: the
# plugin is never tried in your own session. Its window goes to a hidden
# special workspace; it draws on a headless output instead (HEADLESS-2, its
# own frame clock), which `dev/nest.sh shot` captures.
#
#   dev/nest.sh start        start it (builds nothing: run make first)
#   dev/nest.sh run CMD…     run a command in it (its runtime dir, display, hyprctl)
#   dev/nest.sh eval LUA     hyprctl eval in it
#   dev/nest.sh shot FILE    screenshot of its headless output
#   dev/nest.sh stop
#
# Isolation: a Hyprland that finds a seat session believes it is the session.
# It imports its WAYLAND_DISPLAY and HYPRLAND_INSTANCE_SIGNATURE into systemd
# and D-Bus when it starts, and runs `systemctl --user stop
# graphical-session.target` when it exits, which stops every app of the real
# session. So the nested one gets its own XDG_RUNTIME_DIR (no systemd user
# manager there), no D-Bus session bus, and
# Hyprland's systemd integration switched off; and it is only ever SIGKILLed,
# so no exit cleanup runs at all.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
HOST_RUNTIME=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
HOST_WAYLAND=${WAYLAND_DISPLAY:-wayland-1}
DIR=$HOST_RUNTIME/hyprture-nest
# Short: the IPC socket path in it must fit a sockaddr_un.
RUNTIME=$HOST_RUNTIME/hn
mkdir -p "$RUNTIME"
chmod 700 "$RUNTIME"

ISOLATED=(
    HOME="$HOME" PATH="$PATH" USER="${USER:-}" TERM="${TERM:-dumb}"
    XDG_RUNTIME_DIR="$RUNTIME"
    DBUS_SESSION_BUS_ADDRESS="unix:path=$RUNTIME/no-bus"
    HYPRLAND_NO_SD_VARS=1 HYPRLAND_NO_SD_TARGET=1
)
isolated() {
    env -i "${ISOLATED[@]}" "$@"
}

instance() {
    [ -f "$DIR/pid" ] || { echo "not running" >&2; exit 1; }
    isolated hyprctl instances -j | jq -r --argjson pid "$(cat "$DIR/pid")" '.[] | select(.pid == $pid) | .instance'
}
socket() {
    isolated hyprctl instances -j | jq -r --argjson pid "$(cat "$DIR/pid")" '.[] | select(.pid == $pid) | .wl_socket'
}
inside() {
    isolated WAYLAND_DISPLAY="$(socket)" HYPRLAND_INSTANCE_SIGNATURE="$(instance)" "$@"
}

case "${1:-}" in
start)
    BEFORE=$(systemctl --user show-environment | sort | md5sum)
    cat > "$DIR/hyprland.lua" <<LUA
hl.plugin.load("$ROOT/hyprture.so")
hl.config({
    misc = { disable_hyprland_logo = true, disable_splash_rendering = true },
    animations = { enabled = false },
    debug = { disable_logs = false },
    general = { gaps_in = 4, gaps_out = 8, border_size = 2 },
    decoration = { rounding = 8 },
})
hl.monitor({ output = "WAYLAND-1", mode = "640x360", position = "-2000x0", scale = 1 })
hl.monitor({ output = "HEADLESS-2", mode = "1280x720@60", position = "0x0", scale = 1 })
LUA
    # The nested window, out of sight in the session running it.
    hyprctl eval 'hl.window_rule({ name = "hyprture-nest", match = { class = "^aquamarine$" }, workspace = "special:hyprture-nest silent" })' >/dev/null
    # Its host display by absolute path: its own runtime dir is elsewhere.
    env -i "${ISOLATED[@]}" WAYLAND_DISPLAY="$HOST_RUNTIME/$HOST_WAYLAND" Hyprland -c "$DIR/hyprland.lua" >"$DIR/log" 2>&1 &
    echo $! >"$DIR/pid"
    for _ in $(seq 50); do
        [ -n "$(instance 2>/dev/null)" ] && break
        sleep 0.2
    done
    # Should it ever reach the session anyway: gone before it can do more.
    if [ "$(systemctl --user show-environment | sort | md5sum)" != "$BEFORE" ]; then
        pkill -KILL -f "^Hyprland -c $DIR/hyprland.lua" || true
        echo "the nested Hyprland changed the session's environment; killed it" >&2
        exit 1
    fi
    inside hyprctl output create headless HEADLESS-2 >/dev/null
    sleep 0.5
    echo "nested: $(instance) on $RUNTIME/$(socket)"
    ;;
run)
    shift
    inside "$@"
    ;;
eval)
    inside hyprctl eval "$2"
    ;;
shot)
    inside grim -o HEADLESS-2 "$2"
    ;;
stop)
    # SIGKILL only: a clean exit would run Hyprland's session cleanup.
    pkill -KILL -f "^Hyprland -c $DIR/hyprland.lua" || true
    rm -f "$DIR/pid"
    ;;
*)
    sed -n '2,12p' "$0"
    exit 1
    ;;
esac
