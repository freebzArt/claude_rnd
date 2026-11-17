// Headless smoke test for dist/Quire.html, opened from file:// exactly as a writer would.
// Usage: node tools/build.mjs && node tools/smoke.mjs [--shots <dir>]
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { mkdirSync } from 'node:fs';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const url = pathToFileURL(join(root, 'dist', 'Quire.html')).href;
const shotsArg = process.argv.indexOf('--shots');
const shots = shotsArg > 0 ? resolve(process.argv[shotsArg + 1]) : null;
if (shots) mkdirSync(shots, { recursive: true });

let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'}  ${msg}`); if (!cond) failures++; };
const shot = async (page, name) => { if (shots) await page.screenshot({ path: join(shots, `${name}.png`) }); };

const browser = await chromium.launch();

// 1. The real File System Access API must be available on a file:// page.
{
  const page = await browser.newPage();
  await page.goto(url);
  const env = await page.evaluate(() => ({ secure: isSecureContext, save: typeof showSaveFilePicker, open: typeof showOpenFilePicker }));
  check(env.secure && env.save === 'function' && env.open === 'function', `file:// page can use the File System Access API (${JSON.stringify(env)})`);
  await page.close();
}

// 2. The full workflow, with the native file picker replaced by an in-memory file.
const ctx = await browser.newContext({ viewport: { width: 1360, height: 860 } });
await ctx.addInitScript(() => {
  window.__files = {};
  window.__mtimes = {};
  const handle = (name) => ({
    kind: 'file', name,
    queryPermission: async () => 'granted', requestPermission: async () => 'granted',
    isSameEntry: async (o) => o.name === name,
    getFile: async () => new File([window.__files[name] || ''], name, { lastModified: window.__mtimes[name] || 1 }),
    createWritable: async () => { let buf = ''; return { write: async (d) => { buf += d; }, close: async () => { window.__files[name] = buf; window.__mtimes[name] = Date.now(); } }; },
  });
  window.showSaveFilePicker = async (o) => handle(o.suggestedName);
  window.showOpenFilePicker = async () => [handle(Object.keys(window.__files)[0])];
});
const page = await ctx.newPage();
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
await page.goto(url);
await page.waitForSelector('.wordmark');
await shot(page, '01-welcome');

// Sanitiser
const san = await page.evaluate(() => {
  const s = window.quire.sanitizeHTML;
  return [
    s('<div>one</div><div><b>two</b> <i>three</i></div>'),
    s('<p>a<br>b</p><p><br></p>'),
    s('<img src=x onerror=alert(1)><script>alert(2)</script><p>safe</p>'),
    s('<b style="font-weight:normal"><span style="font-style:italic">docs</span> text</b>'),
    window.quire.countWords('It’s a well-known fact — isn’t it? 42 words…'),
  ];
});
check(san[0] === '<p>one</p><p><strong>two</strong> <em>three</em></p>', `sanitise divs and marks: ${san[0]}`);
check(san[1] === '<p>a</p><p>b</p><p><br></p>', `sanitise line breaks: ${san[1]}`);
check(san[2] === '<p>safe</p>', `sanitise strips scripts and images: ${san[2]}`);
check(san[3] === '<p><em>docs</em> text</p>', `sanitise Google Docs paste: ${san[3]}`);
check(san[4] === 8, `word count handles apostrophes and hyphens: ${san[4]}`);

// Sample story
await page.click('text=Explore a sample');
await page.waitForSelector('.prose');
await page.waitForTimeout(300);
await shot(page, '02-write');
const before = await page.evaluate(() => window.quire.project.manuscript[0].children[0].content.length);
check(before > 100, 'sample story opens on its first scene');

// Typing, smart quotes, dashes, word counts
const prose = page.locator('.prose').first();
await prose.click();
await page.keyboard.press('Control+End');
await page.keyboard.press('Enter');
await page.keyboard.type('"Not tonight," she said -- and meant it...', { delay: 5 });
await page.waitForTimeout(800);
const typed = await page.evaluate(() => window.quire.project.manuscript[0].children[0].content);
check(typed.includes('“Not tonight,” she said — and meant it…'), 'smart quotes, em dash and ellipsis while typing');
const status = await page.textContent('#stCounts');
check(/Scene \d/.test(status) && /Book [\d,]+ \/ 60,000/.test(status), `status bar counts: ${status}`);
const today = await page.textContent('#stToday');
check(/Today \+7/.test(today), `today's progress: ${today}`);

