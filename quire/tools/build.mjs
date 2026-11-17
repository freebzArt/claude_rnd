// Builds dist/Quire.html: the whole app in one file you can double-click, carry on a USB stick,
// or keep next to your stories. Usage: node tools/build.mjs
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const read = (f) => readFileSync(join(root, 'app', f), 'utf8');

let html = read('index.html');
const css = read('styles.css');
const js = read('app.js').replace(/<\/script/gi, '<\\/script');
const icon = Buffer.from(read('icon.svg')).toString('base64');

const swap = (from, to) => {
  if (!html.includes(from)) throw new Error(`build: marker not found: ${from}`);
  html = html.replace(from, () => to);
};
// Drop the PWA links (a single local file has no manifest or service worker) but keep an inline icon.
html = html.replace(/<!--pwa-->[\s\S]*?<!--\/pwa-->/, `<link rel="icon" href="data:image/svg+xml;base64,${icon}">`);
swap('<link rel="stylesheet" href="styles.css">', `<style>\n${css}</style>`);
swap('<script src="app.js"></script>', `<script>\n${js}</script>`);

mkdirSync(join(root, 'dist'), { recursive: true });
writeFileSync(join(root, 'dist', 'Quire.html'), html);
console.log(`dist/Quire.html  ${(html.length / 1024).toFixed(1)} KB`);
