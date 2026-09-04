# Microsoft Fluent icon research — 2026-09-04

## Primary sources and application

1. [Fluent 2 iconography](https://fluent2.microsoft.design/iconography) distinguishes system icons used in command bars from product launch icons. Regular icons are the default for actions and navigation; simple silhouettes, restrained details, and one solid foreground color support recognition. Filled modifiers belong at the lower right. Applied here to all toolbar families, with neutral, blue, and destructive red foregrounds. Filled Stop and menu radio marks retain their established semantics.
2. [Microsoft Windows icons guidance](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/icons) recommends clear, simple symbols that remain legible at small sizes and vector formats for scaling. Applied by removing shaded bitmap-style details, tiny lettering, gradients, and multi-color overlays. Shared color alone does not distinguish commands; each metaphor carries the meaning.
3. [Microsoft Fluent SVG package documentation](https://github.com/microsoft/fluentui-system-icons/blob/main/packages/svg-icons/README.md) describes separate size/style assets, notes that not every size exists, and deprecates color variants in favor of regular/filled icons. This change imports regular geometry from @fluentui/svg-icons@1.1.339, with MIT attribution and source hashes. It does not depend on an installed icon font.
4. The official [16 px Arrow Left](https://github.com/microsoft/fluentui-system-icons/blob/main/assets/Arrow%20Left/SVG/ic_fluent_arrow_left_16_regular.svg) and [24 px Arrow Left](https://github.com/microsoft/fluentui-system-icons/blob/main/assets/Arrow%20Left/SVG/ic_fluent_arrow_left_24_regular.svg) illustrate size-specific geometry. This informed logical-size master selection before DPI rasterization. Fluent's explicit advice against simply scaling product-launch icons is in its product section; that rule is not presented here as a separate system-icon mandate.

## Scope and optical-size decisions

The screenshot shows a coherent vector command row above shaded drive and plug-in icons. This revision brings all 124 command, drive, and bundled plug-in families into the same style, including the existing first row. Application identity icons and file-association artwork are separate from these toolbar action glyphs.

| Logical slot | Exact-size Microsoft drawing | Application drawing | Resized Microsoft fallback |
| --- | ---: | ---: | ---: |
| 16 px | 119 | 5 | 0 |
| 24 px | 116 | 2 | 6 |
| 32 px | 64 | 2 | 58 |

The application drawings cover console, menu radio, and missing small recycle, disk-map, and unselect-name metaphors. Console and radio use simple scalable geometry at larger sizes. Fallbacks prefer 24 px artwork for a missing 32 px master; the source is explicit in fluent-provenance.json, so these are not represented as independently designed 32 px icons. All variants receive the same runtime checks.

The palette is #424242 for general actions, #0F6CBD for navigation/storage/selection and plug-ins, and #C50F1F for Delete/Stop. These colors provide strong separation from the application's light toolbar and hover backgrounds. Disabled icons use the Windows button-shadow color. No new dark-theme support is claimed.

## Rendering checks

The application offers 16/24/32 logical pixels and DPI buckets 100, 125, 150, 200, 250, 300, 400, and 500 percent, producing 16–160 physical pixels. The native check exercises all combinations, geometry bounds, grayscale coverage, bitmap fallback, and both rendering paths on normal, white, and selection backgrounds. In testing, Windows image-list insertion premultiplied HICON channels; restoring straight alpha at that boundary fixed dark fringes. The direct command-toolbar path keeps NanoSVG's premultiplied output. The check compares the final pixels, rather than assuming both paths handle alpha alike.

Actual-size contact sheets support visual review at 100% zoom. Automated pixel and geometry checks supplement that review; they do not measure human recognition or replace testing on physical displays at their native resolution.

The imported paths also exposed the bundled NanoSVG version's incorrect arc sweep handling: circular paths could cancel out. The focused fix follows [upstream NanoSVG arc conversion](https://github.com/memononen/nanosvg/blob/master/src/nanosvg.h). Geometry checks permit 0.1 logical pixel of rounding in upstream path exports.