// Save as (to the in-memory file), then reopen it
await page.click('#stSave');
await page.waitForTimeout(500);
const saved = await page.evaluate(() => window.__files['The Lantern Keeper.quire']);
check(saved && JSON.parse(saved).format === 'quire' && saved.includes('meant it…'), 'Save as writes a .quire file with the new text');
check((await page.textContent('#stSave')).includes('Saved to The Lantern Keeper.quire'), 'status shows saved file');
await prose.click();
await page.keyboard.press('Control+End');
await page.keyboard.type(' More.', { delay: 5 });
await page.waitForTimeout(2600);
const autosaved = await page.evaluate(() => window.__files['The Lantern Keeper.quire']);
check(autosaved.includes('meant it… More.'), 'autosave writes to the file after a pause');

// Inspector shows mention suggestions
await page.click('.b-scene >> nth=1');
await page.waitForTimeout(200);
check(await page.isVisible('#inspector .insp'), 'scene details panel is visible');
await shot(page, '03-scene-details');

// Board, story map, characters, places, notes
for (const [key, name, sel] of [['2', 'board', '.card'], ['3', 'map', 'table.grid'], ['4', 'characters', '.ent-card'], ['5', 'places', '.ent-card'], ['6', 'notes', '.prose']]) {
  await page.keyboard.press(`Alt+${key}`);
  await page.waitForTimeout(250);
  check(await page.locator(sel).count() > 0, `view: ${name}`);
  await shot(page, `04-${name}`);
}
await page.keyboard.press('Alt+4');
await page.click('.ent-card >> nth=0');
await page.waitForTimeout(200);
const apps = await page.textContent('.sheet');
check(/Appears in 3 scenes/.test(apps), 'character sheet lists scene appearances');
await shot(page, '05-character-sheet');

// Story map: toggle a thread cell
await page.keyboard.press('Alt+3');
const threadsBefore = await page.evaluate(() => window.quire.project.manuscript[1].children[1].threads.length);
await page.locator('table.grid tbody tr:nth-child(2) td.cell').nth(3).click();
const threadsAfter = await page.evaluate(() => window.quire.project.manuscript[1].children[1].threads.length);
check(threadsAfter !== threadsBefore, 'story map cell toggles a plot thread');

// Command palette full-text search
await page.keyboard.press('Control+k');
await page.keyboard.type('mercury');
await page.waitForTimeout(100);
await shot(page, '06-palette');
await page.keyboard.press('Enter');
await page.waitForTimeout(250);
check((await page.textContent('#crumb')).includes('Last of the oil'), 'palette finds a line of prose and opens its scene');

// Focus mode
await page.keyboard.press('Control+Shift+F');
await page.waitForTimeout(300);
check(await page.evaluate(() => document.body.classList.contains('focus-mode')), 'focus mode on');
await shot(page, '07-focus');
await page.keyboard.press('Escape');

// Night theme, chapter view
await page.evaluate(() => { localStorage.setItem('quire:prefs', JSON.stringify({ theme: 'night' })); });
await page.click('.b-chap >> nth=0');
await page.keyboard.press('Control+k');
await page.keyboard.type('theme: night');
await page.keyboard.press('Enter');
await page.waitForTimeout(250);
await shot(page, '08-night-chapter');
check(await page.evaluate(() => document.documentElement.dataset.theme === 'night'), 'night theme');

// Add a scene with Ctrl+Enter
const n0 = await page.evaluate(() => window.quire.project.manuscript.flatMap((c) => c.children).length);
await page.locator('.prose').first().click();
await page.keyboard.press('Control+Enter');
await page.waitForTimeout(200);
const n1 = await page.evaluate(() => window.quire.project.manuscript.flatMap((c) => c.children).length);
check(n1 === n0 + 1, 'Ctrl+Enter adds a scene');

