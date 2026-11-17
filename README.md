# FLAC / MP3 / AIFF Tag Editor

A native C++ recreation of the original MP3 Tag Editor UI, expanded to
read and write metadata across three formats: **FLAC** (Vorbis comments),
**MP3** (ID3v2 frames), and **AIFF** (ID3v2 embedded chunk) — all via
TagLib, all with embedded cover art support.

Runs on **Windows** (native Win32 UI) and **Linux** (GTK 3 UI). Both front
ends share the same TagLib tag-reading/writing code and behave the same.

## Download

Pre-built binaries are on the [Releases](../../releases) page:

- **Windows x64** — `FlacTagEditor.exe`, statically linked (no DLLs needed).
  Copy it anywhere and run it.
- **Linux x86_64** — `FlacTagEditor-<version>-linux-x86_64.tar.gz`. TagLib is
  statically linked; needs GTK 3 and glibc 2.38+ (Ubuntu 24.04+, Debian 13+,
  Fedora 39+, Arch, …). On Debian/Ubuntu: `sudo apt install libgtk-3-0`.
  Optionally pass a folder to open on startup: `./FlacTagEditor ~/Music`.

Everything below is for building it yourself.

## What it does

- **Select Folder** opens a folder picker and lists matching audio files
  inside.
- **FLAC / MP3 / AIFF checkboxes** above the file list toggle which formats
  are scanned and shown — uncheck a format to hide it from the list without
  changing folders. `.aif` is treated as AIFF too.
- **Filter box** narrows the (checkbox-filtered) list live by filename
  substring.
- Selecting a file loads its tags into the fields on the right: Filename,
  Artist, Genre, Title, Year/Date, Album, Key, Composer, BPM, Comment — plus
  the embedded cover art thumbnail and read-only audio info (sample rate,
  bit depth where applicable, channels, bitrate, duration).
- **Filename**, **Key**, **Composer**, and **BPM** aren't part of every
  format's standard tag interface, so they're mapped to each format's
  native custom/extended fields rather than bolted on:
  - FLAC: Vorbis comment fields `KEY`, `COMPOSER`, `BPM` (free-form).
  - MP3 / AIFF: standard ID3v2 frames `TKEY`, `TCOM`, `TBPM`.
  Editing Filename and saving renames the file on disk, and the file list
  updates immediately to show the new name.
- **Year/Date** writes free text — FLAC's Vorbis `DATE` field, or ID3v2's
  `TDRC` frame for MP3/AIFF — so it accepts either a plain year (`2024`) or
  a full ISO date (`2024-03-15`).
- **Set Cover... / Remove Cover** replace or clear the embedded front-cover
  picture (FLAC `PICTURE` block or ID3v2 `APIC` frame, depending on
  format). The change is held in memory and written on the next save.
- **Nothing saves automatically.** Clicking a different file in the list
  just loads it — any unsaved edits in the fields are discarded, no prompt.
  Four explicit actions write to disk:
  - **Save** — writes the current field values (and rename, and cover
    change) to the file(s) currently selected in the list.
  - **◀ Previous / Next ▶** — save the file you're currently looking at,
    then move to and load the adjacent file in the list. A quick way to
    work through a folder track by track without touching the mouse.
  - **Cancel** — discards in-progress edits and reloads the last-saved
    values from disk (does *not* write anything).
- **Double-click a file** in the list to open it in your system's default
  media player (whatever's associated with that file type on your
  machine), for a quick listen while tagging. Loading the file into the
  fields still happens as normal on the same click.
- **Refresh button (or F5)** re-scans the current folder from disk — picks
  up files added, removed, or renamed outside the app — without resetting
  the filter text, and keeps your current single-file selection loaded if
  that file is still present.
- **Ctrl+A** selects all text in whichever field has focus, or selects
  every file in the list if focus is on the file list (handy for jumping
  into batch mode across a whole folder).
- **Batch tagging**: select multiple files in the list (Ctrl/Shift-click),
  even a mix of formats. The fields clear, and any field you type into —
  plus a cover image, if you set one — gets applied to every selected file
  when you click **Save**. Fields left blank in batch mode are left
  untouched (blank does *not* clear the field, unlike single-file mode,
  where a blank field is written as empty). Previous/Next are disabled in
  batch mode since there's no single "current" file to step from.
- **The window is resizable** (and maximizable). The file list, text
  fields, and comment box stretch with it; the Save/Previous/Next/Cancel
  row always stays flush against the bottom edge of the window, however
  it's sized.

## Files

- `main.cpp` — Windows front end: window creation, controls, and application logic (Win32 API)
- `main_gtk.cpp` — Linux front end: the same UI and logic ported to GTK 3
- `PathUtil.h` — converts file paths to the form TagLib expects on each platform
  (UTF-16 on Windows; lossless UTF-8/raw bytes on Linux)
- `TagIO.h` / `.cpp` — format dispatcher: routes to `FlacTagIO` or
  `Mp3AiffTagIO` based on file extension, so the UI code doesn't need to
  care which format it's looking at
- `FlacTagIO.h` / `.cpp` — TagLib wrapper for FLAC (Vorbis comments)
- `Mp3AiffTagIO.h` / `.cpp` — TagLib wrapper for MP3/AIFF (ID3v2 frames)
- `CoverArt.h` / `.cpp` — (Windows) GDI+ helper that decodes cover images into
  thumbnail bitmaps for display
