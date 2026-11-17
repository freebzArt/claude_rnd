// Builds:
//   dist/Quire.html  the whole app in one file: double-click it, carry it on a USB stick, keep it beside your stories
//   dist/web/        the installable web app (+ Quire.html) that cloud/wrangler.toml deploys to Cloudflare
// Usage: node tools/build.mjs
import { readFileSync, writeFileSync, mkdirSync, rmSync, copyFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const app = (f) => join(root, 'app', f);
const read = (f) => readFileSync(app(f), 'utf8');
const SCRIPTS = ['merge.js', 'docx.js', 'app.js'];
const WEB = ['index.html', 'styles.css', ...SCRIPTS, 'manifest.webmanifest', 'icon.svg'];

// ---- single file
let html = read('index.html');
const swap = (from, to) => {
  if (!html.includes(from)) throw new Error(`build: marker not found: ${from}`);
  html = html.replace(from, () => to);
};
const icon = Buffer.from(read('icon.svg')).toString('base64');
html = html.replace(/<!--pwa-->[\s\S]*?<!--\/pwa-->/, `<link rel="icon" href="data:image/svg+xml;base64,${icon}">`);
swap('<link rel="stylesheet" href="styles.css">', `<style>\n${read('styles.css')}</style>`);
for (const s of SCRIPTS) swap(`<script src="${s}"></script>`, `<script>\n${read(s).replace(/<\/script/gi, '<\\/script')}</script>`);
mkdirSync(join(root, 'dist'), { recursive: true });
writeFileSync(join(root, 'dist', 'Quire.html'), html);

// ---- installable web app
const web = join(root, 'dist', 'web');
rmSync(web, { recursive: true, force: true });
mkdirSync(web, { recursive: true });
const hash = createHash('sha256');
for (const f of WEB) { copyFileSync(app(f), join(web, f)); hash.update(readFileSync(app(f))); }
hash.update(read('sw.js'));
writeFileSync(join(web, 'Quire.html'), html);
const version = hash.digest('hex').slice(0, 12);
const sw = read('sw.js').replace("const CACHE = 'quire-dev';", `const CACHE = 'quire-${version}';`);
if (!sw.includes(version)) throw new Error('build: could not stamp sw.js');
writeFileSync(join(web, 'sw.js'), sw);

console.log(`dist/Quire.html  ${(html.length / 1024).toFixed(1)} KB\ndist/web/        app ${version}`);