// Drag a scene to the other chapter in the binder
await page.keyboard.press('Alt+1');
const firstTitle = await page.evaluate(() => window.quire.project.manuscript[0].children[0].title);
await page.locator('.b-scene', { hasText: firstTitle }).dragTo(page.locator('.b-scene', { hasText: 'What the tide brought' }), { targetPosition: { x: 40, y: 20 } });
await page.waitForTimeout(200);
const moved = await page.evaluate(() => window.quire.project.manuscript[1].children.map((s) => s.title));
check(moved.at(-1) === firstTitle, `drag a scene into another chapter: ${moved.join(', ')}`);

// Delete a scene, then undo it
const exists = (t) => page.evaluate((x) => window.quire.project.manuscript.flatMap((c) => c.children).some((s) => s.title === x), t);
await page.locator('.b-scene', { hasText: firstTitle }).click({ button: 'right' });
await page.click('.menu-item:has-text("Delete scene")');
check(!(await exists(firstTitle)) && (await page.textContent('#toast')).includes('Undo'), 'delete scene offers Undo');
await page.locator('#sideList').click({ position: { x: 5, y: 5 } });
await page.evaluate(() => document.activeElement.blur());
await page.keyboard.press('Control+z');
await page.waitForTimeout(100);
check(await exists(firstTitle), 'Ctrl+Z brings the scene back');
await page.keyboard.press('Control+Shift+z');
check(!(await exists(firstTitle)), 'Ctrl+Shift+Z deletes it again');
await page.keyboard.press('Control+z');

// Split a scene at the cursor, then merge it back
await page.locator('.b-scene', { hasText: 'Last of the oil' }).click();
await page.locator('.prose p').nth(2).click({ position: { x: 2, y: 8 } });
await page.keyboard.press('Home');
const before2 = await page.evaluate(() => window.quire.project.manuscript.flatMap((c) => c.children).length);
await page.keyboard.press('Control+Shift+Enter');
await page.waitForTimeout(150);
const split = await page.evaluate(() => { const all = window.quire.project.manuscript.flatMap((c) => c.children); const i = all.findIndex((s) => s.title === 'Last of the oil'); return { n: all.length, a: all[i].content, b: all[i + 1].content, t: all[i + 1].title }; });
check(split.n === before2 + 1 && split.a.includes('Eleven cans') && !split.a.includes('mercury') && split.b.startsWith('<p>Above her') && split.t === 'Last of the oil (continued)', `split at cursor: ${split.t}`);
await page.locator('.b-scene', { hasText: /^Last of the oil\d/ }).first().click({ button: 'right' });
await page.click('.menu-item:has-text("Merge with next scene")');
const merged = await page.evaluate(() => { const all = window.quire.project.manuscript.flatMap((c) => c.children); return { n: all.length, c: all.find((s) => s.title === 'Last of the oil').content }; });
check(merged.n === before2 && merged.c.includes('Eleven cans') && merged.c.includes('mercury'), 'merge with next joins them back');

// Find and replace
await page.keyboard.press('Control+h');
await page.fill('.modal .fr input >> nth=0', 'Wren');
await page.waitForTimeout(250);
const info = await page.textContent('.modal .hint');
await page.fill('.modal .fr input >> nth=1', 'Robin');
await page.click('.modal .btn:has-text("Replace all")');
await page.waitForTimeout(100);
const after = await page.evaluate(() => JSON.stringify(window.quire.project.manuscript));
check(/\d+ match/.test(info) && !after.includes('Wren counted') && after.includes('Robin counted'), `find and replace across scenes (${info})`);
await page.locator('#sideList').click({ position: { x: 5, y: 5 } });
await page.evaluate(() => document.activeElement.blur());
await page.keyboard.press('Control+z');
check((await page.evaluate(() => JSON.stringify(window.quire.project.manuscript))).includes('Wren counted'), 'replace all can be undone');

