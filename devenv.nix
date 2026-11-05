{ pkgs, inputs, ... }:

let
  hyprland = inputs.hyprland.packages.${pkgs.stdenv.hostPlatform.system}.hyprland;
in
{
  # A plugin is built with the compiler of the Hyprland that loads it.
  stdenv = hyprland.stdenv;

  packages = [
    # The plugin (make).
    hyprland.dev
    pkgs.pkg-config
    pkgs.hyprwayland-scanner
    # The QML module (cmake -S qml -B .build/qml).
    pkgs.cmake
    pkgs.wayland-scanner
    pkgs.qt6.qtbase
    pkgs.qt6.qtdeclarative
    # dev/nest.sh
    pkgs.jq
    pkgs.grim
    pkgs.clang-tools
  ] ++ hyprland.buildInputs;

  enterTest = ''
    make
    make qml
    make test
  '';
}
