# Installation & Build Guide — PS4 Button Mod

This guide takes you from source to a working in-game button swap. Three build
paths are documented; pick one:

- **A. MinGW-w64 + `build.ps1`** — one command, no IDE. *(Used to build & verify this repo.)*
- **B. CMake** — MSVC or MinGW.
- **C. Visual Studio** — GUI project setup.

---

## 1. Prerequisites

| Need | Why | Get it |
|---|---|---|
| A 64-bit C++17 compiler | OPWS is x64 | MinGW-w64 *or* Visual Studio (below) |
| **MinHook** source | function hooking | Already in `third_party/minhook/` (or clone it) |
| One PNG of PS4 button art | the replacement | You author it (see §6) |
| Windows 10/11 | WIC + UCRT | already present |

WIC (image decoding) and the DirectX 11 runtime ship with Windows — there is
**nothing else to download**.

### Get MinHook (only if `third_party/minhook` is empty)

```powershell
git clone --depth 1 https://github.com/TsudaKageyu/minhook third_party/minhook
```

The build compiles MinHook **from source** alongside the DLL, so there is no
prebuilt `.lib` to manage and no runtime-library mismatch.

---

## 2. Build — Path A: MinGW-w64 (recommended, no IDE)

Install MinGW-w64 if you don't have it:

```powershell
winget install BrechtSanders.WinLibs.POSIX.UCRT
```

Then from the project root:

```powershell
.\build.ps1
```

Output → `dist\PS4ButtonMod.dll` and `dist\Injector.exe`.

> **Non-ASCII path note (important on this machine).** The GNU toolchain renders
> wide paths through the ANSI codepage, so it fails to assemble/link when either
> the **build folder** *or* the **compiler's own install folder** contains
> non-ASCII characters (e.g. a Unicode user-profile name like `C:\Users\דניאל`).
> `build.ps1` handles both automatically:
> 1. it builds via the project's **8.3 short path** (pure ASCII), and
> 2. if the compiler itself is under a non-ASCII path, it **mirrors the toolchain
>    once** to `C:\mgw` and builds from there.
>
> This is a GCC limitation, not a bug in the mod. MSVC (Path C) is unaffected.

---

## 3. Build — Path B: CMake (MSVC or MinGW)

```powershell
# MinGW + Ninja
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Visual Studio 2022/2026 generator, x64
cmake -S . -B build -A x64
cmake --build build --config Release
```

`CMakeLists.txt` builds MinHook statically, links
`d3d11 dxgi windowscodecs ole32 oleaut32 uuid`, and statically links the
C/C++ runtime so the artifacts are self-contained.

---

## 4. Build — Path C: Visual Studio (GUI)

1. **File → New → Project → Dynamic-Link Library (DLL)**, name it `PS4ButtonMod`.
   Switch the toolbar to **Release / x64**. ⚠️ x64 is mandatory.
2. Add to the project: `src/dllmain.cpp`, `src/Crc32.h`, `src/TextureLoader.h`,
   and **all** files under `third_party/minhook/src` (including `src/hde/`).
