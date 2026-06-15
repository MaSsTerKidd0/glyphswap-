# Drop your controller-prompt glyphs here

Put the **individual** PNG icons from a CC0 controller-prompt pack in this
folder, then run `..\build_atlas.ps1` to stitch them into a single
`GlyphSwap\ps4_buttons.png` atlas the mod can swap in.

## Recommended free (CC0 / public-domain) packs

- **Kenney — “Input Prompts”** — `kenney.nl/assets/input-prompts` (CC0).
  Clean PlayStation/Xbox/Switch/keyboard glyphs, individual PNGs.
- **Xelu — “FREE Controller & Keyboard Prompts”** (Nicolae Berbece) —
  `thoseawesomeguys.com/prompts` (CC0). This is the multi-style set most button
  mods use; matches the “designed pack” look exactly.

> Always confirm the licence on the download page before redistributing. CC0 /
> public-domain means you can use them in your mod freely.

## How to use

1. Download a pack and copy its PlayStation PNGs here.
2. Rename them to match `atlas_layout.ini` (e.g. `ps_cross.png`, `ps_circle.png`),
   **or** edit the `file` column in `atlas_layout.ini` to your pack's filenames.
3. After the in-game discovery step, set `[atlas] width/height` and each glyph's
   `x,y,w,h` in `atlas_layout.ini` to match the original atlas's cells.
4. Run `..\build_atlas.ps1`.

Files in this folder are not part of the mod's source — they're your chosen
art. Commit them only if their licence allows (CC0 does).
