// End-to-end sync test: two "laptops" (separate browser profiles running dist/Quire.html from file://),
// the real cloud/worker.js behind a local server, and a flight with no connection in the middle.
// Then the installed web app opening with the server switched off.
// Usage: node tools/build.mjs && node tools/sync-e2e.mjs [--shots <dir>] [--server http://127.0.0.1:8787 --token KEY]
//   --server runs against an already running server, e.g. `wrangler dev` (Cloudflare's own runtime).
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { mkdirSync } from 'node:fs';
import { startServer } from './dev-server.mjs';

const { chromium } = createRequire(import.meta.url)('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const fileUrl = pathToFileURL(join(root, 'dist', 'Quire.html')).href;
const shotsArg = process.argv.indexOf('--shots');
const shots = shotsArg > 0 ? resolve(process.argv[shotsArg + 1]) : null;
if (shots) mkdirSync(shots, { recursive: true });

let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'}  ${msg}`); if (!cond) failures++; };
const shot = async (page, name) => { if (shots) await page.screenshot({ path: join(shots, `${name}.png`) }); };

const arg = (n) => { const i = process.argv.indexOf(`--${n}`); return i > 0 ? process.argv[i + 1] : null; };
const external = arg('server');
const KEY = arg('token') || 'test-key';
const server = external ? null : await startServer({ port: 0, token: KEY });
const cloudUrl = external || `http://127.0.0.1:${server.port}`;
const cloudStory = async (id) => {
  if (server) return JSON.parse(server.bucket._objects.get(`stories/${id}.quire`)?.body || 'null');
  const r = await fetch(`${cloudUrl}/api/stories/${id}`, { headers: { Authorization: `Bearer ${KEY}` } });
  return r.ok ? r.json() : null;
};
const browser = await chromium.launch();

async function laptop(name) {
  const ctx = await browser.newContext({ viewport: { width: 1300, height: 820 } });
  await ctx.addInitScript(() => {
    window.__files = {}; window.__mtimes = {};
    const handle = (n) => ({
      kind: 'file', name: n, queryPermission: async () => 'granted', requestPermission: async () => 'granted',
      isSameEntry: async (o) => o.name === n,
      getFile: async () => new File([window.__files[n] || ''], n, { lastModified: window.__mtimes[n] || 1 }),
      createWritable: async () => { let buf = ''; return { write: async (d) => { buf += d; }, close: async () => { window.__files[n] = buf; window.__mtimes[n] = Date.now(); } }; },
    });
    window.showSaveFilePicker = async (o) => handle(o.suggestedName);
    window.showOpenFilePicker = async () => [handle(Object.keys(window.__files)[0])];
  });
  const page = await ctx.newPage();
  page.on('pageerror', (e) => { console.log(`  [${name} page error] ${e}`); failures++; });
  await page.goto(fileUrl);
  await page.waitForSelector('.wordmark');
  return { ctx, page };
}
const story = (page) => page.evaluate(() => window.quire.project);
const sceneText = (p, title) => p.manuscript.flatMap((c) => c.children).find((s) => s.title === title)?.content || '';
const sync = (page) => page.evaluate(() => window.quire.syncStory({ force: true }));
async function typeInScene(page, title, text) {
  await page.locator('.b-scene', { hasText: title }).first().click();
  await page.locator('.prose').first().click();
  await page.keyboard.press('Control+End');
  await page.keyboard.type(text, { delay: 3 });
  await page.waitForTimeout(700);
}

// ---------- Laptop A: start a story, connect it to the cloud through the UI ----------
const A = await laptop('A');
await A.page.click('text=Explore a sample');
await A.page.waitForSelector('.prose');
await A.page.click('#stSave'); // Save as… (fake picker)
await A.page.waitForTimeout(300);
await A.page.click('#tbMenu');
await A.page.click('.menu-item:has-text("Cloud sync")');
await A.page.fill('.cloud input >> nth=0', cloudUrl);
await A.page.fill('.cloud input >> nth=1', 'wrong-key');
await A.page.click('.cloud .btn:has-text("Connect")');
await A.page.waitForFunction(() => !/Connecting/.test(document.querySelector('.cloud .hint')?.textContent || 'Connecting'), null, { timeout: 15000 });
const wrongMsg = await A.page.textContent('.cloud .hint');
check(wrongMsg.includes('sync key is wrong'), `a wrong sync key is reported clearly: "${wrongMsg}"`);
await A.page.fill('.cloud input >> nth=1', KEY);
await A.page.click('.cloud .btn:has-text("Connect")');
await A.page.waitForSelector('.cloud [role="switch"]');
await A.page.click('.cloud [role="switch"]');
await A.page.waitForTimeout(600);
await shot(A.page, 'sync-01-cloud-dialog');
await A.page.keyboard.press('Escape');
const storyId = (await story(A.page)).meta.id;
check(!!(await cloudStory(storyId)), 'turning on sync uploads the story');
check((await A.page.textContent('#stCloud')).startsWith('Synced'), `status bar: ${await A.page.textContent('#stCloud')}`);

// ---------- Laptop B: connect; the welcome screen lists the cloud story and downloads it ----------
const B = await laptop('B');
await B.page.evaluate(([u, k]) => localStorage.setItem('quire:cloud', JSON.stringify({ url: u, token: k })), [cloudUrl, KEY]);
await B.page.reload();
await B.page.waitForSelector('.w-section h3:has-text("In your cloud")');
await B.page.waitForTimeout(600);
await shot(B.page, 'sync-02-welcome-cloud');
check((await B.page.textContent('.w-section:has(h3:has-text("In your cloud"))')).includes('on this computer'), 'laptop B downloads the cloud story in the background');

// ---------- A writes more and syncs. B is online with Quire open, so it picks that up ----------
await typeInScene(A.page, 'Ansel’s warning', 'Ansel rowed out at noon.');
await sync(A.page);
await B.page.evaluate(() => window.quire.prefetchCloud());
await B.page.waitForTimeout(300);

// ---------- The flight: B has no connection at all ----------
await B.ctx.setOffline(true);
await B.page.reload();
await B.page.waitForSelector('.wordmark');
await B.page.waitForTimeout(300);
check(await B.page.isVisible('text=The Lantern Keeper'), 'offline: the cloud story is still listed on laptop B');
await B.page.click('.w-item:has-text("The Lantern Keeper")');
await B.page.waitForSelector('.prose');
check(sceneText(await story(B.page), 'Ansel’s warning').includes('Ansel rowed out at noon.'), 'offline: laptop B opens the latest version, with A’s newest scene');
await typeInScene(B.page, 'What the tide brought', 'The tide brought a lantern.');
await typeInScene(B.page, 'A boat in the fog', ' B wrote this on the plane.');
await sync(B.page);
check((await B.page.textContent('#stCloud')).includes('Offline'), `offline status: ${await B.page.textContent('#stCloud')}`);
await shot(B.page, 'sync-03-offline-writing');

// Meanwhile A, on the ground, edits a different scene and the same scene as B.
await typeInScene(A.page, 'Last of the oil', ' A added this at home.');
await typeInScene(A.page, 'A boat in the fog', ' A changed this too.');
await sync(A.page);

// B's work survives a reload while still offline (browser copy).
await B.page.reload();
await B.page.waitForSelector('.wordmark');
await B.page.click('.w-item:has-text("The Lantern Keeper")');
await B.page.waitForSelector('.prose');
check(sceneText(await story(B.page), 'What the tide brought').includes('The tide brought a lantern.'), 'offline work survives closing and reopening Quire');

// ---------- Landing: B reconnects, everything merges ----------
await B.ctx.setOffline(false);
await B.page.evaluate(() => window.dispatchEvent(new Event('online')));
await B.page.waitForTimeout(1500);
await sync(B.page);
await sync(A.page);
const a = await story(A.page), b = await story(B.page);
const all = (p) => JSON.stringify(p.manuscript);
for (const [who, p] of [['A', a], ['B', b]]) {
  check(all(p).includes('Ansel rowed out at noon.') && all(p).includes('The tide brought a lantern.') && all(p).includes('A added this at home.'), `laptop ${who} has every edit from both laptops`);
  const fog = p.manuscript.flatMap((c) => c.children).filter((s) => s.title.startsWith('A boat in the fog'));
  check(fog.length === 2 && fog.some((s) => s.content.includes('A changed this too.')) && fog.some((s) => s.content.includes('B wrote this on the plane.')),
    `laptop ${who}: the scene edited on both laptops kept both versions (${fog.map((s) => s.title).join(' | ')})`);
}
const ids = (p) => p.manuscript.map((c) => c.children.map((s) => `${s.id}:${s.content.length}`).join(',')).join('/');
check(ids(a) === ids(b), 'both laptops end with the same manuscript');
await A.page.click('.b-scene:has-text("(other version)")');
await shot(A.page, 'sync-04-after-landing');

// ---------- Two laptops saving at once can't overwrite each other ----------
await typeInScene(A.page, 'Last of the oil', ' Race A.');
await typeInScene(B.page, 'What the tide brought', ' Race B.');
await Promise.all([sync(A.page), sync(B.page)]);
await sync(A.page); await sync(B.page); await sync(A.page);
const finalCloud = await cloudStory(storyId);
check(all(finalCloud).includes('Race A.') && all(finalCloud).includes('Race B.'), 'simultaneous saves from two laptops both land in the cloud');

const hist = await A.page.evaluate(async (k) => (await (await fetch(`${JSON.parse(localStorage.getItem('quire:cloud')).url}/api/stories/${window.quire.project.meta.id}/history`, { headers: { Authorization: `Bearer ${k}` } })).json()).versions.length, KEY);
check(hist >= 1, `cloud keeps version history (${hist})`);

// ---------- Installed web app: opens with the server gone ----------
const C = await browser.newContext();
const page = await C.newPage();
await page.goto(`${cloudUrl}/`);
await page.waitForSelector('.wordmark');
await page.evaluate(() => navigator.serviceWorker.ready);
await page.reload();
await page.waitForFunction(() => !!navigator.serviceWorker.controller);
await page.waitForFunction(() => document.querySelector('.ready')?.textContent.includes('Ready to work offline'), null, { timeout: 10000 }).catch(() => {});
check((await page.textContent('.ready')).includes('Ready to work offline'), `web app says it is ready offline: "${await page.textContent('.ready')}"`);
if (server) await server.close();
await C.setOffline(true);
await page.reload();
await page.waitForSelector('.wordmark', { timeout: 5000 }).catch(() => {});
check(await page.isVisible('.wordmark'), 'web app opens with the server switched off and no network');
await page.click('text=Explore a sample');
await page.waitForSelector('.prose', { timeout: 5000 }).catch(() => {});
check(await page.isVisible('.prose'), 'and writing works');
await shot(page, 'sync-05-webapp-offline');

await browser.close();
console.log(failures ? `\n${failures} sync check(s) failed` : '\nall sync checks passed');
process.exit(failures ? 1 : 0);
