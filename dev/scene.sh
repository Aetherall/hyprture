#!/usr/bin/env bash
# A scene in the nested Hyprland (dev/nest.sh): a ticking clock on hidden
# workspace 3, a window on workspace 2, which is shown.
set -euo pipefail
N=$(dirname "$0")/nest.sh
spawn() { setsid "$N" run kitty "$@" >/dev/null 2>&1 & }

"$N" eval 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-2" })); hl.dispatch(hl.dsp.focus({ workspace = 3 }))' >/dev/null
spawn --class clock -o font_size=40 -o background=#203050 sh -c 'while true; do clear; date +%T.%N | cut -c1-10; sleep 0.2; done'
sleep 1.2
"$N" eval 'hl.dispatch(hl.dsp.focus({ workspace = 2 }))' >/dev/null
spawn --class two -o background=#402020 sh -c 'echo workspace two; sleep 1e9'
sleep 1.2
