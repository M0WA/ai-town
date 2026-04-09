# AI Town

A 3D city simulator built with C++ and the Irrlicht engine. Zone land, build roads,
manage finances, and grow a living city with traffic, economy simulation, and spatial audio.

![AI Town screenshot](docs/screenshot-v0.0.12.png)

---

| main                                                                        | develop                                                                                       | image                                                                                            | sonarcloud                                                                                                                                                                  |
| --------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ![main](https://github.com/M0WA/ai-town/actions/workflows/ci.yml/badge.svg) | ![develop](https://github.com/M0WA/ai-town/actions/workflows/ci.yml/badge.svg?branch=develop) | ![docker ci cd](https://github.com/M0WA/ai-town/actions/workflows/docker-ci-image.yml/badge.svg) | [![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=M0WA_ai-town&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=M0WA_ai-town) |

---

## Installation

Pre-built installer/packages are available on the [Releases page](https://github.com/M0WA/ai-town/releases).

- **Linux**: download the `.deb` package for your distro (Debian Bookworm/Trixie, Ubuntu Jammy/Noble) and install with `sudo dpkg -i aitown-*.deb`
- **Windows**: download and run the `.exe` installer

---

## Default keybindings

### Tools

| Key | Action                 |
| --- | ---------------------- |
| `Z` | Zone tool              |
| `R` | Road tool              |
| `U` | Utilities tool         |
| `D` | Demolish tool          |
| `I` | Inspector / Query tool |

### UI

| Key       | Action                              |
| --------- | ----------------------------------- |
| `T`       | Toggle Finances panel               |
| `B`       | Toggle Notification log             |
| `Space`   | Pause / unpause simulation          |
| `+` / `=` | Increase simulation speed           |
| `-`       | Decrease simulation speed           |
| `Escape`  | Pause menu (in-game) / Back (menus) |
| `Ctrl+S`  | Save game                           |
| `Ctrl+Z`  | Undo                                |

### Camera

| Key / Mouse       | Action         |
| ----------------- | -------------- |
| Arrow keys        | Pan camera     |
| Scroll wheel      | Zoom in / out  |
| Right-mouse drag  | Rotate / pitch |
| Middle-mouse drag | Pan camera     |

Keys can be rebound in **Settings > Controls**. WASD camera pan is available as a preset.

---

## Build from source

**Recommended: Dev Container**

The canonical development environment is the dev container defined in `.devcontainer/`.
Open the repo in VS Code with the Dev Containers extension (or any compatible tool) and all
dependencies — GCC 13, CMake, Ninja, vcpkg, and all ports — are pre-installed.

**Prerequisites** (if not using the dev container)

| Tool         | Linux                        | Windows                      |
| ------------ | ---------------------------- | ---------------------------- |
| C++ compiler | GCC 13 or Clang 16           | MSVC 2022                    |
| CMake        | ≥ 3.21                       | ≥ 3.21                       |
| vcpkg        | [vcpkg.io](https://vcpkg.io) | [vcpkg.io](https://vcpkg.io) |
| Ninja        | `apt install ninja-build`    | included with VS 2022        |

**Linux**

```bash
git clone https://github.com/M0WA/aitown.git
cd aitown
make build        # configures + compiles (requires VCPKG_ROOT to be set)
./build/aitown
```

Run `make test` to build with coverage and execute the full test suite.
See the `Makefile` at the repo root for all available targets.

**Windows**

```powershell
git clone https://github.com/M0WA/aitown.git
cd aitown
cmake -B build -S . -G "Visual Studio 17 2022" `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
.\build\aitown.exe
```

---

## License

See [LICENSE.md](LICENSE.md) for license details.
