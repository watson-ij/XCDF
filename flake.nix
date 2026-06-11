{
  description = "Development shell for XCDF";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        commonPackages = with pkgs; [
          cmake
          gcc
          git
          gnumake
          perf
          ninja
          pkg-config
          python3
          zlib
        ];
        setupBuildEnv = ''
          export CPATH="${pkgs.zlib.dev}/include''${CPATH:+:$CPATH}"
          export LIBRARY_PATH="${pkgs.zlib}/lib''${LIBRARY_PATH:+:$LIBRARY_PATH}"
          export LD_LIBRARY_PATH="${pkgs.zlib}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        '';
        configureAndBuild = ''
          ${setupBuildEnv}
          build_dir=''${XCDF_BUILD_DIR:-build-nix}
          build_type=''${CMAKE_BUILD_TYPE:-Release}
          cmake -S . -B "$build_dir" \
            -DBUILD_PYTHON=OFF \
            -DCMAKE_BUILD_TYPE="$build_type" \
            -G Ninja >&2
          cmake --build "$build_dir" >&2
        '';
        mkApp = name: text: {
          type = "app";
          program = "${pkgs.writeShellApplication {
            inherit name;
            runtimeInputs = commonPackages;
            inherit text;
          }}/bin/${name}";
        };
      in
      {
        devShells.default = pkgs.mkShell {
          packages = commonPackages;
        };

        apps = rec {
          default = test;

          build = mkApp "xcdf-build" ''
            ${setupBuildEnv}
            build_dir=''${XCDF_BUILD_DIR:-build-nix}
            build_type=''${CMAKE_BUILD_TYPE:-Release}
            cmake -S . -B "$build_dir" \
              -DBUILD_PYTHON=OFF \
              -DCMAKE_BUILD_TYPE="$build_type" \
              -G Ninja \
              "$@"
            cmake --build "$build_dir"
          '';

          test = mkApp "xcdf-test" ''
            ${configureAndBuild}
            ctest --test-dir "$build_dir" --output-on-failure "$@"
          '';

          perf = mkApp "xcdf-perf" ''
            ${configureAndBuild}
            "$build_dir"/xcdf-perf-test "$@"
          '';
        };
      });
}
