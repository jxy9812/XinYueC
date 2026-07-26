# XGui Qt Alignment Handoff

Last updated: 2026-07-27 00:01 Asia/Shanghai

## Scope

This handoff covers the partial Qt 6.8 alignment work in `Src/XGui` completed
on Windows. The comparison source was `D:\Qt\6.8.3\Src\qtbase`.

Do not assume the entire worktree belongs to this task. At handoff time it also
contains unrelated network/FTP and CMake changes. Limit review, staging, and
future commits to the XGui paths listed below unless the task is intentionally
expanded.

## Files Changed For This Task

```
Src/XGui/XBitmap.c
Src/XGui/XGuiTypes.c
Src/XGui/XIcon.c
Src/XGui/XImage.c
Src/XGui/XImageFormat.c
Src/XGui/XImageIOHandler.c
Src/XGui/XImageReader.c
Src/XGui/XImageWriter.c
Src/XGui/XPicture.c
Src/XGui/XPixmap.c
Src/XGui/XPixmap.h
Src/XGui/XPixmapCache.c
Test/XGuiTest/XPictureTest.c
Test/XGuiTest/XPixmapCacheTest.c
```

## Completed Work

### XImage and image formats

- Fixed writable `bits()` and `scanLine()` to detach shared data before exposing
  mutable pixels.
- Deep-copy palette and image metadata on detach; fixed palette mutations after
  COW.
- Added size, stride, and allocation overflow validation.
- Corrected storage depth for RGB444 (16) and RGB666 (24), and standardised
  owned image scanlines to 4-byte alignment.
- Made cache keys change after mutation/detach.
- Added guarded self-copy and self-move behavior.
- Added common format conversion paths, safe per-format `rgbSwapped`, and
  aspect-ratio-aware scaling. Smooth mode still uses nearest-neighbor sampling.
- Added BMP-only I/O: uncompressed `BI_RGB`, 24-bit BGR and 32-bit BGRA,
  including bottom-up/top-down input, 4-byte row padding, and bounds checks.
  `XImage_load`, `XImage_loadFromData`, and `XImage_save` are now functional
  for BMP.

### Pixmap, bitmap, and icon

- Added mask extraction/application, color mask generation, heuristic mask
  generation, scrolling with exposed regions, mutation cache-key updates, and
  null-safe `save` handling for `XPixmap`.
- Added internal `XPixmap_init_bitmap_image()` so `XBitmap` retains both its
  `XBitmap` vtable and `isQBitmap()` identity.
- Fixed byte-aligned Mono input handling in `XBitmap_fromData`.
- Improved icon resource matching, actual-size calculation, device-pixel-ratio
  output, and cache key invalidation after mutations.

### Picture and cache

- Fixed `XPicture_detach()` use-after-free and made `setData` and
  `setBoundingRect` COW-safe.
- Added raw binary `XPicture` load/save. This persists the XPicture data buffer;
  it is not a validated Qt QPIC implementation.
- Reworked `XPixmapCache` key lifecycle: new keys are invalid, all copies become
  invalid after removal/eviction/clear, and the old heap key-wrapper leak is
  removed.
- Replaced newest-first eviction with LRU (access promotes to front; eviction
  removes the tail), replace duplicate string keys, and use 64-bit accumulated
  cache cost.

### Reader, writer, and handler contracts

- Preserved format spelling instead of forcing uppercase.
- Corrected writer default compression to `-1`.
- Added allocation limit storage (default 256 MB), file signature detection for
  PNG/JPEG/BMP/GIF/WebP, mutually exclusive file/device setters, and explicit
  error results instead of false-positive `canRead`/`canWrite` values.
- No decoder/encoder registry was added. Reader/writer report a clear
  unsupported-format error when no handler is available.

## Verification Already Performed

The Windows command below completed successfully after the final changes:

```powershell
cmake --build build --config Debug --target XinYueC --parallel 4
```

Additional checks completed:

- `git diff --check -- Src/XGui Test/XGuiTest` (no whitespace errors; Git may
  print existing CRLF conversion notices).
- Native DLL smoke checks for image COW, mutable cache-key change, picture
  detach data preservation, and pixmap-cache key invalidation.
- The new menu regression tests for Picture COW/file round-trip and cache
  lifecycle/LRU were run successfully by the previous worker.

The BMP round-trip smoke script did not run: its PowerShell `UInt32` literal
conversion failed before any native call. No temporary test file was retained.

## Linux Continuation

Start with an isolated build directory and do not remove the existing Windows
`build` directory from a shared checkout:

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target XinYueC -j"$(nproc)"
git diff --check -- Src/XGui Test/XGuiTest XGui_Qt_Alignment_Handoff.md
```

If the complete project fails due to Windows-only drivers or unrelated network
work, first identify whether the failure is outside the XGui file list above.
The BMP implementation uses only ISO C file APIs; it requires no PNG/JPEG
development package. A future multi-format codec implementation should use the
project's existing dependency strategy rather than invoking Qt at runtime.

Suggested first Linux regression cases:

1. BMP 24-bit and 32-bit save/load/loadFromData round trips, including top-down
   BMP input and malformed offsets/strides.
2. Shared `XImage` writes through `bits()` and `scanLine()`; source and copy
   must diverge and cache keys must change.
3. Shared `XPicture` data followed by `detach`, `setData`, and file round trip.
4. Pixmap cache duplicate string replacement, LRU promotion, key invalidation,
   and negative cache limits.
5. Mono bitmap identity and Pixmap masks/scroll exposed-region behavior.

## Remaining Qt Gaps

This is still a partial compatibility layer, not QtGui parity. Highest-value
remaining work is:

1. Add real image handler discovery/registration and codec support beyond BMP;
   wire `XImageReader` and `XImageWriter` to those handlers and `XIODevice`.
2. Implement `XPixmap_transformed`, `XPixmap_fromImageReader`, and device I/O
   overloads. Validate heuristic-mask output against Qt fixtures.
3. Implement `XIcon_paint`, `availableSizes`, icon engines, theme lookup and
   fallback search paths.
4. Implement QPicture command recording/replay (`XPicture_play`) and a defined,
   validated serialization format. Do not claim Qt QPIC binary compatibility
   without test fixtures from Qt.
5. Add automated non-menu tests. Existing `Test/XGuiTest` code is guarded by
   `DEMOTEST` and historically printed expectations rather than asserting them.
6. Fill remaining QImage APIs: color-space APIs, text metadata, device-pixel
   ratio, color-table APIs, pixel color APIs, transforms, invertPixels, and the
   full QRegion/QRect/QSize/QPoint surface.

## Safe Git Review Commands

```bash
git status --short
git diff -- Src/XGui Test/XGuiTest XGui_Qt_Alignment_Handoff.md
git diff --check -- Src/XGui Test/XGuiTest XGui_Qt_Alignment_Handoff.md
```

Do not use a broad reset or checkout in this worktree: it contains unrelated
user changes outside XGui.
