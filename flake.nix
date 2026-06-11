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
        defaultZlib = pkgs.zlib;
        zlibNgCompat = pkgs.zlib-ng.override { withZlibCompat = true; };
        commonPackagesFor = zlibProvider: with pkgs; [
          cmake
          gcc
          git
          gnumake
          perf
          ninja
          pkg-config
          python3
          zlibProvider
        ];
        setupBuildEnvFor = zlibProvider: ''
          export CPATH="${zlibProvider.dev}/include''${CPATH:+:$CPATH}"
          export LIBRARY_PATH="${zlibProvider}/lib''${LIBRARY_PATH:+:$LIBRARY_PATH}"
          export LD_LIBRARY_PATH="${zlibProvider}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        '';
        configureAndBuildFor = zlibProvider: defaultBuildDir: ''
          ${setupBuildEnvFor zlibProvider}
          build_dir=''${XCDF_BUILD_DIR:-${defaultBuildDir}}
          build_type=''${CMAKE_BUILD_TYPE:-Release}
          cmake -S . -B "$build_dir" \
            -DBUILD_PYTHON=OFF \
            -DCMAKE_BUILD_TYPE="$build_type" \
            -G Ninja >&2
          cmake --build "$build_dir" >&2
        '';
        buildAppTextFor = zlibProvider: defaultBuildDir: ''
          ${setupBuildEnvFor zlibProvider}
          build_dir=''${XCDF_BUILD_DIR:-${defaultBuildDir}}
          build_type=''${CMAKE_BUILD_TYPE:-Release}
          cmake -S . -B "$build_dir" \
            -DBUILD_PYTHON=OFF \
            -DCMAKE_BUILD_TYPE="$build_type" \
            -G Ninja \
            "$@"
          cmake --build "$build_dir"
        '';
        mkAppFor = zlibProvider: name: text: {
          type = "app";
          program = "${pkgs.writeShellApplication {
            inherit name;
            runtimeInputs = commonPackagesFor zlibProvider;
            inherit text;
          }}/bin/${name}";
        };
      in
      {
        devShells.default = pkgs.mkShell {
          packages = commonPackagesFor defaultZlib;
        };

        apps = rec {
          default = test;

          build = mkAppFor defaultZlib "xcdf-build"
            (buildAppTextFor defaultZlib "build-nix");

          test = mkAppFor defaultZlib "xcdf-test" ''
            ${configureAndBuildFor defaultZlib "build-nix"}
            ctest --test-dir "$build_dir" --output-on-failure "$@"
          '';

          perf = mkAppFor defaultZlib "xcdf-perf" ''
            ${configureAndBuildFor defaultZlib "build-nix"}
            "$build_dir"/xcdf-perf-test "$@"
          '';

          build-zlib-ng = mkAppFor zlibNgCompat "xcdf-build-zlib-ng"
            (buildAppTextFor zlibNgCompat "build-nix-zlib-ng");

          test-zlib-ng = mkAppFor zlibNgCompat "xcdf-test-zlib-ng" ''
            ${configureAndBuildFor zlibNgCompat "build-nix-zlib-ng"}
            ctest --test-dir "$build_dir" --output-on-failure "$@"
          '';

          perf-zlib-ng = mkAppFor zlibNgCompat "xcdf-perf-zlib-ng" ''
            ${configureAndBuildFor zlibNgCompat "build-nix-zlib-ng"}
            "$build_dir"/xcdf-perf-test "$@"
          '';
        };
      });
}
