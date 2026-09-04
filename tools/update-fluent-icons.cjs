// Import a pinned Microsoft Fluent regular set; retain optical masters instead of stretching 16 px art everywhere.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const repo = path.resolve(__dirname, '..');
const design = path.join(repo, 'design/icon-reference/fluent-color-essentials');
const destination = path.join(repo, 'src/res/toolbars');
const spec = JSON.parse(fs.readFileSync(path.join(design, 'fluent-map.json'), 'utf8'));
const packageRoot = process.argv[2];
if (!packageRoot) throw new Error('Pass the extracted @fluentui/svg-icons package directory.');
const metadata = JSON.parse(fs.readFileSync(path.join(packageRoot, 'package.json'), 'utf8'));
if (metadata.name !== spec.package || metadata.version !== spec.version)
    throw new Error(`Expected ${spec.package}@${spec.version}.`);

// These small metaphors have no upstream 16 px drawing; one-pixel edges and open counters preserve their meaning.
const custom = {
    CommandShell: '<rect x="1.5" y="2.5" width="13" height="11" rx="1" fill="none" stroke="currentColor"/><path d="M4 5.5L6.5 8L4 10.5M8 10.5H12" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"/>',
    OpenRecycleBin: '<path d="M4.5 4.5H11.5L11 13.5H5Z M3.5 4.5H12.5M6 4V2.5H10V4" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"/><path d="M6.5 8.5C6.5 7 9.5 7 9.5 8.5M9.5 10C9.5 11.5 6.5 11.5 6.5 10" fill="none" stroke="currentColor"/><path d="M8.5 8L10 9H8.5ZM7.5 10.5L6 9.5H7.5Z"/>',
    PluginDiskMap: '<rect x="1.5" y="2.5" width="13" height="11" rx="1" fill="none" stroke="currentColor"/><path d="M3 4H7V12H3ZM8 4H13V7H8ZM8 8H13V12H8Z"/>',
    UnselectFilesWithSameName: '<path d="M7.5 12.5H2.5C2 12.5 1.5 12 1.5 11.5V4.5C1.5 4 2 3.5 2.5 3.5H13.5C14 3.5 14.5 4 14.5 4.5V8M4 6.5H12M4 9.5H7" fill="none" stroke="currentColor" stroke-linecap="round"/><path d="M9.5 10.5L13.5 14.5M13.5 10.5L9.5 14.5" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>',
    MenuRadio: '<circle cx="8" cy="8" r="2.5"/>',
};
const allSizesCustom = new Set(['CommandShell', 'MenuRadio']);
const blue = /^(Back|Forward|ParentDirectory|RootDirectory|GoTo|Focus|Drive|Plugin|Open|Select|Unselect|InvertSelection|RestoreSelection|SaveSelection|LoadSelection|MenuCheck|MenuRadio|ConnectNetworkDrive|Refresh|SwapPanels)/;
const manifest = { package: `${spec.package}@${spec.version}`, icons: {} };
const currentNames = fs.readdirSync(destination).filter(n => n.endsWith('.svg')).map(n => path.basename(n, '.svg')).sort();
if (JSON.stringify(currentNames) !== JSON.stringify(Object.keys(spec.icons).sort()))
    throw new Error('The mapping must cover every existing toolbar icon exactly.');

for (const size of [16, 24, 32]) {
    const outputDir = size === 16 ? destination : path.join(destination, String(size));
    fs.mkdirSync(outputDir, { recursive: true });
    for (const [name, upstream] of Object.entries(spec.icons)) {
        const color = /^(Delete|Stop)$/.test(name) ? '#C50F1F' : blue.test(name) ? '#0F6CBD' : '#424242';
        let body, sourceSize, source, hash;
        if (custom[name] && (size === 16 || allSizesCustom.has(name))) {
            body = custom[name]; sourceSize = 16; source = 'application optical drawing';
        } else {
            const style = name === 'Stop' ? 'filled' : 'regular';
            // Prefer the matching upstream size, then the closest smaller drawing; record every fallback for review.
            const candidates = size === 32 ? [32, 24, 28, 20, 16] : size === 24 ? [24, 20, 16, 28, 32] : [16, 20, 24];
            sourceSize = candidates.find(s => fs.existsSync(path.join(packageRoot, 'icons', `${upstream}_${s}_${style}.svg`)));
            if (!sourceSize) throw new Error(`Missing ${upstream} at ${size}.`);
            source = `${upstream}_${sourceSize}_${style}.svg`;
            const original = fs.readFileSync(path.join(packageRoot, 'icons', source), 'utf8');
            hash = crypto.createHash('sha256').update(original).digest('hex');
            body = original.replace(/^<svg[^>]*>/, '').replace(/<\/svg>\s*$/, '');
            if (/<(?:image|text|filter|mask|clipPath|linearGradient|radialGradient)\b/.test(body))
                throw new Error(`Unsupported runtime SVG features in ${source}.`);
        }
        body = body.replaceAll('currentColor', color);
        if (sourceSize !== size) body = `<g transform="scale(${size / sourceSize})">${body}</g>`;
        const meaning = name === 'GoToHotPath' ? 'A bookmark denotes a saved destination.' : `${name} retains a simple, recognizable silhouette.`;
        const comment = `<!-- Fluent System Icons: ${meaning} ${size} px optical slot from ${source}; solid color and open counters preserve clarity. -->`;
        const attribution = source === 'application optical drawing' ? '' : '\n<!-- Copyright (c) Microsoft Corporation. MIT License; see LICENSE-fluent.txt. -->';
        const svg = `${comment}${attribution}\n<svg width="${size}" height="${size}" viewBox="0 0 ${size} ${size}" fill="${color}" xmlns="http://www.w3.org/2000/svg">\n${body}\n</svg>\n`;
        fs.writeFileSync(path.join(outputDir, `${name}.svg`), svg);
        (manifest.icons[name] ??= {})[size] = { source, sourceSize, color, ...(hash ? { sha256: hash } : {}) };
    }
}
fs.writeFileSync(path.join(design, 'fluent-provenance.json'), JSON.stringify(manifest, null, 2) + '\n');
console.log(`Generated ${currentNames.length} icon families at 16, 24, and 32 px.`);
