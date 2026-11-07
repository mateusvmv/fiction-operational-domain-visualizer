# SiDB Operational Domain Visualizer

A native desktop application for configuring, running, and comparing operational-domain experiments for SiDB gates.

The application presents computed data in terms of relative permittivity (`epsilon_r`) and Thomas–Fermi screening length (`lambda_tf`). Each experiment configuration selects a gate file, an algorithm, a chemical-potential value (`mu`, displayed in eV), and a starting point. Results can be inspected in tables and plots and exported to CSV.

## Features

- Configure multiple experiments independently.
- Search configured directories for gate files. The default search directory in the application state is `fiction/experiments/sidb_gate_libraries/siqad_gates` (relative to the working directory).
- Select an algorithm and experiment parameters, including `mu`, `epsilon_r`, and `lambda_tf` starting values.
- Enable or disable individual experiments for plotting.
- Plot operational and non-operational points in the `(epsilon_r, lambda_tf)` parameter plane.
- Compare the number of calculated points and algorithm runtime across experiments.
- Inspect an experiment's summary and export its data as CSV.
- Save application state in `app_state.json`; cached operational data is reconstructed when a saved state is loaded.

The plot code recognizes these algorithm names:

- Contour Tracing (`CT`)
- Grid Search (`GS`)
- Flood Fill (`FF`)
- Random Sampling (`RS`)
- Radial Search (`RadS`)

The available algorithm implementations and their precise behavior are defined by the computation code, not by the plotting layer.

## Requirements

The project uses CMake and Ninja to build a C++ desktop application. The supplied `build.sh` declares the following environment packages:

- C and C++ compilers (GCC in the supplied environment)
- CMake and Ninja
- Python 3
- X11 development libraries: X11, XRandR, Xinerama, XCursor, and XInput (`Xi`)
- OpenGL development libraries

The application sources include raylib, Dear ImGui, ImPlot, rlImGui, and fmt. The provided build scripts do not show whether these C++ libraries are vendored, fetched by CMake, or expected to be installed separately. Check `CMakeLists.txt` and the project's dependency files if CMake reports that one of them is missing. The package commands below install the system toolchain and graphics development dependencies listed by the supplied build environment; they cannot install an unknown project-specific dependency automatically.

## Install dependencies

Choose the command for your distribution. These commands install the compiler toolchain, CMake, Ninja, Python 3, and the X11/OpenGL development packages used by the supplied Nix build environment.

### Ubuntu, Debian, and Linux Mint

```sh
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build python3 \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libgl1-mesa-dev
```

### Arch Linux, Manjaro, and EndeavourOS

```sh
sudo pacman -Syu --needed \
  base-devel cmake ninja python \
  libx11 libxrandr libxinerama libxcursor libxi mesa
```

`yay` users can use the same package names:

```sh
yay -S --needed \
  base-devel cmake ninja python \
  libx11 libxrandr libxinerama libxcursor libxi mesa
```

These packages are available in the standard Arch repositories, so `pacman` is sufficient; an AUR helper is not required.

### Fedora

```sh
sudo dnf install -y \
  gcc gcc-c++ cmake ninja-build python3 \
  libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel \
  libXi-devel mesa-libGL-devel
```

### openSUSE

```sh
sudo zypper install -y \
  gcc gcc-c++ cmake ninja python3 \
  libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel \
  libXi-devel Mesa-libGL-devel
```

### Optional debugger tools

The normal application build does not require either debugger. Install these only if you intend to use the corresponding launcher option:

- `gdb` for `./target/build gdb`.
- `gf2` for `./target/build gf`. The Nix package named `gf` provides the `gf2` executable; availability and packaging differ by distribution.

## Build on Linux

### 1. Compile the build launcher

`build.c` is a small C program that invokes CMake and Ninja. It is not tied to Nix or to a particular Linux distribution. After installing the dependencies above, compile it with the system C compiler:

```sh
mkdir -p target
cc -g build.c -o target/build
```

You only need to repeat this step when `build.c` changes (or when you delete `target/build`).

### 2. Configure and build the application

Run the launcher from the repository root:

```sh
# Release build (default)
./target/build

# Debug build
./target/build debug
```

The launcher configures CMake with the Ninja generator and builds into `target/release` or `target/debug`.

### 3. Run the application

