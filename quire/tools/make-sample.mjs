// Writes samples/The Lantern Keeper.quire from the app's built-in sample. Usage: node tools/make-sample.mjs
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { writeFileSync, mkdirSync } from 'node:fs';

const { chromium } = createRequire(import.meta.url)('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const browser = await chromium.launch();
const page = await browser.newPage();
await page.goto(pathToFileURL(join(root, 'dist', 'Quire.html')).href);
const json = await page.evaluate(async () => {
  await window.quire.openProject(window.quire.sampleProject(), null, { recoverCheck: false });
  return window.quire.serialize();
});
await browser.close();
mkdirSync(join(root, 'samples'), { recursive: true });
writeFileSync(join(root, 'samples', 'The Lantern Keeper.quire'), `${json}\n`);
console.log(`samples/The Lantern Keeper.quire  ${(json.length / 1024).toFixed(1)} KB`);
