# Impulse

A technical Linux-native desktop music player.

## Reason

I just want a player that plays music and gives me what I need.

`mpd` with `ncmpcpp` is... well, not really comfortable since I use KDE Plasma and not a minimal WM.

I tried Strawberry but it's kinda... well, I mean it works fine and does its job well but when playing files in the playlist that are in different sample rates, once the sample rate changes, it kinda glitches out and stuff.

Ideally, I'd love to use Foobar2000 on Linux but I don't want to install Wine just to run Foobar2000 (yes, it's a me-problem).

So yeah. The fastest way to do this is create a new player.

## Screenshots

![main](/screenshot.png)

## Dependencies

The lists below describe native builds from source.

If you are using the Flatpak build, these libraries are provided by the Flatpak runtime and bundled modules from the Flatpak build, so you do not need to install them as host distro packages just to run the app.

Bundled third-party code:

- [ImGui (docking)](https://github.com/ocornut/imgui)
- [readerwriterqueue](https://github.com/cameron314/readerwriterqueue)
- [libvgm](https://github.com/ValleyBell/libvgm) (for VGM / VGZ playback)
- `ASAP` (vendored in `third_party/asap`, for SAP playback)
- `libsc68` (vendored in `third_party/sc68`, for SC68 / SNDH playback)

System libraries required for native builds:

- `libopenmpt` (for tracker / module playback)
- `ffmpeg` (`libavformat`, `libavcodec`, `libavutil`, `libswresample`, `libswscale`) for fallback decoding and metadata
- `pipewire` (`libpipewire-0.3`) for audio output. Sorry, no other option.
- `sdl3` for the GUI and desktop integration
- `libsystemd` (for MPRIS)
- `zlib` (required by the vendored `libsc68` backend)

Build tools:

- a C and C++ toolchain with C++23 support
- `cmake`
- `git` (used by CMake `FetchContent` for bundled dependencies)
- `pkg-config`

Test-only dependency:

- [doctest](https://github.com/doctest/doctest) (fetched automatically when `BUILD_TESTS=ON`)

Flatpak packaging tools:

- `flatpak`
- `flatpak-builder`

## Building

Local debug:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build -j"$(nproc)"
```

Release:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build-release -j"$(nproc)"
ctest --test-dir build-release --output-on-failure
```

## License

Licensed under the GNU General Public License v3.0 only.