3. **Project → Properties** (Release | x64):
   - **C/C++ → Language → C++ Language Standard** → `ISO C++17 (/std:c++17)`
   - **C/C++ → General → Additional Include Directories** → add
     `third_party\minhook\include`
   - **C/C++ → Code Generation → Runtime Library** → `Multi-threaded (/MT)`
     (so the game doesn't need a VC++ redistributable)
   - `d3d11.lib`, `dxgi.lib`, `windowscodecs.lib` are linked automatically by the
     `#pragma comment(lib, …)` lines in the code under MSVC — no manual step.
4. **Build** → `x64\Release\PS4ButtonMod.dll`.
5. Add a second **Console App** project `Injector`, also **Release / x64**, add
   `injector/main.cpp`, build → `Injector.exe`.

> To install the C++ tools: Visual Studio Installer → Modify → check
> **“Desktop development with C++.”**

---

## 5. Install into the game folder

Find the install dir (Steam: *right-click → Manage → Browse local files*). The
real D3D11 process is usually:

```
...\OnePieceWorldSeeker\Game\Binaries\Win64\OPWS.exe
```

Place files **next to that exe**:

```
...\Binaries\Win64\
├─ OPWS.exe
├─ PS4ButtonMod.dll        ← from dist\
├─ Injector.exe            ← from dist\
└─ PS4Mod\
   ├─ config.ini
   ├─ debug_magenta.png    ← for discovery (included)
   ├─ ps4_buttons.png      ← your art (add after discovery)
   └─ ps4mod_log.txt       ← created at runtime
```

> If a small launcher `OPWS.exe` spawns a separate *Shipping* exe, inject into
> whichever process owns the game window and the GPU usage. Pass its name to the
> injector: `Injector.exe TheRealProcess.exe`.

The mod reads `PS4Mod\` relative to the **host .exe**, and works fine even when
the game lives under a Unicode path (it uses wide file APIs throughout).

---

## 6. Finding the Xbox button texture

The hooks are the easy part — the real task is identifying **which** texture
holds the prompts. Do this once:

1. Keep `DumpTextures=1` in `config.ini`. Launch, inject, and walk to a screen
   that clearly shows Xbox glyphs (tutorial popup, pause menu, interact prompt).
2. Open `PS4Mod\ps4mod_log.txt`. Each uploaded texture logs a line:
   ```
   [TEX]  512x512  fmt=98  mips=10  hash=0x3FA90C12
   ```
   Button atlases are typically **square power-of-two** (256/512/1024/2048),
   often `BC7` (fmt **98**), `BC3` (fmt **77**), or uncompressed RGBA8 (fmt **28**).
   Note a few candidate hashes.
3. **Confirm visually.** Add a dimension rule pointing at the included magenta
   tile and relaunch:
   ```ini
   [Replacements]
   1024x1024 = debug_magenta.png
   ```
   Whatever turns magenta in-game at that size is in that bucket. Narrow the size
   until **only the button prompts** change.
4. **Pin it by hash** and turn dumping off:
   ```ini
   [Settings]
   DumpTextures=0

   [Replacements]
   0x3FA90C12 = ps4_buttons.png
   ```

### Authoring `ps4_buttons.png`

This is an **in-place texture swap, not an overlay**. The mod doesn't draw
anything on top of the frame — it hands the game *your* atlas in place of the
Xbox one, so the game renders your glyphs through its own UI quads. The practical
consequence: your atlas must mirror the original.

- **Same pixel dimensions** as the Xbox atlas (from the `[TEX]` log line).
- **Same UV cell per glyph** — the game's mesh/UVs are unchanged, so △/◯/✕/□ must
  sit exactly where Y/B/A/X were. Standard Xbox → PS face-button mapping:

  | Xbox | Position | PS4 | Color |
  |---|---|---|---|
  | A | bottom | ✕ Cross | blue |
  | B | right | ◯ Circle | red |
  | X | left | □ Square | pink |
  | Y | top | △ Triangle | green |

- **Format is free** — the mod decodes your PNG to RGBA8. Keep the background
  transparent (alpha) so only the glyphs are opaque.
- If prompts span several atlases, add one `0xHASH = file.png` rule per atlas.

Starter glyph sheets are included in `PS4Mod/` (transparent, optically centered):
`ps4_buttons_template.png` (1024×1024 — face buttons in rings, L1/R1/L2/R2,
L3/R3, D-pad, Touchpad, Options, Share, PS) and `ps4_buttons_filled.png`
(solid face buttons). These are procedural *reference art* — fine as a
placeholder, but for a designed look use a CC0 pack (next section).

### Using a CC0 prompt pack (recommended for best quality)

The mod swaps **any** PNG, so the highest-quality route is a free,
professionally-designed controller-prompt pack rather than hand-drawn glyphs:

- **Kenney — “Input Prompts”** (`kenney.nl/assets/input-prompts`, CC0)
- **Xelu — “FREE Controller & Keyboard Prompts”** (`thoseawesomeguys.com/prompts`,
  CC0) — the multi-style set most button mods use.

A pack ships **one PNG per button**, but the game samples **one atlas** texture,
so the included compositor stitches them together:

```powershell
# 1. Put the pack's PlayStation PNGs in tools\glyphs\
# 2. After discovery, edit tools\atlas_layout.ini:
#      [atlas] width/height  = the original atlas size from the [TEX] log line
#      each glyph's x,y,w,h   = the cell that glyph must occupy
# 3. Build the atlas:
.\tools\build_atlas.ps1
#    -> writes PS4Mod\ps4_buttons.png (clean bicubic scaling, transparent bg)
# 4. Point config.ini at it:  0xHASH = ps4_buttons.png
```

See [tools/glyphs/README.md](tools/glyphs/README.md) for pack links and naming.
If the game uses **separate** textures per glyph (not one atlas), skip the
compositor and point one `0xHASH = pack_file.png` rule at each pack PNG directly.

---

## 7. Running & injecting

**Built-in injector** — it waits up to ~2 minutes for the process, so order
doesn't matter:

```powershell
.\Injector.exe                       # defaults: OPWS.exe + .\PS4ButtonMod.dll
.\Injector.exe OPWS.exe PS4ButtonMod.dll
```

Optional `run.bat` next to the game exe:

```bat
@echo off
start "" Injector.exe OPWS.exe PS4ButtonMod.dll
start "" steam://rungameid/PUT_APPID_HERE
```

**Prefer Xenos?** Add `PS4ButtonMod.dll`, target `OPWS.exe`, use the standard
`LoadLibrary` method (manual mapping not required), and inject after the game
window appears.

Confirm success in `PS4Mod\ps4mod_log.txt`:

```
=== PS4 Button Mod starting ===
Config loaded: dump=0, hash-rules=1, dim-rules=0
Hook installation: OK
First Present(): D3D11 pipeline active, mod ready.
Loaded replacement '...\PS4Mod\ps4_buttons.png'
[MATCH] 512x512 hash=0x3FA90C12 -> replacement bound
```

---

## 8. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `LoadLibrary returned 0` | Wrong arch (must be x64), or injected before D3D11 init — inject at the menu. |
| No `ps4mod_log.txt` at all | DLL never loaded; check the injector found the right process and DLL path. |
| Log exists, no `[TEX]` lines | `DumpTextures` is 0, or the atlas is created without initial pixel data (streamed) — try the magenta dimension rule instead of a hash. |
| `[MATCH]` appears but nothing changes on screen | Glyphs may come from a different stage/material; verify with the magenta tile first. |
| `FAILED to load '…png'` | Path/typo, or file isn't a PNG/JPG/BMP/TIFF (DDS isn't supported by WIC). |
| `Dummy D3D11 device failed … trying WARP` then OK | Harmless — the throwaway device only harvests vtables; WARP works fine for that. |
| Build: `as: can't create …` or `ld: cannot find -lkernel32` under a Unicode path | Use `build.ps1` (handles it) — see the note in §2. |

---

## 9. Caveats & design notes

- **Pointer reuse.** Identification caches raw COM pointers. If the game frees the
  atlas and the allocator reuses that address, a stale match is theoretically
  possible. UI atlases are loaded once and long-lived, so this rarely matters;
  exact-hash rules keep matches tight.
- **Mip levels.** Replacements are created with a single mip — correct for UI that
  samples mip 0. (For mipped sampling you'd add `GenerateMips`.)
- **DDS support.** WIC decodes PNG/JPG/BMP/TIFF. For `.dds`, drop Microsoft's
  single-file `DDSTextureLoader.h/.cpp` into `src/` and call
  `CreateDDSTextureFromFile` inside `GetOrLoadSRV` instead of `LoadImageToSRV`.
- **Anti-tamper.** This is a graphics-only, single-player UI mod. If injection
  ever fails at launch, reach the main menu first, then inject.
