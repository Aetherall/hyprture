# hyprture

> **AI disclaimer.** Most of this code was written by an AI assistant
> (Anthropic's Claude), directed and reviewed by the author. Read it before
> trusting it.

Apertures for Hyprland: a rectangle in which Hyprland draws a workspace, a
window or a monitor, live. Whoever asks says where the view goes; Hyprland
renders the source and composites it there. No pixels travel to the asker,
and a hidden workspace works as well as a shown one.

Three ways in, for three kinds of consumers:

| Who | How | Where the aperture is drawn |
| --- | --- | --- |
| A client (bar, overview, switcher) | the `hyprture_v1` Wayland protocol, or the `Hyprture` QML module | in a rectangle of its own layer surface, above the surface's content |
| A Hyprland config | `hl.plugin.hyprture.*` in Lua | on a monitor, above everything or above windows |
| Another plugin | the C API in [`hyprture.h`](hyprture.h) | on a monitor, like Lua |

**Status:** early. Built for Hyprland 0.57 with its Lua config.

## QML

```qml
import Quickshell
import Hyprture

PanelWindow {
    // …
    Aperture {
        anchors.fill: parent
        workspace: 6          // or window: "0x5a1b…", or monitor: "DP-2"
        scene: Aperture.Windows // or Aperture.Shell: with wallpaper and layers
        radius: 8
        live: true
    }
}
```

The view is fitted inside the item with the source's aspect ratio, centered;
`sourceSize` gives that size so the item can match it. The item's effective
opacity (its own and its ancestors') applies. `available` says whether the
compositor runs hyprture. The window must be a layer surface (a Quickshell
`PanelWindow`).

## Lua

```lua
local id = hl.plugin.hyprture.aperture({
    monitor = "DP-3",
    level   = "overlay",                         -- or "windows"
    box     = { x = 40, y = 40, w = 480, h = 270 }, -- monitor-local, logical
    source  = { workspace = 6 },                 -- { window = "0x…" }, { monitor = "DP-2" }
    radius  = 12,
})
hl.plugin.hyprture.update(id, { source = { workspace = 7 } })
hl.plugin.hyprture.remove(id)
```

## The protocol

[`protocol/hyprture-v1.xml`](protocol/hyprture-v1.xml): `get_aperture(parent
wl_surface)`, then a source, a destination rectangle, opacity, radius and
liveness, all applied on the parent's commit. A `source` event reports the
source's size.

## How it works

Before a monitor renders a frame, each aperture shown on it renders its
source offscreen, through the same renderer path as Hyprland's own workspace
capture (a workspace in its workspace scene, a window alone). The frame then
draws that framebuffer where the aperture is: right after its parent layer
surface, or at a stage of the frame for apertures on a monitor. Live
apertures damage themselves every frame, keep their source windows awake
(Hyprland suspends the windows of hidden workspaces) and give them frame
callbacks. Nothing is drawn while the session is locked.

Unloading the plugin removes the global, then the objects: the QML module
drops its apertures without a protocol error and makes them again when
hyprture comes back.

## Developing

The devenv shell (`direnv allow`, or `devenv shell`) has the Hyprland that
dots runs, pinned in `devenv.yaml`: `make` builds the plugin, `make qml` the
QML module (in `.build/qml`), `make test` the tests, and `devenv test` all
of it. The flake packages both for Nix users.

Never load a development build into your own session: [`dev/nest.sh`](dev/nest.sh)
runs a Hyprland nested in yours, isolated from your session's systemd and
D-Bus (a nested Hyprland otherwise stops your graphical session when it
exits), with its own headless output to screenshot. [`dev/scene.sh`](dev/scene.sh)
puts a ticking clock on a hidden workspace of it.

## Installation (Nix)

Follow your Hyprland input; the QML module must be built with the Qt of the
program that loads it:

```nix
inputs.hyprture = {
  url = "github:aetherall/hyprture";
  inputs.hyprland.follows = "hyprland";
};
# plugin: ${inputs.hyprture.packages.${system}.default}/lib/libhyprture.so
# QML:    inputs.hyprture.lib.mkQml pkgs  ->  QML_IMPORT_PATH=…/lib/qt-6/qml
```

## License

BSD 3-Clause, see [LICENSE](LICENSE).
