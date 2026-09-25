{
  description = "Power, battery, fan and keyboard settings for Lenovo Legion laptops";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachSystem [ "x86_64-linux" ] (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        qt6 = pkgs.qt6;
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "cohort";
          version = "0.1.0";

          src = pkgs.lib.cleanSource ./.;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            qt6.wrapQtAppsHook
          ];

          buildInputs = [
            qt6.qtbase
            qt6.qtdeclarative
            qt6.qtsvg
            qt6.qtwayland
          ];

          # The polkit policy names the helper by its store path, which is
          # the libexec directory CMake is given here.
          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DBUILD_TESTING=OFF"
          ];

          meta = {
            description = "Power, battery, fan and keyboard settings for Lenovo Legion laptops";
            homepage = "https://github.com/yappologistic/Cohort";
            license = pkgs.lib.licenses.gpl3Plus;
            platforms = [ "x86_64-linux" ];
            mainProgram = "cohort";
          };
        };

        devShells.default = pkgs.mkShell {
          packages = [
            pkgs.cmake
            pkgs.ninja
            qt6.qtbase
            qt6.qtdeclarative
            qt6.qtsvg
            qt6.qtwayland
          ];
        };
      }
    )
    // {
      # programs.cohort.enable installs Cohort where polkit reads its policy.
      # programs.cohort.legionModule also loads LenovoLegionLinux's kernel
      # module, which fan curves and the extra switches need.
      nixosModules.default =
        {
          config,
          lib,
          pkgs,
          ...
        }:
        let
          cfg = config.programs.cohort;
        in
        {
          options.programs.cohort = {
            enable = lib.mkEnableOption "Cohort, settings for Lenovo Legion laptops";
            legionModule = lib.mkEnableOption "the legion_laptop kernel module from LenovoLegionLinux";
          };
          config = lib.mkIf cfg.enable {
            environment.systemPackages = [ self.packages.${pkgs.stdenv.hostPlatform.system}.default ];
            security.polkit.enable = true;
            boot.extraModulePackages = lib.mkIf cfg.legionModule [ config.boot.kernelPackages.lenovo-legion-module ];
            boot.kernelModules = lib.mkIf cfg.legionModule [ "legion_laptop" ];
            # Leaves the power mode to the mainline drivers and adds the
            # module's fan control beside them (packaging/legion_laptop.conf).
            boot.extraModprobeConfig = lib.mkIf cfg.legionModule (builtins.readFile ./packaging/legion_laptop.conf);
          };
        };
    };
}
