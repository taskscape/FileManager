# Fluent toolbar icon reference

All 124 application-owned command, drive, and bundled plug-in toolbar icon families use simple Fluent silhouettes and a single foreground color. The historical folder name is retained for compatibility. See [research and decisions](FLUENT-RESEARCH.md).

Runtime assets in src/res/toolbars are authoritative: root SVGs are 16 px masters, with complete 24/ and 32/ variants. The renderer selects the logical size before applying DPI; a small toolbar at 200% uses the 16 px master at 32 physical pixels. Where Microsoft does not supply an exact size, the provenance records the source size used. Menus retain compact icons independently of toolbar configuration.

Bundled plug-ins share GetPluginSVGName across drive and plug-in toolbars, menus, and plug-in lists. Related archive handlers share their archive metaphor. Unknown modules retain supplied artwork; missing optional SVGs retain bitmap fallbacks.

NanoSVG emits premultiplied BGRA for the command toolbar's AlphaBlend path. The HICON path restores straight alpha before Windows image-list insertion, preventing double premultiplication and dark edges. Grayscale preserves coverage, and disabled rendering tints both fills and strokes.

- fluent-map.json pins Microsoft package version and command-to-glyph mappings.
- fluent-provenance.json records original filenames, sizes, colors, and SHA-256.
- LICENSE-fluent.txt accompanies the Microsoft MIT-licensed artwork.
- svg/ contains enlarged inspection copies of the **16 px** masters only.
- fluent-color-essentials-icon-catalog.png is the complete labeled overview.
- size-review-*.png show actual Windows-rendered icons at 16/24/32 px, plus grayscale and disabled states, without enlarging the samples.

## Regeneration and verification

Use Node.js, an extracted @fluentui/svg-icons@1.1.339 package, and Sharp available through NODE_PATH. The import requires no network access or package execution.

    node tools/update-fluent-icons.cjs '<extracted package directory>'
    node design/icon-reference/fluent-color-essentials/generate-reference.cjs
    ./tools/verify-fluent-icon-coverage.ps1
    ./tools/verify-fluent-icon-rendering.ps1
    node tools/preview-fluent-icons.cjs

The native verifier uses installed Visual Studio 2026. It compiles the current production loader and renderer, checks all 2,976 logical-size/DPI combinations, compares command rendering with Windows image lists on three backgrounds, checks grayscale alpha and geometry bounds, and detects GDI resource growth. The preview script uses its actual raster output. UI tests separately exercise live toolbar size changes and persistence with an isolated profile.