```sh
# Build Release and launch the application
./target/build run

# Build Debug and launch the application
./target/build debug run
```

The executable is expected at `target/release/tester` or `target/debug/tester`.

### 4. Run with a debugger (optional)

```sh
# Build Debug and launch GDB
./target/build gdb

# Build Debug and launch gf2
./target/build gf
```

The `gdb` and `gf` options automatically select the Debug build. The `gf` option invokes a command named `gf2`.

## About `build.sh` and Nix

`build.sh` is a convenience wrapper for users with Nix installed. Its shebang uses `nix-shell` to provide the declared compiler, CMake, Ninja, Python, and graphics dependencies, then it compiles `build.c` if needed and runs the resulting launcher.

On other distributions, you do **not** need Nix and you do **not** need to use `build.sh`. Install the dependencies using the commands above, then run the manual `cc` and `./target/build` commands. The launcher accepts the same arguments either way:

```text
./target/build [release|debug] [run|gdb|gf]
```

## Build output and logs

- Release build directory: `target/release/`
- Debug build directory: `target/debug/`
- Build helper executable: `target/build`
- Expected application executable: `target/release/tester` or `target/debug/tester`
- Failed build log: `target/failed_compilation.txt`

The helper streams compiler output to the terminal and writes it to `target/failed_compilation.txt`. The log is removed after a successful build and retained when the build fails.

## Using the application

1. Start the application from the repository root so relative paths, including the default gate search directory, resolve as expected.
2. Configure the directories used to discover gate files and select the gates to include.
3. Add experiment configurations. Each configuration specifies a gate file, algorithm, `mu`, and starting `(epsilon_r, lambda_tf)` point. The UI also stores an enabled flag and a plot color for each experiment.
4. Run or load the experiments and inspect the results window and plot window.
5. Use the plot tabs to compare operational points, non-operational points, calculated-point totals, and runtime.
6. In the results window, select an experiment and use the CSV controls to save its records.

The CSV export currently writes this header:

```csv
epsilon_r,lambda_tf,status,mu
```

In the plot and summary code, status `1` is treated as operational and status `0` as non-operational.

## Saved state and cache

The application reads and writes `app_state.json` in the working directory. The saved state contains the experiment configuration and other application settings. Operational data is held in an in-memory cache and rebuilt from the saved experiments when a compatible saved state is loaded.

The cache is keyed by gate path, algorithm, `mu`, and the exact starting coordinates. Configurations with different starting coordinates are kept distinct; configurations with identical values for all of those fields share the same cached computation. If identical Random Sampling configurations are intended to represent independent runs, that behavior would need a separate persistent run/experiment identifier and corresponding state-serialization changes.

## Troubleshooting

### `cc`, `gcc`, or `g++` is not found

Install the compiler toolchain package for your distribution (`build-essential`, `base-devel`, or the equivalent). Then retry `cc -g build.c -o target/build`.

### CMake cannot find Ninja

Install the Ninja package for your distribution and rerun the build. The launcher explicitly selects `-G Ninja`.

### CMake cannot find X11 or OpenGL

Install the development packages, not just the runtime libraries. On Debian/Ubuntu these are the `*-dev` packages; on Fedora and openSUSE they are generally `*-devel` packages.

### CMake cannot find raylib, ImGui, ImPlot, rlImGui, or fmt

These libraries are referenced by the application source, but their acquisition method is not visible in the supplied build scripts. Check `CMakeLists.txt` and any dependency-management files to determine whether they should be fetched, built from vendored sources, or installed separately.

### The application cannot find gate files

Run it from the repository root or update the configured gate-search directories. The default path is relative to the current working directory.

### A build fails

Inspect `target/failed_compilation.txt` for the captured compiler output. For more detailed CMake output, configure and build directly using the commands in [Build on Linux](#build-on-linux), or run the CMake configure/build commands manually:

```sh
cmake -S . -B target/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build target/release --parallel
```

## Platform scope

The supplied scripts and dependency list describe a Linux desktop build using X11 and OpenGL. This README documents Linux distributions; it does not claim native Windows or macOS build support.

## References

- [raylib Linux build dependencies](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)
- [Arch Linux package database](https://archlinux.org/packages/)
- [Fedora package database](https://packages.fedoraproject.org/)
- [openSUSE software search](https://software.opensuse.org/)
