# Third-Party Notices

Impulse is licensed under the GNU General Public License v3.0 only.

This project depends on source code from the following third-party components:

- Dear ImGui (`docking` branch): MIT License
  Copyright (c) 2014-2026 Omar Cornut
- readerwriterqueue: Simplified BSD License
  Copyright (c) 2013-2021 Cameron Desrochers
- doctest: MIT License
  Copyright (c) 2016-2023 Viktor Kirilov
- libvgm: zlib License
  Copyright (c) ValleyBell and contributors
- libopenmpt and openmpt123: BSD-3-Clause
  Copyright (c) 2004-2026, OpenMPT Project Developers and Contributors
  Copyright (c) 1997-2003, Olivier Lapicque
- sc68 / file68 / unice68: GPL-3.0-or-later
  Copyright (c) 1998-2016 Benjamin Gerard
  Vendored from `sc68-code-r713` (SourceForge)
  Local integration changes include CMake build glue, generated `include/sc68/trap68.h`,
  local config headers required for in-tree builds, and a fail-soft patch in
  `third_party/sc68/libsc68/io68/mfpemul.c` to avoid aborting Impulse on an
  unsupported MFP timer mode.
  Original upstream copyright and license notices are preserved in the vendored
  source files.
- ASAP (Another Slight Atari Player): GPL-2.0-or-later
  Copyright (c) 2005-2026 Piotr Fusik
  Vendored from ASAP source code as a minimal subset for
  SAP playback support. Vendored files: `asap.c`, `asap.h`, `asap-stdio.c`,
  `asap-stdio.h`, and `COPYING`. The vendored version is identified in
  `asap.h` as `8.0.0`.
  Local integration changes include CMake build glue only.
  Original upstream copyright and license notices are preserved in the vendored
  source files.

Impulse also links against system-provided libraries including SDL3, FFmpeg,
libopenmpt, PipeWire, and systemd/libsystemd. These libraries are not
redistributed in this repository and remain under their respective upstream and
distribution licenses.