// Word export round trip through import
const docx = await page.evaluate(async () => {
  const bytes = QuireDocs.exportDocx(window.quire.project, { countWords: () => 200 });
  const parsed = await QuireDocs.importDocx(bytes, 'x');
  return { size: bytes.length, title: parsed.title, chapters: parsed.chapters.map((c) => c.title), firstPara: parsed.chapters[0].scenes[0].html.slice(0, 60), italics: JSON.stringify(parsed).includes('<em>nobody</em>') };
});
check(docx.size > 2000 && docx.title === 'The Lantern Keeper' && docx.chapters.join('|') === 'I. The Light|II. The Harbour' && docx.italics, `Word export reads back: ${JSON.stringify(docx)}`);
const md = await page.evaluate(() => QuireDocs.importText('# My Book\n\n## Chapter 1\n\nIt was *dark*.\n\n***\n\nLater **that** night.\n\n## Chapter 2\n\nEnd.', 'x', true));
check(md.title === 'My Book' && md.chapters.length === 2 && md.chapters[0].scenes.length === 2 && md.chapters[0].scenes[0].html === '<p>It was <em>dark</em>.</p>', `Markdown import: ${JSON.stringify(md)}`);

// The file changed on disk (another laptop via a synced folder): merged, not overwritten
await page.evaluate(async () => {
  const name = 'The Lantern Keeper.quire';
  const other = JSON.parse(window.__files[name]);
  other.manuscript[1].children[0].content = '<p>Written on the other laptop.</p>';
  window.__files[name] = JSON.stringify(other);
  window.__mtimes[name] = Date.now() + 5000;
});
await page.locator('.b-scene', { hasText: 'A boat in the fog' }).click();
await page.locator('.prose').first().click();
await page.keyboard.press('Control+End');
await page.keyboard.type(' Local words.', { delay: 5 });
await page.waitForTimeout(2600);
const diskMerged = await page.evaluate(() => window.__files['The Lantern Keeper.quire']);
check(diskMerged.includes('Written on the other laptop.') && diskMerged.includes('Local words.'), 'edits made to the file elsewhere are merged in, not overwritten');

// Paste rich text from another app
await page.locator('.b-scene', { hasText: 'Ansel' }).click();
await page.locator('.prose').first().click();
await page.evaluate(() => {
  const dt = new DataTransfer();
  dt.setData('text/html', '<meta charset="utf-8"><b style="font-weight:normal"><p><span style="font-size:20pt;color:red">Pasted </span><span style="font-style:italic">words</span></p><p>Second para</p></b>');
  dt.setData('text/plain', 'Pasted words\nSecond para');
  document.activeElement.dispatchEvent(new ClipboardEvent('paste', { clipboardData: dt, bubbles: true, cancelable: true }));
});
await page.waitForTimeout(700);
const pasted = await page.evaluate(() => window.quire.project.manuscript[1].children[0].content);
check(pasted.includes('Pasted <em>words</em>') && pasted.includes('<p>Second para</p>') && !/style|span|color/.test(pasted), `paste keeps italics, drops styling: ${pasted}`);

// Close, then reopen from the welcome screen
await page.click('#tbMenu');
await page.click('.menu-item:has-text("Close story")');
await page.waitForSelector('.wordmark');
await shot(page, '09-welcome-recent');
check(await page.locator('.w-item').count() > 0, 'welcome lists the story after closing');
await page.locator('.w-item').first().click();
await page.waitForSelector('.prose');
check((await page.textContent('#projTitle')) === 'The Lantern Keeper', 'reopens from the welcome screen');

// New story: creates a file straight away
await page.click('#tbMenu');
await page.click('.menu-item:has-text("Close story")');
await page.waitForSelector('.wordmark');
await page.click('text=New story');
await page.fill('.new-row input', 'Salt and Iron');
await page.click('.new-row button');
await page.waitForTimeout(600);
check(await page.evaluate(() => !!window.__files['Salt and Iron.quire']), 'new story is saved to a file at once');
check((await page.textContent('#stSave')).includes('Saved to Salt and Iron.quire'), 'new story shows saved status');

// Narrow window
await page.setViewportSize({ width: 700, height: 800 });
await page.waitForTimeout(150);
await shot(page, '10-narrow');
check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'no horizontal scroll at 700px');

await page.evaluate(() => { localStorage.setItem('quire:prefs', JSON.stringify({ theme: 'paper' })); });
check(errors.length === 0, `no page errors${errors.length ? `: ${errors.join(' | ')}` : ''}`);

await browser.close();
console.log(failures ? `\n${failures} check(s) failed` : '\nall checks passed');
process.exit(failures ? 1 : 0);
