# XGui Qt Alignment Handoff

Last updated: 2026-07-27 21:00 Asia/Shanghai

## Scope

This handoff covers the partial Qt 6.8 alignment work in `Src/XGui` completed
on Windows. The current comparison source is
`D:/Qt/6.8.3/Src/qtbase` (use the corresponding Qt source path on Linux).
Shared XGui code must remain portable and
must not call platform GUI APIs.

Do not assume the entire worktree belongs to this task. At handoff time it also
contains unrelated network/FTP and CMake changes. Limit review, staging, and
future commits to the XGui paths listed below unless the task is intentionally
expanded.

## Files Changed For This Task

```
Src/XGui/XBitmap.c
Src/XData/XGeometry.h
Src/XData/XGeometry.c
Src/XGui/XIcon.c
Src/XGui/XImage.h
Src/XGui/XImage.c
Src/XGui/XImageFormat.c
Src/XGui/XImageIOHandler.c
Src/XGui/XImageReader.c
Src/XGui/XImageWriter.c
Src/XGui/XPicture.c
Src/XGui/XPicture.h
Src/XGui/XPixmap.c
Src/XGui/XPixmap.h
Src/XGui/XPixmapCache.c
Test/XGuiTest/XPictureTest.c
Test/XGuiTest/XPixmapCacheTest.c
xgui_regression_test.c
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
- Added format-aware conversion and pixel access for every declared
  `XImageFormat`, including packed Mono, Indexed8, 16/24/30/32/64/128-bit,
  floating-point, grayscale, CMYK, and premultiplied formats.
- Added aspect-ratio-aware scaling with fast nearest-neighbor and smooth
  bilinear sampling; output aliasing is handled through temporary images.
- Added BMP-only I/O: uncompressed `BI_RGB`, 24-bit BGR and 32-bit BGRA,
  including bottom-up/top-down input, 4-byte row padding, and bounds checks.
  `XImage_load`, `XImage_loadFromData`, and `XImage_save` are now functional
  for BMP.
- Added Qt-style pixel access for Mono/MonoLSB, Indexed8, RGB16/RGB555,
  RGB/BGR888, RGB/ARGB/RGBA8888, Alpha8, and grayscale formats, including
  palette-aware `pixel()`/`setPixel()` and channel-correct fill behavior.
- Added `XImage_invertPixels()` and `devicePixelRatio()`/`setDevicePixelRatio()`;
  metadata setters now update the image cache key and COW preserves the DPR.
- Reworked mirrored and RGB-swapped operations to use format-aware pixel
  access, preserving palettes, metadata, packed Mono data, and RGBA byte order.
- Preserved unpremultiplied public `pixel()` values for premultiplied formats;
  conversion paths perform premultiplication exactly once.

### Pixmap, bitmap, and icon

- Added mask extraction/application, color mask generation, heuristic mask
  generation, scrolling with exposed regions, mutation cache-key updates, and
  null-safe `save` handling for `XPixmap`.
- Added internal `XPixmap_init_bitmap_image()` so `XBitmap` retains both its
  `XBitmap` vtable and `isQBitmap()` identity.
- Fixed byte-aligned Mono input handling in `XBitmap_fromData`.
- Improved icon resource matching, actual-size calculation, device-pixel-ratio
  output, and cache key invalidation after mutations.
- Aligned icon best-match fallback order with Qt mode/state fallback rules,
  area-based source selection, and effective DPR calculation for low-resolution
  fallbacks. Theme engines and painting remain unavailable.
- Expanded portable QPoint/QSize/QRect/QRegion helpers, including normalized
  intersection/union/subtraction operations, alias-safe outputs, saturated
  coordinate arithmetic, and region capacity/empty-rectangle checks.
- Hardened XPixmap scroll clipping and exposed-region replacement against
  integer overflow; scaled/transformed/copyRect preserve DPR and bitmap identity
  when output aliases the source. setMask now converts non-alpha images to an
  alpha-capable format before applying the mask, and XBitmap transforms retain
  XBitmap vtable identity. Added portable `convertFromImage()` and `swap()`.

### Picture and cache

- Fixed `XPicture_detach()` use-after-free and made `setData` and
  `setBoundingRect` COW-safe.
- Added a portable XPIC v1 command stream with fixed little-endian header,
  opcode/payload lengths, FNV checksum, strict validation, and COW-safe
  recording. It records lines, filled rectangles, save/restore, and
  self-contained XImage payloads including format, stride, palette, DPR, DPM,
  and offset.
- Added the pure C `XPainter` callback interface and complete dispatch in
  `XPicture_play()`. Empty recordings remain successful no-ops.
- `XPicture_load/save` validate XPIC streams when the magic is present; raw
  non-XPIC data remains accepted by `setData` for compatibility. XPIC is a
  project format and is intentionally not claimed to be Qt QPIC compatible.
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
- Reader/writer now use `XIODevice` for BMP device reads and writes.
- Added a portable static discovery registry for the built-in BMP codec:
  supported format/MIME lists are owned `XVector<const char*>` values, MIME
  lookup is case-insensitive, and unknown MIME types return an empty list.
  The built-in codec remains BMP-only; dynamic plugin discovery and other
  decoders/encoders are still not implemented.

### Continuation Fixes

- Added portable affine `XPixmap_transformed()`,
  `XPixmap_fromImageReader()`, and safe output replacement for scaled and
  cropped pixmap APIs.
- Added `XIcon_availableSizes()` using an initialized `XVector` of
  `XSize` values, with duplicate dimensions removed.
- Normalized copy/move/deinit virtual dispatch in XImage, XPixmap, XBitmap,
  XIcon, and XPicture; copy/move virtuals initialize an uninitialized
  destination before taking ownership.

## Verification Already Performed

The Windows command below completed successfully after the final changes:

```powershell
cmake --build build --config Debug --target XinYueC --parallel 4
```

The current Debug build also completed successfully for `XinYueC` and
`XGuiRegression_Test`; the executable printed `XGui regression tests passed`.

Additional checks completed:

- `git diff --check -- Src/XGui Test/XGuiTest` (no whitespace errors; Git may
  print existing CRLF conversion notices).
- Native DLL smoke checks for image COW, mutable cache-key change, picture
  detach data preservation, and pixmap-cache key invalidation.
- The new menu regression tests for Picture COW/file round-trip and cache
  lifecycle/LRU were run successfully by the previous worker.

The current `XGuiRegression_Test` includes BMP save/load and device round-trip
assertions, the BMP reader/writer registry, icon fallback/size/DPR matching,
XImage pixel/palette/alpha/DPR/invert and premultiplied conversion behavior,
geometry operations, scroll exposed regions, RGB mask conversion, bitmap
identity, aliased transform outputs, XPainter command dispatch, image payload
replay, and XPIC stream validation.

The verified checkout is Windows-only at this handoff; Linux must rerun the
commands below after syncing the current XGui changes.

## Linux Continuation

For a fresh isolated build, do not remove the existing Windows `build`
directory from a shared checkout:

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target XinYueCS XGuiRegression_Test -j"$(nproc)"
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

Continuation update (2026-07-27):

- `XIcon_addFile()` now decodes the source first and then stores the requested
  positive width/height raster, matching `QIcon::addFile` resource sizing.
- Added a regression fixture that verifies an `8x6` requested resource is
  reported by `XIcon_availableSizes()`.
- Rebuilt `XinYueCS` and `XGuiRegression_Test` in the isolated `build-linux`
  directory (the checkout is Windows/MSVC despite the directory name).
- Qt 6.8 `QImage::invertPixels()` explicitly complements 1/8-bit pixel indices
  and does **not** modify the color table. Keep this as the Linux comparison
  oracle for Indexed8; an Indexed8 test must check the complemented index and
  out-of-range palette behavior rather than expecting an inverted palette.
- Mono/MonoLSB conversion now preserves packed source bits even when the
  source has no color table; this keeps `XBitmap_fromImage()` masks intact.
- `XRegion_addRect()` now performs transitive merging of compatible adjacent
  rectangles. The regression suite covers chained vertical merges.
- Final Windows verification after these fixes:
  `cmake --build build --config Debug --target XinYueC --parallel 1`,
  `cmake --build build --config Debug --target XGuiRegression_Test --parallel 1`,
  and `bin/Debug/XGuiRegression_Test.exe` all completed successfully.
- Lightweight geometry value APIs now return structs directly: `XPoint` add/
  subtract, `XSize` bounded/expanded/transposed, and pure `XRect` queries and
  operations. `XRegion` operations keep output parameters because regions own
  dynamic rectangle storage.

Memory/API continuation update (2026-07-27):

- Added `XMemory_strdup()` to `Src/XMemory/XMemory.h/.c`; it allocates with
  `XMalloc_System()` and must be paired with `XFree_System()`.
- Replaced `strdup`/`XStrdup` use in URL, CAN parser/bus, XDir, and the Windows
  file driver. The obsolete `XStrdup` declaration and implementation were
  removed from `XString.h/.c`.
- `XGeometry.c` region storage now uses `XRealloc_System()` and
  `XFree_System()`. `XClass_delete_base()` still honors custom `FreeMethod`
  callbacks, but no longer contains a native `free()` call.
- The embedded xxHash allocation hooks now use `XMalloc_Hybrid()` and
  `XFree_Hybrid()`; no native allocation calls remain in actual `Src` code.
- A Python lexer scan over every file below `Src` reports
  `UNWRAPPED_CALLS=0` after ignoring comments, literals, and member callbacks.
- Windows verification passed serially:
  `cmake --build build --config Debug --target XinYueCS --parallel 1`,
  `cmake --build build --config Debug --target XGuiRegression_Test --parallel 1`,
  and `bin/Debug/XGuiRegression_Test.exe`.

## Remaining Qt Gaps

This is still a partial compatibility layer, not QtGui parity. Highest-value
remaining work is:

1. Add real image handler discovery/registration and codec support beyond BMP;
   the current reader/writer device path is intentionally BMP-only.
2. Implement `XIcon_paint`, icon engines, theme lookup and
   fallback search paths.
3. Extend XPainter opcodes only when a matching portable callback is defined;
   the current XPIC stream is not Qt QPIC binary compatible.
4. Expand `xgui_regression_test.c` and add Qt fixture comparisons for
   malformed BMP input, masks, cache lifecycle, and picture serialization.
   Existing `Test/XGuiTest` code is guarded by `DEMOTEST` and historically
   printed expectations rather than asserting them.
5. Fill remaining QImage APIs: color-space APIs, text metadata, and other Qt
   extensions not represented by the current C ABI. The common color-table,
   pixel color, DPR, invert, transform, and geometry paths now have coverage.

## Safe Git Review Commands

```bash
git status --short
git diff -- Src/XGui Test/XGuiTest XGui_Qt_Alignment_Handoff.md
git diff --check -- Src/XGui Test/XGuiTest XGui_Qt_Alignment_Handoff.md
```

Do not use a broad reset or checkout in this worktree: it contains unrelated
user changes outside XGui.
