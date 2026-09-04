// Assemble actual Windows raster samples without rescaling, so small-size review reflects the application.
const fs = require('fs');
const path = require('path');
const sharp = require('sharp');
const repo = path.resolve(__dirname, '..');
const output = path.join(repo, 'design/icon-reference/fluent-color-essentials');
const samples = path.join(repo, 'TestResults/fluent-rendering');
const names = fs.readdirSync(path.join(repo, 'src/res/toolbars')).filter(n => n.endsWith('.svg')).map(n => n.slice(0, -4)).sort();
async function main() {
    for (let start = 0; start < names.length; start += 32) {
        const page = names.slice(start, start + 32), width = 820, height = 106 + page.length * 44;
        const labels = ['16 px', '24 px', '32 px', 'Gray 32', 'Disabled 32'];
        let svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}"><rect width="100%" height="100%" fill="#f3f2f1"/><g font-family="Segoe UI, sans-serif" fill="#424242"><text x="22" y="32" font-size="20">Fluent icons · actual Windows rendering · ${start + 1}–${start + page.length}</text>`;
        labels.forEach((label, i) => { svg += `<text x="${354 + i * 90}" y="72" font-size="13">${label}</text>`; });
        page.forEach((name, row) => { svg += `<text x="22" y="${113 + row * 44}" font-size="14">${name}</text>`; });
        svg += '</g></svg>';
        const layers = [];
        for (const [row, name] of page.entries()) for (let column = 0; column < 5; column++) {
            const size = [16, 24, 32, 32, 32][column], state = [0, 0, 0, 1, 2][column];
            const bgra = fs.readFileSync(path.join(samples, `${name}-${size}-${state}.bgra`));
            for (let i = 0; i < bgra.length; i += 4) [bgra[i], bgra[i+2]] = [bgra[i+2], bgra[i]];
            layers.push({input: await sharp(bgra, {raw: {width: size, height: size, channels: 4}}).png().toBuffer(), left: 363 + column * 90, top: 91 + row * 44 + (32-size)/2});
        }
        await sharp(Buffer.from(svg)).composite(layers).png().toFile(path.join(output, `size-review-${start/32+1}.png`));
    }
    console.log(`Created actual-size contact sheets for ${names.length} icons.`);
}
main().catch(error => { console.error(error); process.exitCode = 1; });
