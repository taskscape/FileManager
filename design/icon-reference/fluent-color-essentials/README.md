# Fluent Color Essentials icon reference

This folder is the archival presentation set for the toolbar and menu glyphs in
`src/res/toolbars`. It is intentionally separate from runtime resources.

- `svg/` contains one 64 x 64 reference SVG for each runtime icon. The original
  16 x 16 view box is preserved so the reference art remains identical to the
  production glyph at every configured toolbar size.
- `fluent-color-essentials-icon-catalog.png` is the complete labeled catalog at
  3840 x 3440 pixels.
- `generate-reference.cjs` rebuilds both deliverables from the runtime SVG set.

The runtime folder remains authoritative. Regenerate this reference after an
icon is added, removed, renamed, or visually changed.

## Regeneration

Run the generator with Node.js and Sharp available through `NODE_PATH`:

```powershell
$env:NODE_PATH = '<node_modules directory containing sharp>'
node .\design\icon-reference\fluent-color-essentials\generate-reference.cjs
```

