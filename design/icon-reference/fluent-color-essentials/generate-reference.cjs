// Regenerates the archival icon masters and catalog directly from authoritative runtime SVGs.
const fs = require('fs');
const path = require('path');
const sharp = require('sharp');

const referenceRoot = __dirname;
const repositoryRoot = path.resolve(referenceRoot, '..', '..', '..');
const sourceDirectory = path.join(repositoryRoot, 'src', 'res', 'toolbars');
const svgDirectory = path.join(referenceRoot, 'svg');
const catalogPath = path.join(referenceRoot, 'fluent-color-essentials-icon-catalog.png');

const catalogWidth = 3840;
const catalogHeight = 3440;
const columns = 7;
const cellWidth = 520;
const cellHeight = 240;
const horizontalMargin = 100;
const headerHeight = 240;

function xmlEscape(value) {
    return value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
}

function createReferenceSvg(source) {
    const resized = source.replace(
        '<svg width="16" height="16" viewBox="0 0 16 16"',
        '<svg width="64" height="64" viewBox="0 0 16 16"');
    if (resized === source)
        throw new Error('Runtime SVG does not use the expected 16 x 16 design grid.');

    return '<!-- Reference master: 64 px inspection size with the production 16 px design grid preserved. -->\n' + resized;
}

async function main() {
    const iconNames = fs.readdirSync(sourceDirectory)
        .filter((name) => name.toLowerCase().endsWith('.svg'))
        .sort((left, right) => left.localeCompare(right));

    if (iconNames.length === 0)
        throw new Error('No runtime toolbar SVGs were found.');

    // This directory is wholly generated, so clearing it prevents removed runtime icons from lingering in the archive.
    fs.rmSync(svgDirectory, { recursive: true, force: true });
    fs.mkdirSync(svgDirectory, { recursive: true });

    const referenceIcons = iconNames.map((name) => {
        const source = fs.readFileSync(path.join(sourceDirectory, name), 'utf8');
        const reference = createReferenceSvg(source);
        fs.writeFileSync(path.join(svgDirectory, name), reference, 'utf8');
        return { name, label: path.basename(name, '.svg'), svg: reference };
    });

    const cards = referenceIcons.map((icon, index) => {
        const column = index % columns;
        const row = Math.floor(index / columns);
        const x = horizontalMargin + column * cellWidth + 10;
        const y = headerHeight + row * cellHeight + 10;
        const centerX = x + 250;
        const dataUri = Buffer.from(icon.svg).toString('base64');
        return [
            `<rect x="${x}" y="${y}" width="500" height="220" rx="24" fill="#FFFFFF" stroke="#D6D6D6" stroke-width="2"/>`,
            `<image x="${centerX - 64}" y="${y + 24}" width="128" height="128" href="data:image/svg+xml;base64,${dataUri}"/>`,
            `<text x="${centerX}" y="${y + 190}" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="26" font-weight="600" fill="#323130">${xmlEscape(icon.label)}</text>`,
        ].join('\n');
    }).join('\n');

    const catalogSvg = `
<svg xmlns="http://www.w3.org/2000/svg" width="${catalogWidth}" height="${catalogHeight}" viewBox="0 0 ${catalogWidth} ${catalogHeight}">
  <rect width="${catalogWidth}" height="${catalogHeight}" fill="#F5F5F5"/>
  <text x="100" y="100" font-family="Segoe UI, Arial, sans-serif" font-size="64" font-weight="700" fill="#201F1E">Fluent Color Essentials</text>
  <text x="100" y="158" font-family="Segoe UI, Arial, sans-serif" font-size="30" fill="#605E5C">Open Salamander toolbar and menu icon reference - ${referenceIcons.length} vector glyphs</text>
  <line x1="100" y1="198" x2="3740" y2="198" stroke="#C8C6C4" stroke-width="2"/>
  ${cards}
</svg>`;

    await sharp(Buffer.from(catalogSvg))
        .png({ compressionLevel: 9 })
        .toFile(catalogPath);

    process.stdout.write(`Generated ${referenceIcons.length} reference SVGs and ${path.basename(catalogPath)}.\n`);
}

main().catch((error) => {
    process.stderr.write(`${error.stack || error.message}\n`);
    process.exitCode = 1;
});

