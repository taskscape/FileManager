# Fluent Color Essentials icon reference

This folder is the archival presentation set for the toolbar and menu glyphs in
`src/res/toolbars`. It is intentionally separate from runtime resources.

- `svg/` contains one 64 x 64 reference SVG for each runtime icon. The original
  16 x 16 view box is preserved so the reference art remains identical to the
  production glyph at every configured toolbar size.
- `fluent-color-essentials-icon-catalog.png` is the complete labeled catalog at
  3840 pixels wide; its height grows to fit every icon (4160 pixels for 106 icons).
- `generate-reference.cjs` rebuilds both deliverables from the runtime SVG set.

The runtime folder remains authoritative. Regenerate this reference after an
icon is added, removed, renamed, or visually changed.

The drive bar reuses the Documents, Desktop, and Network masters and adds the
`Drive*.svg` storage and bundled plug-in glyphs. Both toolbar rows use 16, 24, or
32 logical pixels, rasterized at the current DPI. Unknown plug-ins and missing
SVG assets retain their supplied icons. Drive-bar artwork uses native alpha for
hover and selection backgrounds; the bundled NanoSVG already emits Windows
premultiplied BGRA.

## Regeneration

Run the generator with Node.js and Sharp available through `NODE_PATH`:

```powershell
$env:NODE_PATH = '<node_modules directory containing sharp>'
node .\design\icon-reference\fluent-color-essentials\generate-reference.cjs
```

