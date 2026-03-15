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

Static dependencies:

- [ImGui (docking)](https://github.com/ocornut/imgui)
- [readerwriterqueue](https://github.com/cameron314/readerwriterqueue)

Runtime dependencies:

- `ffmpeg` (the actual decoder)
- `pipewire` (for audio output. Sorry, no other option.)
- `sdl3` (for Wayland support and GUI)
- `systemd-libs` (for MPRIS)

Build dependencies:

- `base-devel`
- `cmake`
- `git`
- `pkgconf`

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
