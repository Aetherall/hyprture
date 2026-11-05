{
  description = "hyprture: apertures, live views of workspaces and windows that Hyprland draws into a rectangle";

  inputs = {
    hyprland.url = "github:hyprwm/Hyprland";
    nixpkgs.follows = "hyprland/nixpkgs";
    systems.follows = "hyprland/systems";
  };

  outputs = {
    self,
    hyprland,
    nixpkgs,
    systems,
    ...
  }: let
    inherit (nixpkgs) lib;
    eachSystem = lib.genAttrs (import systems);
    version = self.shortRev or self.dirtyShortRev or "unknown";

    src = lib.cleanSourceWith {
      src = lib.cleanSource self;
      filter = name: _type: let
        base = baseNameOf name;
      in
        base != ".build" && base != "hyprture.so" && !(lib.hasPrefix "result" base);
    };

    # The plugin must be built with the headers and compiler of the Hyprland
    # that loads it, so the build takes that Hyprland package as an argument.
    mkPlugin = pkgs: hyprlandPackage:
      hyprlandPackage.stdenv.mkDerivation {
        pname = "hyprture";
        inherit version src;

        nativeBuildInputs = [pkgs.pkg-config pkgs.hyprwayland-scanner];
        buildInputs = [hyprlandPackage.dev] ++ hyprlandPackage.buildInputs;

        doCheck = true;
        checkTarget = "test";

        installPhase = ''
          runHook preInstall
          install -Dm755 hyprture.so $out/lib/libhyprture.so
          install -Dm644 hyprture.h $out/include/hyprture.h
          install -Dm644 protocol/hyprture-v1.xml $out/share/hyprture/hyprture-v1.xml
          runHook postInstall
        '';

        meta = {
          description = "Live views of Hyprland workspaces and windows, for plugins and clients";
          homepage = "https://github.com/aetherall/hyprture";
          license = lib.licenses.bsd3;
          platforms = lib.platforms.linux;
        };
      };

    # The QML module (import Hyprture): the client side of the protocol. Built
    # with the Qt of the program that loads it (quickshell), hence `pkgs`.
    mkQml = pkgs:
      pkgs.stdenv.mkDerivation {
        pname = "hyprture-qml";
        inherit version;
        src = "${src}/qml";

        nativeBuildInputs = [pkgs.cmake pkgs.pkg-config pkgs.wayland-scanner pkgs.qt6.wrapQtAppsHook];
        buildInputs = [pkgs.qt6.qtbase pkgs.qt6.qtdeclarative pkgs.wayland];
        dontWrapQtApps = true;

        cmakeFlags = ["-DHYPRTURE_PROTOCOL_DIR=${src}/protocol"];
      };
  in {
    packages = eachSystem (system: let
      pkgs = nixpkgs.legacyPackages.${system};
    in rec {
      hyprture = mkPlugin pkgs hyprland.packages.${system}.hyprland;
      qml = mkQml pkgs;
      default = hyprture;
    });

    # Builds against the `hyprland` and Qt in the package set it is applied to.
    overlays.default = final: _prev: {
      hyprture = mkPlugin final final.hyprland;
      hyprture-qml = mkQml final;
    };

    lib = {inherit mkPlugin mkQml;};

    formatter = eachSystem (system: nixpkgs.legacyPackages.${system}.alejandra);
  };
}