- `TagFields.h` — shared data structures (`TagFields`, `AudioInfo`, `CoverImage`)
- `resource.h` — control ID constants
- `CMakeLists.txt` — build script (picks the Win32 or GTK front end by platform)
- `mingw-toolchain.cmake` — CMake toolchain file for cross-compiling the Windows build from Linux/macOS
- `packaging/flac-tag-editor.desktop` — Linux desktop menu entry

## Dependencies

- **TagLib** (handles Vorbis-comment, ID3v2, and cover-art reading/writing)
- Windows: **GDI+** (`gdiplus.lib`) for cover thumbnails, Win32 API + COM
  (`IFileDialog`) — all part of the Windows SDK, no extra install
- Linux: **GTK 3** (includes GdkPixbuf for cover thumbnails)

## Option A — Build on Windows with Visual Studio + vcpkg (recommended)

```powershell
vcpkg install taglib:x64-windows
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

The executable will be at `build/Release/FlacTagEditor.exe`.

## Option B — Cross-compile from Linux/macOS with MinGW-w64

This is exactly how the release `.exe` is produced.

```bash
# 1. Install the cross-compiler
sudo apt-get install mingw-w64 cmake

# 2. Build TagLib for the Windows target
git clone --depth 1 --branch v1.13.1 https://github.com/taglib/taglib.git
cmake -S taglib -B taglib/build \
      -DCMAKE_TOOLCHAIN_FILE=mingw-toolchain.cmake \
      -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF \
      -DCMAKE_INSTALL_PREFIX=./taglib-install
cmake --build taglib/build --target install -j

# 3. Build the app
cmake -B build -DCMAKE_TOOLCHAIN_FILE=mingw-toolchain.cmake \
      -DTAGLIB_ROOT=$(pwd)/taglib-install
cmake --build build
```

Or skip CMake entirely and invoke the compiler directly:

```bash
x86_64-w64-mingw32-g++ -std=c++17 -O2 -mwindows -municode -DTAGLIB_STATIC \
  -I taglib-install/include -I taglib-install/include/taglib \
  main.cpp FlacTagIO.cpp Mp3AiffTagIO.cpp TagIO.cpp CoverArt.cpp -o FlacTagEditor.exe \
  -L taglib-install/lib -ltag \
  -lcomctl32 -lole32 -lshell32 -luuid -lgdiplus -lgdi32 \
  -static -static-libgcc -static-libstdc++ -s
```

(`-municode` selects the Unicode `wWinMain` entry point; `-s` strips debug
symbols for a smaller binary — drop it if you want to debug.)

## Option C — Build on Linux (GTK 3)

```bash
# Debian/Ubuntu
sudo apt-get install cmake g++ pkg-config libgtk-3-dev libtag1-dev   # libtag1-dev pulls in TagLib 2.x on newer releases

cmake -B build
cmake --build build
./build/FlacTagEditor
```

That links TagLib dynamically from the system. The release binary instead
links a static TagLib 1.13.1 (so it doesn't depend on the distro's TagLib
version) — build it the same way as step 2 of Option B but without the
toolchain file (add `-DCMAKE_POSITION_INDEPENDENT_CODE=ON`), then pass
`-DTAGLIB_ROOT=$(pwd)/taglib-install` when configuring the app.

`sudo cmake --install build` installs the binary and a desktop menu entry.

## Fixed bugs

- **MP3: clearing Comment, Album, Genre, Title, or Artist and saving
  silently did nothing.** Root cause: TagLib's default MP3 save writes
  *every* tag type present in the file (ID3v2, and a legacy ID3v1 tag if
  one exists — common from various encoders/taggers) with cross-tag value
  syncing enabled. If a field you just cleared in ID3v2 still had an old
  value sitting in the untouched ID3v1 tag, that sync silently re-filled
  it back in, making the clear appear to fail. Fixed by saving MP3 files
  as ID3v2-only, stripping any legacy ID3v1/APE tag, with syncing
  disabled — found and confirmed via a real read/write/verify test
  against actual MP3 files, not just code inspection. FLAC and AIFF were
  unaffected (FLAC has no legacy dual-tag format; AIFF's save path never
  had this option in the first place).

## Notes / known limitations

- Batch mode always applies the *same* cover image (or removal) to every
  selected file if you set one — there's no way to batch-clear a text field
  (blank = "don't touch" in batch mode, by design, to avoid accidentally
  wiping tags across many files at once).
- No batch renaming — the Filename field is disabled while multiple files
  are selected.
- Only the first embedded picture is shown/replaced. A file with multiple
  pictures (front cover, back cover, artist photo, etc.) will have all of
  them replaced by whatever single image you set.
- Filenames and all text fields are fully Unicode (UTF-16 internally, UTF-8
  in FLAC's Vorbis comments and MP3/AIFF's ID3v2 frames), so non-Latin
  filenames and tag text are handled correctly.
- If you change the filter text (or a format checkbox) while a file is
  loaded and that file drops out of view as a result, the fields are
  cleared without saving — save (or move on with Previous/Next) first if
  you want to keep edits.
- MP3 is a lossy format with no fixed sample bit depth, so the audio info
  panel omits "N-bit" for MP3 files and shows it only for FLAC/AIFF.
- MP3/AIFF Year/Date is stored as a raw ID3v2.4 `TDRC` frame. Very old
  players that only understand ID3v2.3 (`TYER`/`TDAT`/`TIME`) may not read
  it; virtually everything made in the last ~15 years handles `TDRC` fine.
