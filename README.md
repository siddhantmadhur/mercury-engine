# Mercury

Mercury is a game-engine built with C++ for performant games

<img width="854" height="508" alt="mercury_engine_example" src="https://github.com/user-attachments/assets/dc234997-66ed-462a-9bb8-d86c7bafa2c5">

## Table of Contents

- [Goal](#goal)
- [Support](#support)
- [Building](#building)
- [Issues](#issues)
- [Contributing](#contributing)
- [Documentation](#documentation)
- [LICENSE](#license)

## Goal

A completely open-source program that is designed to be develop performant video-games. The primary goal
is to develop simulator video-games like Railroad Tycoon, SimCity, Cities Skylines etc. This means that it
is developed specifically for top-down games with lots of systems being simulated, and features like ray-tracing,
or advanced graphics might never be supported.

## Support

A goal of this project was to be cross-platform. Since everything is being implemented
myself it is difficult to and thus I'm only aiming for desktop platforms with a limited number
of graphics backends.

The order of the platform is the order of importance for compatibility.
For example, Windows has the most support and hence is the first on the list.
If a platform is not on the list then that means supporting is not a goal of this project.

| Platform      | Renderer | Support |
|---------------|----------|---------|
| Windows 10/11 | Vulkan   | Yes     |
| Linux         | Vulkan   | Yes     |
| MacOS         | MoltenVK | Yes     |
| SteamOS       | Vulkan   | No [^1] |


Rendering requires a GPU and driver with Vulkan 1.3 (dynamic rendering). Shaders are
written in HLSL and compiled to SPIR-V with DXC at build time.

[^1]: The goal is to have SteamOS specific features like support for those touchpads on the
SteamDeck or the controllers. Along with support for controller layouts, battery modes and more.

## Building

Game scripts are TypeScript, run by an embedded V8. V8 is fetched once with depot_tools and is
built by Bazel from V8's own Bazel files. esbuild bundles the scripts.

The build requires [Bazelisk](https://github.com/bazelbuild/bazelisk); the repository pins Bazel
in `.bazelversion`. CMake and Ninja are still used, but only to build the third-party dependencies
through `rules_foreign_cc`.

### macOS (Apple Silicon)

```sh
./scripts/setup_project_macos.sh        # Homebrew: Bazelisk, CMake, Ninja, MoltenVK, Vulkan, DXC
bazelisk build //mercury:mercury_player
bazelisk test //tests/...
bazelisk run //mercury:mercury_player -- examples/pong/pong.mproj
```

V8's first Bazel build takes 30-60 minutes. If V8's Bazel build cannot be used on your Xcode/host
combination, build the GN fallback once and select it:

```sh
./scripts/build_v8.sh
bazelisk build //mercury:mercury_player --define=v8_backend=gn
```

### Linux (X11 and Wayland)

```sh
./scripts/setup_project_linux.sh        # apt or pacman; Vulkan, GLFW deps, DXC, esbuild, Bazelisk
bazelisk build //mercury:mercury_player
bazelisk test //tests/...
bazelisk run //mercury:mercury_player -- examples/pong/pong.mproj
```

`scripts/fetch_v8.sh` (see `third_party/v8/README.md`) fetches the version in
`third_party/v8/VERSION`. Node and `typescript` are optional: when `tsc` is installed, Build also
type-checks the scripts.

Debian/Ubuntu don't package DXC, so the setup script downloads the official release into
`bin/dxc/`. If you use the [LunarG Vulkan SDK](https://vulkan.lunarg.com) instead, set
`VULKAN_SDK` and its `dxc` is used.

Debug builds enable `VK_LAYER_KHRONOS_validation` when it is installed.

### Windows

Install the [LunarG Vulkan SDK](https://vulkan.lunarg.com) (loader, validation layers and DXC, and
it sets `VULKAN_SDK`), Bazelisk, CMake and Ninja. Fetch V8 with `scripts/fetch_v8.sh` (for example
from Git Bash) or use the GN fallback via `scripts/build_v8.ps1` and `--define=v8_backend=gn`.
Windows Bazel support is not validated yet; CMake and Ninja are only needed to build the
dependencies through `rules_foreign_cc`.

## Issues

Any bugs related to the game engine can be reported at [issues.reocelot.com](https://issues.reocelot.com)

## Contributing

Mercury is open-source but not open-contribution. Features part of this engine
are specifically for games **I** want to make and not to be generalized.
In fact there are much better engines already out there that do most things
better so I'd recommend using that if you cannot find a feature here.

That being said, there are many instances where you might want a change urgently (bugs, vulnerabilities)
and perhaps already fixed it, in which case email [smadhur@reocelot.com](mailto:smadhur@reocelot.com) with
more information or with
a [git patch](https://git-scm.com/book/en/v2/Distributed-Git-Contributing-to-a-Project#_project_over_email).

Official repositories for the code:

- https://github.com/siddhantmadhur/mercury-engine [main]

## Documentation

How the engine works (application structure, projects and the editor, file formats, components,
rendering and lighting, scripting, UI Toolkit, shipping and testing) is in
[DOCUMENTATION.md](DOCUMENTATION.md). Planned work is in [GOAL.md](GOAL.md).

## License

[GNU GPLv3](/LICENSE)
