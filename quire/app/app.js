/* Quire: a quiet, offline-first place to write a story.
 *
 * One classic script (no modules) so the app runs straight from file:// in Chrome or Edge.
 * A story is one JSON file (*.quire); see docs/file-format.md.
 */
(() => {
'use strict';

/* =========================================================================
 * Constants
 * ========================================================================= */
const VERSION = '0.1.0';
const FORMAT = 1;
const EXT = '.quire';
const HAS_FS = typeof window.showOpenFilePicker === 'function' && typeof window.showSaveFilePicker === 'function';
const OPEN_TYPES = [{ description: 'Quire story', accept: { 'application/json': ['.quire', '.json'] } }];
const SAVE_TYPES = [{ description: 'Quire story', accept: { 'application/json': ['.quire'] } }];
const COLORS = ['#b0643c', '#4f7f8f', '#7b8f4c', '#8a5c9c', '#c09238', '#566fb0', '#a84f62', '#4c8a70', '#8c7a64'];
const STATUSES = [['idea', 'Idea'], ['draft', 'Draft'], ['revised', 'Revised'], ['final', 'Final']];
const ENTITY = {
  characters: {
    one: 'character', title: 'Characters', icon: 'users', roleLabel: 'Role', rolePh: 'Protagonist, mentor, rival…',
    fields: ['Goal', 'Motivation', 'Conflict', 'Appearance', 'Voice'],
  },
  locations: {
    one: 'place', title: 'Places', icon: 'pin', roleLabel: 'Kind', rolePh: 'Village, ship, tavern…',
    fields: ['Atmosphere', 'Sights & sounds', 'Significance'],
  },
};
const VIEWS = [
  ['write', 'Manuscript', 'pen'],
  ['board', 'Board', 'board'],
  ['plot', 'Story map', 'route'],
  ['characters', 'Characters', 'users'],
  ['locations', 'Places', 'pin'],
  ['notes', 'Notes', 'note'],
];
const PREF_KEY = 'quire:prefs';
const DEFAULT_PREFS = {
  theme: 'auto', font: 'serif', size: 19, measure: 34, para: 'indent',
  smart: true, typewriter: true, dim: true, sidebar: true, inspector: true,
};

/* =========================================================================
 * Small utilities
 * ========================================================================= */
const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];
const uid = (p) => p + Math.random().toString(36).slice(2, 8) + Date.now().toString(36).slice(-4);
const nowISO = () => new Date().toISOString();
const pad = (n) => String(n).padStart(2, '0');
const dayKey = (d = new Date()) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
const fmt = (n) => (n || 0).toLocaleString();
const byId = (arr, id) => arr.find((x) => x.id === id);
const plural = (n, w) => `${fmt(n)} ${w}${n === 1 ? '' : 's'}`;
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const escRe = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const safeName = (s) => (s || 'Untitled').replace(/[\\/:*?"<>|]+/g, '').trim().slice(0, 80) || 'Untitled';
function debounce(fn, ms) {
  let t;
  const d = (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); };
  d.cancel = () => clearTimeout(t);
  return d;
}
function ago(ts) {
  if (!ts) return '';
  const s = (Date.now() - new Date(ts).getTime()) / 1000;
  if (s < 45) return 'just now';
  if (s < 3600) return `${Math.round(s / 60)} min ago`;
  if (s < 86400) return `${Math.round(s / 3600)} h ago`;
  const d = new Date(ts);
  return d.toLocaleDateString(undefined, { month: 'short', day: 'numeric', year: d.getFullYear() === new Date().getFullYear() ? undefined : 'numeric' });
}

function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
    else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
    else if (k === 'value') el.value = v;
    else if (typeof v === 'boolean') el[k] = v;
    else el.setAttribute(k, v);
  }
  add(el, kids);
  return el;
}
function add(el, kids) {
  for (const k of kids.flat(Infinity)) {
    if (k == null || k === false) continue;
    el.append(k instanceof Node ? k : document.createTextNode(String(k)));
  }
  return el;
}

const ICONS = {
  pen: '<path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4Z"/>',
  board: '<rect x="3" y="4" width="5" height="16" rx="1.2"/><rect x="10" y="4" width="5" height="10" rx="1.2"/><rect x="17" y="4" width="4" height="13" rx="1.2"/>',
  route: '<circle cx="6" cy="19" r="2"/><circle cx="18" cy="5" r="2"/><path d="M8 19h8.5a3.5 3.5 0 0 0 0-7h-9a3.5 3.5 0 0 1 0-7H16"/>',
  users: '<path d="M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M22 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/>',
  user: '<circle cx="12" cy="8" r="4"/><path d="M4 21v-1a6 6 0 0 1 6-6h4a6 6 0 0 1 6 6v1"/>',
  pin: '<path d="M20 10c0 6-8 12-8 12s-8-6-8-12a8 8 0 0 1 16 0Z"/><circle cx="12" cy="10" r="3"/>',
  note: '<path d="M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9Z"/><path d="M14 3v6h6"/><path d="M8 13h8M8 17h5"/>',
  search: '<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>',
  focus: '<path d="M8 3H5a2 2 0 0 0-2 2v3M21 8V5a2 2 0 0 0-2-2h-3M3 16v3a2 2 0 0 0 2 2h3M16 21h3a2 2 0 0 0 2-2v-3"/>',
  panelR: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M15 3v18"/>',
  panelL: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M9 3v18"/>',
  more: '<circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  chev: '<path d="m6 9 6 6 6-6"/>',
  x: '<path d="M18 6 6 18M6 6l12 12"/>',
  book: '<path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20V3H6.5A2.5 2.5 0 0 0 4 5.5Z"/><path d="M4 19.5A2.5 2.5 0 0 0 6.5 22H20v-5"/>',
  folder: '<path d="M4 20h16a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.7-.9L9.6 3.9A2 2 0 0 0 7.9 3H4a2 2 0 0 0-2 2v13c0 1.1.9 2 2 2Z"/>',
  spark: '<path d="M12 3v4M12 17v4M3 12h4M17 12h4M5.6 5.6l2.8 2.8M15.6 15.6l2.8 2.8M5.6 18.4l2.8-2.8M15.6 8.4l2.8-2.8"/>',
  file: '<path d="M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9Z"/><path d="M14 3v6h6"/>',
  history: '<path d="M3 12a9 9 0 1 0 3-6.7L3 8"/><path d="M3 3v5h5"/><path d="M12 7v5l3 3"/>',
  restore: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="m7 10 5 5 5-5"/><path d="M12 15V3"/>',
  cmd: '<path d="m9 18 6-12"/>',
  thread: '<path d="M4 6h16M4 12h10M4 18h13"/>',
};
function icon(name) {
  const s = h('span', { class: 'ico', 'aria-hidden': 'true' });
  s.innerHTML = `<svg viewBox="0 0 24 24">${ICONS[name] || ''}</svg>`;
  return s;
}

/* =========================================================================
 * Text: sanitising rich text, counting words, export conversions
 * Scene content is stored as a tiny HTML subset: <p>, <em>, <strong>.
 * ========================================================================= */
const WORD_RE = /[\p{L}\p{N}]+(?:['’\-][\p{L}\p{N}]+)*/gu;
const countWords = (text) => (text.match(WORD_RE) || []).length;

function parseFragment(html) {
  const t = document.createElement('template'); // inert: no scripts, no image loads
  t.innerHTML = html || '';
  return t.content;
}
function textOf(html) {
  return [...parseFragment(html).childNodes].map((n) => n.textContent).join('\n');
}
function paragraphsOf(html) {
  return [...parseFragment(html).childNodes].filter((n) => n.nodeType === 1 || n.textContent.trim());
}

const BLOCK_RE = /^(P|DIV|H[1-6]|LI|BLOCKQUOTE|SECTION|ARTICLE|PRE|UL|OL|TABLE|TR|TD|TH|HEADER|FOOTER|ASIDE|FIGURE)$/;
function sanitizeHTML(html) {
  const root = parseFragment(html);
  const paras = [];
  let cur = null;
  const close = () => { if (cur !== null) { paras.push(cur); cur = null; } };
  const escT = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/ /g, ' ');
  const isLast = (n) => { let x = n.nextSibling; while (x && x.nodeType === 3 && !x.data.trim()) x = x.nextSibling; return !x; };

  function walk(node, m) {
    for (const n of node.childNodes) {
      if (n.nodeType === 3) {
        let t = n.data.replace(/[\r\n\t]+/g, ' ');
        if (cur === null && !t.trim()) continue;
        if (cur === null) t = t.replace(/^\s+/, '');
        let s = escT(t);
        if (s) {
          if (m.em) s = `<em>${s}</em>`;
          if (m.strong) s = `<strong>${s}</strong>`;
          cur = (cur || '') + s;
        }
      } else if (n.nodeType === 1) {
        const tag = n.tagName;
        if (tag === 'SCRIPT' || tag === 'STYLE' || tag === 'TEMPLATE' || tag === 'META' || tag === 'TITLE') continue;
        if (tag === 'BR') {
          if (isLast(n) && cur !== null) continue; // trailing placeholder <br>
          if (cur === null) paras.push(''); else close();
          continue;
        }
        if (BLOCK_RE.test(tag)) {
          close();
          const before = paras.length;
          walk(n, m);
          close();
          if (paras.length === before) paras.push('');
          continue;
        }
        const st = n.style || {};
        const next = {
          em: m.em || tag === 'EM' || tag === 'I' || st.fontStyle === 'italic',
          strong: m.strong || tag === 'STRONG' || tag === 'B' || st.fontWeight === 'bold' || +st.fontWeight >= 600,
        };
        // Google Docs wraps everything in <b style="font-weight:normal">.
        if (tag === 'B' && st.fontWeight && (st.fontWeight === 'normal' || +st.fontWeight < 600)) next.strong = m.strong;
        walk(n, next);
      }
    }
  }
  walk(root, {});
  close();
  const out = paras
    .map((p) => p.replace(/<\/em><em>/g, '').replace(/<\/strong><strong>/g, '').replace(/\s+$/, ''))
    .map((p) => `<p>${p || '<br>'}</p>`).join('');
  return out || '<p><br></p>';
}

function inlineToMd(node) {
  let s = '';
  for (const n of node.childNodes) {
    if (n.nodeType === 3) s += n.data;
    else if (n.tagName === 'EM') s += `*${inlineToMd(n)}*`;
    else if (n.tagName === 'STRONG') s += `**${inlineToMd(n)}**`;
    else s += inlineToMd(n);
  }
  return s;
}
const htmlToMd = (html) => paragraphsOf(html).map((p) => inlineToMd(p).trim()).filter(Boolean).join('\n\n');
const htmlToPlain = (html) => paragraphsOf(html).map((p) => p.textContent.trim()).filter(Boolean).join('\n\n');
const escHTML = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
function textToHTML(text) {
  const lines = text.replace(/\r\n?/g, '\n').split(/\n/).map((l) => l.trim()).filter(Boolean);
  return lines.map((l) => `<p>${escHTML(l)}</p>`).join('') || '<p><br></p>';
}

/* =========================================================================
 * Browser storage: IndexedDB for backups, snapshots and recent file handles
 * ========================================================================= */
const idb = (() => {
  let dbp = null;
  const open = () => dbp || (dbp = new Promise((res, rej) => {
    const r = indexedDB.open('quire', 1);
    r.onupgradeneeded = () => r.result.createObjectStore('kv');
    r.onsuccess = () => res(r.result);
    r.onerror = () => rej(r.error);
  }));
  async function op(mode, fn) {
    const db = await open();
    return new Promise((res, rej) => {
      const tx = db.transaction('kv', mode);
      const req = fn(tx.objectStore('kv'));
      tx.oncomplete = () => res(req && req.result);
      tx.onerror = () => rej(tx.error);
      tx.onabort = () => rej(tx.error);
    });
  }
  const safe = (p, fallback) => p.catch((e) => { console.warn('IndexedDB:', e); return fallback; });
  return {
    get: (k) => safe(op('readonly', (s) => s.get(k)), undefined),
    set: (k, v) => safe(op('readwrite', (s) => s.put(v, k)), undefined),
    del: (k) => safe(op('readwrite', (s) => s.delete(k)), undefined),
    keys: () => safe(op('readonly', (s) => s.getAllKeys()), []),
  };
})();

function loadPrefs() {
  try { return { ...DEFAULT_PREFS, ...JSON.parse(localStorage.getItem(PREF_KEY) || '{}') }; } catch { return { ...DEFAULT_PREFS }; }
}
let prefs = loadPrefs();
function setPref(k, v) {
  prefs[k] = v;
  try { localStorage.setItem(PREF_KEY, JSON.stringify(prefs)); } catch { /* private mode */ }
  applyPrefs();
}

/* =========================================================================
 * Model
 * ========================================================================= */
let project = null;      // the open story
let fileHandle = null;   // FileSystemFileHandle when saving straight to disk
let fileName = '';
let dirty = false;
let changeSeq = 0;
let saving = false, saveAgain = false, saveError = null, needsPerm = false, lastSaved = 0;

const ui = {
  view: 'write',
  sel: { kind: 'book', id: null },
  collapsed: new Set(),
  entitySel: { characters: null, locations: null },
  noteSel: null,
  activeSceneId: null,
  inspNarrow: false, // details panel on small windows: an overlay, closed by default
};

const mkChapter = (title = 'Untitled chapter') => ({ id: uid('c'), title, synopsis: '', notes: '', children: [] });
const mkScene = (title = '') => ({
  id: uid('s'), title, status: 'draft', synopsis: '', pov: '', characters: [], location: '',
  threads: [], when: '', notes: '', content: '<p><br></p>',
});
const mkEntity = (kind, name = '') => ({
  id: uid(kind === 'characters' ? 'ch' : 'lo'), name, role: '', color: COLORS[project[kind].length % COLORS.length],
  aliases: '', summary: '', fields: ENTITY[kind].fields.map((k) => ({ k, v: '' })), notes: '',
});
const mkThread = (name = 'New thread') => ({ id: uid('t'), name, color: COLORS[(project.threads.length + 3) % COLORS.length], summary: '' });
const mkNote = (title = '') => ({ id: uid('n'), title, content: '<p><br></p>' });

function newProject(title) {
  const ch = mkChapter('Chapter One');
  ch.children.push(mkScene('Opening'));
  return {
    format: 'quire', version: FORMAT, app: `Quire ${VERSION}`,
    meta: { id: uid('p'), title: title || 'Untitled story', author: '', created: nowISO(), modified: nowISO(), wordGoal: 80000, dailyGoal: 1000 },
    manuscript: [ch], characters: [], locations: [], threads: [], notes: [], stats: { daily: {} },
  };
}

/** Fill in defaults so files from older or newer versions still open. */
function normalize(p) {
  if (!p || typeof p !== 'object' || !Array.isArray(p.manuscript)) throw new Error('This is not a Quire story file.');
  if (p.version > FORMAT) console.warn('File is from a newer Quire; unknown fields are kept.');
  const base = newProject();
  p.meta = { ...base.meta, ...(p.meta || {}) };
  for (const k of ['characters', 'locations', 'threads', 'notes']) if (!Array.isArray(p[k])) p[k] = [];
  p.stats = p.stats && typeof p.stats === 'object' ? p.stats : {};
  p.stats.daily = p.stats.daily || {};
  p.manuscript = p.manuscript.map((c) => ({ ...mkChapter(), ...c, children: (c.children || []).map((s) => ({ ...mkScene(), ...s })) }));
  for (const s of p.manuscript.flatMap((c) => c.children)) {
    for (const k of ['characters', 'threads']) if (!Array.isArray(s[k])) s[k] = [];
    delete s._wc;
  }
  for (const kind of ['characters', 'locations']) {
    p[kind] = p[kind].map((e, i) => ({ name: '', role: '', color: COLORS[i % COLORS.length], aliases: '', summary: '', fields: [], notes: '', ...e }));
  }
  p.threads = p.threads.map((t, i) => ({ name: '', color: COLORS[i % COLORS.length], summary: '', ...t }));
  p.notes = p.notes.map((n) => ({ title: '', content: '<p><br></p>', ...n }));
  p.format = 'quire';
  p.version = FORMAT;
  return p;
}
const serialize = () => JSON.stringify({ ...project, app: `Quire ${VERSION}` }, (k, v) => (k.startsWith('_') ? undefined : v), 2);

const scenes = () => project.manuscript.flatMap((c) => c.children);
function locateScene(id) {
  for (const ch of project.manuscript) {
    const index = ch.children.findIndex((s) => s.id === id);
    if (index >= 0) return { ch, scene: ch.children[index], index };
  }
  return null;
}
const chapterOf = (id) => project.manuscript.find((c) => c.id === id);
function wcOf(scene) {
  if (scene._wc == null) scene._wc = countWords(plainOf(scene));
  return scene._wc;
}
/** Plain text of a scene or note, cached until its content changes. */
function plainOf(obj) {
  if (obj._src !== obj.content) { obj._src = obj.content; obj._text = textOf(obj.content); }
  return obj._text;
}
const chapterWC = (ch) => ch.children.reduce((a, s) => a + wcOf(s), 0);
const totalWC = () => project.manuscript.reduce((a, c) => a + chapterWC(c), 0);

function trackDaily() {
  const d = project.stats.daily, k = dayKey(), t = totalWC();
  if (!d[k]) d[k] = { start: t, end: t }; else d[k].end = t;
}
const todayWords = () => { const d = project.stats.daily[dayKey()]; return d ? d.end - d.start : 0; };

function entityNames(kind, e) {
  const names = [e.name.trim(), ...e.aliases.split(',').map((s) => s.trim())];
  if (kind === 'characters') {
    const first = e.name.trim().split(/\s+/)[0];
    if (first && first.length > 2 && !/^(the|a|an|old|young|mr|mrs|ms|dr|lady|lord|sir|captain|mother|father|aunt|uncle)\.?$/i.test(first)) names.push(first);
  }
  return [...new Set(names.filter((n) => n.length > 1))];
}
function mentions(kind, e, text) {
  const names = entityNames(kind, e);
  if (!names.length) return false;
  return new RegExp(`(?<![\\p{L}\\p{N}])(?:${names.map(escRe).join('|')})(?![\\p{L}\\p{N}])`, 'u').test(text);
}

/* =========================================================================
 * Change tracking, saving, backups
 * ========================================================================= */
const scheduleSave = debounce(() => saveNow(), 1500);
const scheduleBackup = debounce(() => backupNow(), 4000);

function touch() {
  dirty = true;
  changeSeq++;
  project.meta.modified = nowISO();
  trackDaily();
  scheduleSave();
  if (!fileHandle) scheduleBackup();
  renderStatus();
}

async function backupNow(data = serialize()) {
  if (!project) return;
  const id = project.meta.id;
  const rec = { id, title: project.meta.title, modified: project.meta.modified, at: Date.now(), words: totalWC(), fileName, data };
  await idb.set(`backup:${id}`, rec);
  const snaps = (await idb.get(`snaps:${id}`)) || [];
  if (!snaps.length || Date.now() - snaps[0].at > 10 * 60 * 1000) {
    snaps.unshift({ at: rec.at, words: rec.words, data });
    await idb.set(`snaps:${id}`, snaps.slice(0, 30));
  }
}

async function saveNow({ interactive = false } = {}) {
  if (!project) return;
  flushEditors();
  const seq = changeSeq;
  const data = serialize();
  await backupNow(data);
  if (!fileHandle) {
    if (interactive) return saveAs();
    renderStatus();
    return;
  }
  if (saving) { saveAgain = true; return; }
  saving = true;
  renderStatus();
  try {
    let perm = fileHandle.queryPermission ? await fileHandle.queryPermission({ mode: 'readwrite' }) : 'granted';
    if (perm !== 'granted' && interactive) perm = await fileHandle.requestPermission({ mode: 'readwrite' });
    if (perm !== 'granted') { needsPerm = true; return; }
    needsPerm = false;
    const w = await fileHandle.createWritable();
    await w.write(data);
    await w.close();
    if (seq === changeSeq) dirty = false;
    lastSaved = Date.now();
    saveError = null;
  } catch (e) {
    saveError = e;
    console.error('Save failed', e);
  } finally {
    saving = false;
    renderStatus();
    if (saveAgain) { saveAgain = false; saveNow(); }
  }
}

async function saveAs() {
  if (!project) return;
  flushEditors();
  if (!HAS_FS) { downloadProject(); return; }
  try {
    const handle = await window.showSaveFilePicker({ suggestedName: safeName(project.meta.title) + EXT, types: SAVE_TYPES });
    fileHandle = handle;
    fileName = handle.name;
    saveError = null;
    await saveNow({ interactive: true });
    await addRecent(handle);
    toast(`Saving to ${handle.name}`);
  } catch (e) {
    if (e.name !== 'AbortError') { toast(`Couldn’t save: ${e.message}`); console.error(e); }
  }
  renderStatus();
}

function download(name, text, type) {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = h('a', { href: url, download: name });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}
function downloadProject() {
  flushEditors();
  download(safeName(project.meta.title) + EXT, serialize(), 'application/json');
  dirty = false;
  lastSaved = Date.now();
  backupNow();
  renderStatus();
  toast('Downloaded a copy of your story');
}

async function getRecent() { return (await idb.get('recent')) || []; }
async function addRecent(handle) {
  if (!handle || !project) return;
  const list = await getRecent();
  const keep = [];
  for (const r of list) {
    let same = false;
    try { same = await r.handle.isSameEntry(handle); } catch { /* stale */ }
    if (!same) keep.push(r);
  }
  keep.unshift({ name: handle.name, title: project.meta.title, at: Date.now(), handle, id: project.meta.id });
  await idb.set('recent', keep.slice(0, 8));
}
async function removeRecent(entry) {
  const list = await getRecent();
  await idb.set('recent', list.filter((r) => r.at !== entry.at));
}

async function listBackups() {
  const keys = (await idb.keys()).filter((k) => String(k).startsWith('backup:'));
  const out = [];
  for (const k of keys) {
    const b = await idb.get(k);
    if (b) out.push(b);
  }
  return out.sort((a, b) => b.at - a.at);
}

/* =========================================================================
 * Opening and closing
 * ========================================================================= */
async function openProject(data, handle = null, { recoverCheck = true } = {}) {
  let p;
  try { p = normalize(typeof data === 'string' ? JSON.parse(data) : data); } catch (e) {
    toast(e instanceof SyntaxError ? 'That file isn’t valid Quire JSON.' : e.message);
    return false;
  }
  project = p;
  fileHandle = handle;
  fileName = handle ? handle.name : '';
  dirty = false; saveError = null; needsPerm = false; changeSeq = 0;
  const first = scenes()[0];
  ui.view = 'write';
  ui.sel = first ? { kind: 'scene', id: first.id } : { kind: 'book', id: null };
  ui.collapsed.clear();
  ui.entitySel = { characters: null, locations: null };
  ui.noteSel = project.notes[0]?.id || null;
  trackDaily();
  hideWelcome();
  renderAll();
  if (handle) addRecent(handle);
  idb.set('last', project.meta.id);

  // If the browser holds newer work than the file (e.g. a crash before the last write), offer it.
  if (recoverCheck) {
    const b = await idb.get(`backup:${project.meta.id}`);
    if (b && b.modified > project.meta.modified) {
      const yes = await confirmBox(
        `This browser has a newer copy of “${b.title}” (saved ${ago(b.at)}, ${plural(b.words, 'word')}) than the file you opened. Use the newer copy?`,
        { ok: 'Use newer copy', cancel: 'Keep file version' });
      if (yes) {
        const keep = fileHandle;
        await openProject(b.data, keep, { recoverCheck: false });
        touch();
      }
    }
  }
  return true;
}

async function openFile() {
  if (HAS_FS) {
    try {
      const [handle] = await window.showOpenFilePicker({ types: OPEN_TYPES, multiple: false });
      const file = await handle.getFile();
      await openProject(await file.text(), handle);
    } catch (e) {
      if (e.name !== 'AbortError') toast(`Couldn’t open: ${e.message}`);
    }
    return;
  }
  const input = h('input', { type: 'file', accept: '.quire,.json,application/json', style: { display: 'none' } });
  input.addEventListener('change', async () => {
    const f = input.files[0];
    input.remove();
    if (!f) return;
    if (await openProject(await f.text(), null)) fileName = f.name;
  });
  document.body.append(input);
  input.click();
}

async function openRecent(entry) {
  try {
    let perm = await entry.handle.queryPermission({ mode: 'readwrite' });
    if (perm !== 'granted') perm = await entry.handle.requestPermission({ mode: 'readwrite' });
    if (perm !== 'granted') { toast('Quire needs permission to open that file.'); return; }
    const file = await entry.handle.getFile();
    await openProject(await file.text(), entry.handle);
  } catch (e) {
    console.warn(e);
    toast('Couldn’t open that file. It may have been moved or renamed.');
    await removeRecent(entry);
    showWelcome();
  }
}

async function openHandle(handle) {
  try {
    if (handle.kind !== 'file') return;
    const file = await handle.getFile();
    await openProject(await file.text(), handle);
  } catch (e) { toast(`Couldn’t open: ${e.message}`); }
}

async function closeProject() {
  if (!project) return;
  await saveNow();
  if (!fileHandle && dirty && !(await confirmBox('This story hasn’t been saved to a file. It stays in this browser’s backups, but a file is safer. Close anyway?', { ok: 'Close', cancel: 'Keep writing' }))) return;
  project = null; fileHandle = null; fileName = '';
  showWelcome();
}

function createNew(title) {
  openProject(newProject(title), null, { recoverCheck: false }).then(() => {
    if (HAS_FS) saveAs();
    setTimeout(() => focusTitle(), 50);
  });
}

/* =========================================================================
 * Rendering
 * ========================================================================= */
function renderAll() {
  if (!project) return;
  flushEditors();
  renderSidebar();
  renderToolbar();
  renderView();
  renderInspector();
  renderStatus();
}

function renderSidebar() {
  if (!project) return;
  $('#projTitle').textContent = project.meta.title || 'Untitled story';
  $('#modes').replaceChildren(...VIEWS.map(([v, label, ic], i) => h('button', {
    class: `mode${ui.view === v ? ' active' : ''}`, onclick: () => setView(v),
  }, icon(ic), label, h('kbd', {}, `Alt ${i + 1}`))));

  const list = $('#sideList');
  const foot = $('#sideFoot');
  const scroll = list.scrollTop;
  if (ui.view === 'characters' || ui.view === 'locations') {
    list.replaceChildren(renderEntityList(ui.view));
    foot.replaceChildren(h('button', { class: 'btn ghost', onclick: () => addEntity(ui.view) }, icon('plus'), `New ${ENTITY[ui.view].one}`));
  } else if (ui.view === 'notes') {
    list.replaceChildren(renderNoteList());
    foot.replaceChildren(h('button', { class: 'btn ghost', onclick: addNote }, icon('plus'), 'New note'));
  } else {
    list.replaceChildren(renderBinder());
    foot.replaceChildren(
      h('button', { class: 'btn ghost', onclick: () => addChapter() }, icon('plus'), 'Chapter'),
      h('button', { class: 'btn ghost', onclick: () => addScene() }, icon('plus'), 'Scene'),
    );
  }
  list.scrollTop = scroll;
}
const softSidebar = debounce(() => { renderSidebar(); renderToolbar(); }, 200);

function isSel(kind, id) { return ui.view === 'write' && ui.sel.kind === kind && (kind === 'book' || ui.sel.id === id); }

function renderBinder() {
  const root = h('div', { class: 'binder' });
  root.append(h('div', { class: `b-item b-book${isSel('book') ? ' sel' : ''}`, onclick: () => select('book') },
    icon('book'), h('span', { class: 'b-label' }, 'Manuscript'), h('span', { class: 'b-count' }, fmt(totalWC()))));

  for (const ch of project.manuscript) {
    const closed = ui.collapsed.has(ch.id);
    const head = h('div', {
      class: `b-item b-chap${isSel('chapter', ch.id) ? ' sel' : ''}`, draggable: 'true', 'data-chapter': ch.id,
      onclick: () => select('chapter', ch.id),
      ondblclick: () => { select('chapter', ch.id); focusTitle(); },
      oncontextmenu: (e) => { e.preventDefault(); chapterMenu(e.clientX, e.clientY, ch); },
    },
    h('button', { class: `twisty${closed ? ' closed' : ''}`, title: closed ? 'Expand' : 'Collapse', onclick: (e) => { e.stopPropagation(); toggleCollapse(ch.id); } }, icon('chev')),
    h('span', { class: 'b-label' }, ch.title || 'Untitled chapter'),
    h('span', { class: 'b-count' }, fmt(chapterWC(ch))));
    head.addEventListener('dragstart', (e) => startDrag(e, 'chapter', ch.id, head));
    head.addEventListener('dragover', (e) => {
      if (drag?.type !== 'scene') return;
      e.preventDefault(); e.stopPropagation();
      const r = head.getBoundingClientRect();
      showLine(r.left + 30, r.bottom, r.width - 30);
      head._drop = () => moveScene(drag.id, ch.id, ch.children.length);
    });
    head.addEventListener('drop', (e) => { if (drag?.type === 'scene' && head._drop) { e.preventDefault(); e.stopPropagation(); head._drop(); } });

    const list = h('div', { class: 'b-scenes' });
    if (!closed) {
      for (const s of ch.children) {
        const item = h('div', {
          class: `b-item b-scene${isSel('scene', s.id) ? ' sel' : ''}`, draggable: 'true', 'data-scene': s.id,
          title: s.synopsis || '',
          onclick: () => select('scene', s.id),
          ondblclick: () => { select('scene', s.id); focusTitle(); },
          oncontextmenu: (e) => { e.preventDefault(); sceneMenu(e.clientX, e.clientY, s); },
        }, h('span', { class: `dot st-${s.status}`, title: s.status }), h('span', { class: 'b-label' }, s.title || 'Untitled scene'), h('span', { class: 'b-count' }, fmt(wcOf(s))));
        item.addEventListener('dragstart', (e) => startDrag(e, 'scene', s.id, item));
        list.append(item);
      }
      sceneDropZone(list, ch.id, '.b-scene');
    }
    root.append(h('div', { class: 'b-chapter', 'data-chapter': ch.id }, head, list));
  }

  root.addEventListener('dragover', (e) => {
    if (drag?.type !== 'chapter') return;
    e.preventDefault();
    const items = $$('.b-chapter', root);
    const idx = dropIndex(items, e.clientY);
    lineAt(root, items, idx);
    root._idx = idx;
  });
  root.addEventListener('drop', (e) => {
    if (drag?.type !== 'chapter') return;
    e.preventDefault();
    moveChapter(drag.id, root._idx ?? 0);
  });
  return root;
}

function renderEntityList(kind) {
  const cfg = ENTITY[kind];
  const wrap = h('div', {});
  wrap.append(h('div', { class: `b-item b-book${!ui.entitySel[kind] ? ' sel' : ''}`, onclick: () => { ui.entitySel[kind] = null; renderAll(); } },
    icon(cfg.icon), h('span', { class: 'b-label' }, `All ${cfg.title.toLowerCase()}`), h('span', { class: 'b-count' }, project[kind].length)));
  for (const e of project[kind]) {
    wrap.append(h('div', { class: `b-item b-ent${ui.entitySel[kind] === e.id ? ' sel' : ''}`, onclick: () => { ui.entitySel[kind] = e.id; renderAll(); } },
      h('span', { class: 'swatch', style: { background: e.color } }),
      h('span', { class: 'b-text' }, h('span', { class: 'b-label' }, e.name || `Unnamed ${cfg.one}`), e.role ? h('span', { class: 'b-sub' }, e.role) : null)));
  }
  return wrap;
}

function renderNoteList() {
  const wrap = h('div', {});
  if (!project.notes.length) wrap.append(h('div', { class: 'side-label' }, 'No notes yet'));
  for (const n of project.notes) {
    wrap.append(h('div', { class: `b-item${ui.noteSel === n.id ? ' sel' : ''}`, onclick: () => { flushEditors(); ui.noteSel = n.id; renderAll(); },
      oncontextmenu: (e) => { e.preventDefault(); popMenu(e.clientX, e.clientY, [{ label: 'Delete note', danger: true, run: () => deleteNote(n) }]); } },
    icon('note'), h('span', { class: 'b-label' }, n.title || 'Untitled note')));
  }
  return wrap;
}

function renderToolbar() {
  const crumb = $('#crumb');
  const parts = [];
  if (ui.view === 'write') {
    if (ui.sel.kind === 'book') parts.push('Manuscript');
    else if (ui.sel.kind === 'chapter') parts.push(chapterOf(ui.sel.id)?.title || 'Untitled chapter');
    else {
      const l = locateScene(ui.sel.id);
      if (l) parts.push(l.ch.title || 'Untitled chapter', l.scene.title || 'Untitled scene');
    }
  } else parts.push(VIEWS.find((v) => v[0] === ui.view)[1]);
  crumb.replaceChildren(...parts.flatMap((p, i) => (i ? [h('span', { class: 'sep' }, '/'), h('b', {}, p)] : [h('b', {}, p)])));
  $('#tbFocus').hidden = ui.view !== 'write';
  $('#tbInspector').hidden = ui.view !== 'write';
  const show = ui.view === 'write' && (isNarrow() ? ui.inspNarrow : prefs.inspector);
  $('#tbInspector').classList.toggle('on', show);
  document.body.classList.toggle('show-inspector', show);
}

function renderView() {
  const v = $('#view');
  v.scrollTop = 0;
  if (ui.view === 'write') v.replaceChildren(renderWrite());
  else if (ui.view === 'board') v.replaceChildren(renderBoard());
  else if (ui.view === 'plot') v.replaceChildren(renderPlot());
  else if (ui.view === 'characters' || ui.view === 'locations') v.replaceChildren(renderEntities(ui.view));
  else if (ui.view === 'notes') v.replaceChildren(renderNotes());
}

function setView(v) {
  flushEditors();
  ui.view = v;
  if (v !== 'write') document.body.classList.remove('focus-mode');
  document.body.classList.remove('side-open');
  renderAll();
}

function select(kind, id = null) {
  flushEditors();
  ui.view = 'write';
  ui.sel = { kind, id };
  if (kind === 'scene') {
    const l = locateScene(id);
    if (l) ui.collapsed.delete(l.ch.id);
  }
  document.body.classList.remove('side-open');
  renderAll();
}

function focusTitle() {
  const t = $('#view .doc-title') || $('#view .sheet-name');
  if (t) { t.focus(); t.select?.(); }
}

function toggleCollapse(id) {
  if (ui.collapsed.has(id)) ui.collapsed.delete(id); else ui.collapsed.add(id);
  renderSidebar();
}

/* ---------- binding helpers ---------- */
/** Text input bound to obj[key]; other inputs bound to the same value update live. */
function bound(obj, key, attrs = {}, after) {
  const tag = attrs.multiline ? 'textarea' : 'input';
  const a = { ...attrs };
  delete a.multiline;
  const bind = a['data-bind'] || `${obj.id}:${key}`;
  const el = h(tag, { ...a, value: obj[key] ?? '', 'data-bind': bind });
  el.addEventListener('input', () => {
    obj[key] = el.value;
    for (const o of $$(`[data-bind="${bind}"]`)) if (o !== el) o.value = el.value;
    touch();
    if (after) after(el.value);
  });
  if (tag === 'textarea') autosize(el);
  return el;
}
function autosize(ta) {
  const fit = () => { ta.style.height = 'auto'; ta.style.height = `${ta.scrollHeight + 2}px`; };
  ta.addEventListener('input', fit);
  requestAnimationFrame(fit);
  return ta;
}
function segmented(options, value, onPick) {
  const seg = h('div', { class: 'seg' });
  for (const [v, label, extra] of options) {
    seg.append(h('button', { class: v === value ? 'on' : '', type: 'button', onclick: () => {
      $$('button', seg).forEach((b) => b.classList.remove('on'));
      seg.children[options.findIndex((o) => o[0] === v)].classList.add('on');
      onPick(v);
    } }, extra || null, label));
  }
  return seg;
}
function selectBox(items, value, empty, onPick, newLabel) {
  const s = h('select', { class: 'field', onchange: () => onPick(s.value) },
    h('option', { value: '' }, empty),
    items.map((e) => h('option', { value: e.id, selected: e.id === value }, e.name || 'Unnamed')),
    newLabel ? h('option', { value: '__new' }, newLabel) : null);
  s.value = value || '';
  return s;
}
function sec(label, ...kids) { return h('div', { class: 'insp-sec' }, h('label', {}, label), ...kids); }
function avatar(e) {
  const initials = (e.name || '?').trim().split(/\s+/).slice(0, 2).map((w) => w[0]).join('').toUpperCase() || '?';
  return h('span', { class: 'avatar', style: { background: e.color } }, initials);
}

/* =========================================================================
 * Write view
 * ========================================================================= */
function renderWrite() {
  if (!project.manuscript.length) {
    return h('div', { class: 'empty' }, h('h2', {}, 'A blank page'), h('p', {}, 'Every story starts somewhere. Add a chapter to begin.'),
      h('button', { class: 'btn primary', onclick: () => addChapter() }, icon('plus'), 'Add a chapter'));
  }
  const page = h('div', { class: 'page' });
  const { kind, id } = ui.sel;

  if (kind === 'scene') {
    const l = locateScene(id);
    if (!l) { ui.sel = { kind: 'book', id: null }; return renderWrite(); }
    page.append(
      h('div', { class: 'eyebrow' }, l.ch.title || 'Untitled chapter'),
      bound(l.scene, 'title', { class: 'plain doc-title', placeholder: 'Untitled scene' }, softSidebar),
      editorFor(l.scene),
      h('div', { class: 'add-under' }, h('button', { class: 'btn ghost small', onclick: () => addScene(l.ch.id, l.index + 1) }, icon('plus'), 'New scene after this')),
    );
  } else if (kind === 'chapter') {
    const ch = chapterOf(id);
    if (!ch) { ui.sel = { kind: 'book', id: null }; return renderWrite(); }
    page.append(h('div', { class: 'eyebrow' }, `Chapter · ${plural(ch.children.length, 'scene')}`),
      bound(ch, 'title', { class: 'plain doc-title', placeholder: 'Untitled chapter' }, softSidebar));
    stackScenes(page, ch);
    page.append(h('div', { class: 'add-under' }, h('button', { class: 'btn ghost small', onclick: () => addScene(ch.id) }, icon('plus'), 'Add a scene')));
  } else {
    page.append(bound(project.meta, 'title', { class: 'plain doc-title book-title', placeholder: 'Untitled story', 'data-bind': 'meta:title' }, () => { $('#projTitle').textContent = project.meta.title; }),
      h('div', { class: 'book-author' }, project.meta.author ? `by ${project.meta.author}` : ''));
    for (const ch of project.manuscript) {
      page.append(h('button', { class: 'chap-head', onclick: () => select('chapter', ch.id) }, ch.title || 'Untitled chapter'));
      stackScenes(page, ch);
    }
    page.append(h('div', { class: 'add-under' }, h('button', { class: 'btn ghost small', onclick: () => addChapter() }, icon('plus'), 'Add a chapter')));
  }
  return page;
}
function stackScenes(page, ch) {
  ch.children.forEach((s) => {
    page.append(h('div', { class: 'scene-sep' }, h('button', { onclick: () => select('scene', s.id), title: 'Open this scene' }, s.title || 'Untitled scene')));
    page.append(editorFor(s));
  });
}

/* ---------- the prose editor ---------- */
const editors = new Map();   // element -> object with .content (scene or note)
const pending = new Set();   // editors with unflushed changes
const commitSoon = debounce(() => flushEditors(), 500);

function editorFor(obj, placeholder = 'Begin writing…') {
  const el = h('div', { class: 'prose', contenteditable: 'true', spellcheck: 'true', 'data-id': obj.id, 'data-placeholder': placeholder, role: 'textbox', 'aria-multiline': 'true' });
  el.innerHTML = obj.content || '<p><br></p>';
  editors.set(el, obj);
  const markEmpty = () => el.classList.toggle('is-empty', !el.textContent.trim() && el.querySelectorAll('p').length <= 1);
  markEmpty();
  el.addEventListener('input', () => {
    if (el.firstChild && el.firstChild.nodeType === 3) document.execCommand('formatBlock', false, 'p');
    pending.add(el);
    markEmpty();
    commitSoon();
    if (!dirty) { dirty = true; renderStatus(); }
    requestAnimationFrame(typewriterScroll);
  });
  el.addEventListener('beforeinput', smartType);
  el.addEventListener('paste', onPaste);
  el.addEventListener('focus', () => {
    if (obj.id.startsWith('s')) { ui.activeSceneId = obj.id; renderStatus(); }
  });
  el.addEventListener('keydown', (e) => {
    if (e.key === 'Tab') e.preventDefault();
    if (['ArrowUp', 'ArrowDown', 'PageUp', 'PageDown', 'Enter'].includes(e.key)) requestAnimationFrame(typewriterScroll);
  });
  const wrap = h('div', { class: 'prose-wrap' }, el);
  return wrap;
}

function flushEditors() {
  if (!pending.size) return;
  commitSoon.cancel();
  let changed = false;
  for (const el of pending) {
    const obj = editors.get(el);
    if (!obj) continue;
    const html = sanitizeHTML(el.innerHTML);
    if (html !== obj.content) {
      obj.content = html;
      if (obj.id.startsWith('s')) obj._wc = countWords(plainOf(obj));
      changed = true;
    }
  }
  pending.clear();
  for (const el of [...editors.keys()]) if (!el.isConnected) editors.delete(el);
  if (changed) {
    touch();
    if (['write', 'board', 'plot'].includes(ui.view)) refreshCounts();
  }
}

/** Update word counts in the binder and inspector without re-rendering anything being typed in. */
function refreshCounts() {
  for (const el of $$('#sideList .b-scene')) {
    const s = locateScene(el.dataset.scene)?.scene;
    if (s) el.querySelector('.b-count').textContent = fmt(wcOf(s));
  }
  for (const el of $$('#sideList .b-chap')) {
    const ch = chapterOf(el.dataset.chapter);
    if (ch) el.querySelector('.b-count').textContent = fmt(chapterWC(ch));
  }
  const book = $('#sideList .b-book .b-count');
  if (book) book.textContent = fmt(totalWC());
  const iw = $('#inspector [data-live="wc"]');
  if (iw) {
    const s = ui.sel.kind === 'scene' && locateScene(ui.sel.id)?.scene;
    if (s) iw.textContent = plural(wcOf(s), 'word');
  }
}

function prevChars(n) {
  const sel = getSelection();
  if (!sel.rangeCount) return '';
  const r = sel.getRangeAt(0);
  if (!r.collapsed || r.startContainer.nodeType !== 3) return '';
  return r.startContainer.data.slice(Math.max(0, r.startOffset - n), r.startOffset);
}
function smartType(e) {
  if (!prefs.smart || e.inputType !== 'insertText' || !e.data || e.data.length !== 1) return;
  const d = e.data;
  if (d === '"' || d === "'") {
    const prev = prevChars(1);
    const opening = !prev || /[\s([{—–“‘]/.test(prev);
    const ch = d === '"' ? (opening ? '“' : '”') : (opening ? '‘' : '’');
    e.preventDefault();
    document.execCommand('insertText', false, ch);
  } else if (d === '-' && prevChars(1) === '-') {
    e.preventDefault();
    document.execCommand('delete');
    document.execCommand('insertText', false, '—');
  } else if (d === '.' && prevChars(2) === '..') {
    e.preventDefault();
    document.execCommand('delete'); document.execCommand('delete');
    document.execCommand('insertText', false, '…');
  }
}
function onPaste(e) {
  e.preventDefault();
  const html = e.clipboardData.getData('text/html');
  const text = e.clipboardData.getData('text/plain');
  if (html) {
    const clean = sanitizeHTML(html);
    const paras = paragraphsOf(clean);
    if (paras.length === 1) document.execCommand('insertHTML', false, paras[0].innerHTML);
    else document.execCommand('insertHTML', false, clean);
  } else if (text) {
    if (!/\n/.test(text)) document.execCommand('insertText', false, text);
    else document.execCommand('insertHTML', false, textToHTML(text));
  }
}

function typewriterScroll() {
  if (!prefs.typewriter || !document.body.classList.contains('focus-mode')) return;
  const sel = getSelection();
  if (!sel.rangeCount || !document.activeElement?.classList.contains('prose')) return;
  const r = sel.getRangeAt(0);
  let rect = r.getClientRects()[0];
  if (!rect) {
    const n = r.startContainer.nodeType === 1 ? r.startContainer : r.startContainer.parentElement;
    rect = n.getBoundingClientRect();
  }
  const view = $('#view');
  const vr = view.getBoundingClientRect();
  const delta = rect.top - (vr.top + vr.height * 0.42);
  if (Math.abs(delta) > 6) view.scrollTop += delta;
}

let curPara = null;
document.addEventListener('selectionchange', () => {
  const sel = getSelection();
  let n = sel.anchorNode, p = null;
  while (n && n !== document.body) {
    if (n.nodeType === 1 && n.tagName === 'P' && n.parentElement?.classList.contains('prose')) { p = n; break; }
    n = n.parentNode;
  }
  if (p !== curPara) {
    curPara?.classList.remove('current');
    p?.classList.add('current');
    curPara = p;
  }
});

/* =========================================================================
 * Inspector
 * ========================================================================= */
function renderInspector() {
  const ins = $('#inspector');
  if (ui.view !== 'write') { ins.replaceChildren(); return; }
  const { kind, id } = ui.sel;
  if (kind === 'scene') {
    const l = locateScene(id);
    ins.replaceChildren(l ? inspectScene(l.scene, l.ch) : h('div'));
  } else if (kind === 'chapter') {
    const ch = chapterOf(id);
    ins.replaceChildren(ch ? inspectChapter(ch) : h('div'));
  } else ins.replaceChildren(inspectBook());
}

function chipToggles(items, selected, onToggle, emptyText, onAdd) {
  const box = h('div', { class: 'chips' });
  for (const e of items) {
    box.append(h('button', { class: `chip${selected.includes(e.id) ? ' on' : ''}`, onclick: () => onToggle(e.id) },
      h('span', { class: 'swatch', style: { background: e.color } }), e.name || 'Unnamed'));
  }
  box.append(h('button', { class: 'chip add', onclick: onAdd, title: emptyText }, icon('plus'), items.length ? '' : emptyText));
  return box;
}
const toggleIn = (arr, id) => { const i = arr.indexOf(id); if (i >= 0) arr.splice(i, 1); else arr.push(id); };

function inspectScene(s, ch) {
  const rerender = () => { touch(); renderInspector(); };
  const text = plainOf(s);
  const suggested = project.characters.filter((c) => !s.characters.includes(c.id) && mentions('characters', c, text));
  const suggestedLoc = !s.location && project.locations.find((l) => mentions('locations', l, text));

  return h('div', { class: 'insp' },
    sec('Scene', bound(s, 'title', { class: 'field', placeholder: 'Untitled scene' }, softSidebar)),
    sec('Status', segmented(STATUSES.map(([v, l]) => [v, l, h('span', { class: `dot st-${v}` })]), s.status, (v) => { s.status = v; touch(); renderSidebar(); })),
    sec('Synopsis', bound(s, 'synopsis', { multiline: true, class: 'field', rows: 2, placeholder: 'What happens here, in a line or two' })),
    sec('Point of view', selectBox(project.characters, s.pov, 'No POV character', (v) => {
      s.pov = v;
      if (v && !s.characters.includes(v)) s.characters.push(v);
      rerender();
    })),
    sec('Characters in scene',
      chipToggles(project.characters, s.characters, (id) => { toggleIn(s.characters, id); if (!s.characters.includes(s.pov)) s.pov = ''; rerender(); }, 'New character', () => quickAdd('characters', s)),
      suggested.length ? h('div', { class: 'hint' }, 'Mentioned in the text: ', h('div', { class: 'chips', style: { marginTop: '6px' } }, suggested.map((c) => h('button', { class: 'chip suggest', onclick: () => { s.characters.push(c.id); rerender(); } }, icon('plus'), c.name)))) : null),
    sec('Setting',
      selectBox(project.locations, s.location, 'No place set', (v) => { if (v === '__new') { renderInspector(); quickAdd('locations', s); return; } s.location = v; rerender(); }, 'New place…'),
      suggestedLoc ? h('div', { class: 'hint' }, h('button', { class: 'chip suggest', onclick: () => { s.location = suggestedLoc.id; rerender(); } }, icon('plus'), `${suggestedLoc.name} is mentioned`)) : null),
    sec('Plot threads', chipToggles(project.threads, s.threads, (id) => { toggleIn(s.threads, id); rerender(); }, 'New plot thread', () => quickAdd('threads', s))),
    sec('When', bound(s, 'when', { class: 'field', placeholder: 'Day 3, before dawn' })),
    sec('Scene notes', bound(s, 'notes', { multiline: true, class: 'field', rows: 3, placeholder: 'Reminders, research, things to fix' })),
    h('div', { class: 'insp-foot' }, h('span', { 'data-live': 'wc' }, plural(wcOf(s), 'word')),
      h('button', { class: 'btn ghost small danger', onclick: () => deleteScene(s) }, 'Delete scene')),
  );
}

function inspectChapter(ch) {
  const words = chapterWC(ch);
  return h('div', { class: 'insp' },
    sec('Chapter', bound(ch, 'title', { class: 'field', placeholder: 'Untitled chapter' }, softSidebar)),
    h('div', { class: 'stat-row' },
      h('div', { class: 'stat' }, h('b', {}, fmt(words)), h('span', {}, 'words')),
      h('div', { class: 'stat' }, h('b', {}, ch.children.length), h('span', {}, 'scenes'))),
    sec('Summary', bound(ch, 'synopsis', { multiline: true, class: 'field', rows: 3, placeholder: 'What this chapter does for the story' })),
    sec('Chapter notes', bound(ch, 'notes', { multiline: true, class: 'field', rows: 3, placeholder: 'Notes' })),
    h('div', { class: 'insp-foot' }, h('span', {}, ''), h('button', { class: 'btn ghost small danger', onclick: () => deleteChapter(ch) }, 'Delete chapter')),
  );
}

function inspectBook() {
  const m = project.meta;
  const total = totalWC();
  const days = [];
  for (let i = 13; i >= 0; i--) {
    const d = new Date(); d.setDate(d.getDate() - i);
    const rec = project.stats.daily[dayKey(d)];
    days.push(rec ? Math.max(0, rec.end - rec.start) : 0);
  }
  const max = Math.max(1, ...days, m.dailyGoal || 0);
  const numField = (key) => {
    const el = h('input', { class: 'field', type: 'number', min: '0', step: '500', value: m[key] || 0 });
    el.addEventListener('input', () => { m[key] = Math.max(0, parseInt(el.value, 10) || 0); touch(); });
    return el;
  };
  return h('div', { class: 'insp' },
    sec('Title', bound(m, 'title', { class: 'field', placeholder: 'Untitled story', 'data-bind': 'meta:title' }, () => { $('#projTitle').textContent = m.title; })),
    sec('Author', bound(m, 'author', { class: 'field', placeholder: 'Your name' }, (v) => { const a = $('.book-author'); if (a) a.textContent = v ? `by ${v}` : ''; })),
    h('div', { class: 'stat-row' },
      h('div', { class: 'stat' }, h('b', {}, fmt(total)), h('span', {}, m.wordGoal ? `of ${fmt(m.wordGoal)} words` : 'words')),
      h('div', { class: 'stat' }, h('b', {}, fmt(todayWords())), h('span', {}, 'words today'))),
    sec('Last two weeks', h('div', { class: 'spark' }, days.map((n) => h('i', { class: n ? '' : 'zero', style: { height: `${Math.max(3, (n / max) * 100)}%` }, title: plural(n, 'word') }))),
      h('div', { class: 'spark-legend' }, h('span', {}, '2 weeks ago'), h('span', {}, 'today'))),
    sec('Word goal for the book', numField('wordGoal')),
    sec('Daily goal', numField('dailyGoal')),
  );
}

/* =========================================================================
 * Board view (index cards by chapter)
 * ========================================================================= */
function renderBoard() {
  const board = h('div', { class: 'board' });
  for (const ch of project.manuscript) {
    const cards = h('div', { class: 'cards' }, ch.children.map((s) => sceneCard(s)));
    sceneDropZone(cards, ch.id, '.card');
    board.append(h('div', { class: 'col' },
      h('div', { class: 'col-head' }, bound(ch, 'title', { class: 'plain col-title', placeholder: 'Untitled chapter' }, softSidebar), h('span', { class: 'b-count' }, fmt(chapterWC(ch)))),
      cards,
      h('button', { class: 'add-card', onclick: () => { addScene(ch.id, undefined, { stay: true }); } }, icon('plus'), 'Scene')));
  }
  board.append(h('button', { class: 'btn ghost add-col', onclick: () => addChapter({ stay: true }) }, icon('plus'), 'Chapter'));
  return board;
}
function sceneCard(s) {
  const pov = s.pov && byId(project.characters, s.pov);
  const loc = s.location && byId(project.locations, s.location);
  const excerpt = !s.synopsis && plainOf(s).trim().slice(0, 180);
  const card = h('div', { class: 'card', draggable: 'true', 'data-scene': s.id, style: { borderLeftColor: pov ? pov.color : '' },
    onclick: () => select('scene', s.id), oncontextmenu: (e) => { e.preventDefault(); sceneMenu(e.clientX, e.clientY, s); } },
  h('div', { class: 'card-title' }, s.title || 'Untitled scene'),
  s.synopsis ? h('div', { class: 'card-syn' }, s.synopsis) : excerpt ? h('div', { class: 'card-syn excerpt' }, `${excerpt}…`) : null,
  h('div', { class: 'card-foot' },
    h('span', { class: 'pill' }, h('span', { class: `dot st-${s.status}` }), STATUSES.find((x) => x[0] === s.status)?.[1] || s.status),
    h('span', { class: 'card-who' }, [pov ? pov.name.split(/\s+/)[0] : null, loc ? loc.name : null].filter(Boolean).join(' · ')),
    h('span', { class: 'grow' }), fmt(wcOf(s))));
  card.addEventListener('dragstart', (e) => startDrag(e, 'scene', s.id, card));
  return card;
}

/* =========================================================================
 * Story map: threads, characters and places across every scene
 * ========================================================================= */
function renderPlot() {
  const wrap = h('div', { class: 'plot-wrap' });
  const all = scenes();
  wrap.append(h('p', { class: 'plot-intro' }, 'Every scene, left to right. Click a cell to mark which plot threads a scene advances, who appears in it (click twice for point of view), and where it happens.'));
  if (!all.length) {
    wrap.append(h('div', { class: 'empty' }, h('h2', {}, 'No scenes yet'), h('p', {}, 'Write a scene or two and the map fills in.')));
    return wrap;
  }
  const cols = all.length;
  const table = h('table', { class: 'grid' });
  const thead = h('thead', {},
    h('tr', {}, h('th', { class: 'corner', rowspan: '2' }, 'Story map'),
      project.manuscript.filter((c) => c.children.length).map((c) => h('th', { class: 'ch-th', colspan: String(c.children.length) }, c.title || 'Untitled'))),
    h('tr', {}, all.map((s) => h('th', { class: 'sc-th' }, h('button', { title: s.title, onclick: () => select('scene', s.id) }, s.title || 'Untitled')))));
  const tbody = h('tbody');
  const section = (label) => h('tr', { class: 'section' }, h('th', {}, label), h('td', { colspan: String(cols) }));
  const rowHead = (e, onClick) => h('th', { class: 'rowhead' }, h('div', {}, h('span', { class: 'swatch', style: { background: e.color } }),
    h('button', { class: 'plain', style: { width: 'auto', cursor: 'pointer' }, onclick: onClick }, e.name || 'Unnamed')));
  const redraw = () => { touch(); const sx = $('.grid-scroll')?.scrollLeft; $('#view').replaceChildren(renderPlot()); const g = $('.grid-scroll'); if (g) g.scrollLeft = sx; };

  tbody.append(section('Plot threads'));
  for (const t of project.threads) {
    tbody.append(h('tr', {}, rowHead(t, () => $(`#thread-${t.id} input`)?.focus()),
      all.map((s) => h('td', { class: 'cell', title: `${t.name} · ${s.title}`, onclick: () => { toggleIn(s.threads, t.id); redraw(); } },
        s.threads.includes(t.id) ? h('i', { style: { background: t.color } }) : null))));
  }
  tbody.append(h('tr', { class: 'add-row' }, h('td', { colspan: String(cols + 1) }, h('button', { class: 'btn ghost small', onclick: () => addThread() }, icon('plus'), 'Plot thread'))));

  tbody.append(section('Characters'));
  for (const c of project.characters) {
    tbody.append(h('tr', {}, rowHead(c, () => { ui.view = 'characters'; ui.entitySel.characters = c.id; renderAll(); }),
      all.map((s) => {
        const isPov = s.pov === c.id, isIn = s.characters.includes(c.id);
        return h('td', { class: 'cell', title: `${c.name} · ${s.title}${isPov ? ' (POV)' : ''}`, onclick: () => {
          if (isPov) { s.pov = ''; toggleIn(s.characters, c.id); } else if (isIn) s.pov = c.id; else s.characters.push(c.id);
          redraw();
        } }, isPov ? h('i', { class: 'pov', style: { background: c.color, color: c.color } }) : isIn ? h('i', { style: { background: c.color } }) : null);
      })));
  }
  tbody.append(h('tr', { class: 'add-row' }, h('td', { colspan: String(cols + 1) }, h('button', { class: 'btn ghost small', onclick: () => addEntity('characters') }, icon('plus'), 'Character'))));

  tbody.append(section('Places'));
  for (const l of project.locations) {
    tbody.append(h('tr', {}, rowHead(l, () => { ui.view = 'locations'; ui.entitySel.locations = l.id; renderAll(); }),
      all.map((s) => h('td', { class: 'cell', title: `${l.name} · ${s.title}`, onclick: () => { s.location = s.location === l.id ? '' : l.id; redraw(); } },
        s.location === l.id ? h('i', { class: 'ring', style: { color: l.color } }) : null))));
  }
  tbody.append(h('tr', { class: 'add-row' }, h('td', { colspan: String(cols + 1) }, h('button', { class: 'btn ghost small', onclick: () => addEntity('locations') }, icon('plus'), 'Place'))));

  table.append(thead, tbody);
  wrap.append(h('div', { class: 'grid-scroll' }, table));
  wrap.append(h('div', { class: 'legend' },
    h('span', { class: 'cell' }, h('i'), 'in the scene / advances the thread'),
    h('span', { class: 'cell' }, h('i', { class: 'pov' }), 'point of view'),
    h('span', { class: 'cell' }, h('i', { class: 'ring' }), 'setting')));

  if (project.threads.length) {
    wrap.append(h('h3', { class: 'sec-label', style: { marginTop: '34px' } }, 'Plot threads'));
    wrap.append(h('div', { class: 'threads' }, project.threads.map((t) => {
      const count = all.filter((s) => s.threads.includes(t.id)).length;
      return h('div', { class: 'thread-card', id: `thread-${t.id}` },
        h('div', { class: 'row' }, colorDot(t, redraw), bound(t, 'name', { class: 'plain', placeholder: 'Thread name' }),
          h('button', { class: 'icon-btn', title: 'Delete thread', onclick: () => deleteThread(t) }, icon('x'))),
        bound(t, 'summary', { multiline: true, class: 'field', rows: 2, placeholder: 'What is this thread about? How does it resolve?' }),
        h('span', { class: 'hint', style: { marginTop: 0 } }, `${plural(count, 'scene')}`));
    })));
  }
  return wrap;
}
function colorDot(obj, after) {
  return h('button', { class: 'swatch', title: 'Change colour', style: { background: obj.color, border: 0, width: '14px', height: '14px', padding: 0 },
    onclick: (e) => {
      const r = e.currentTarget.getBoundingClientRect();
      popColors(r.left, r.bottom + 6, obj.color, (c) => { obj.color = c; touch(); after(); });
    } });
}
function popColors(x, y, current, onPick) {
  closeMenu();
  const m = h('div', { class: 'menu', style: { minWidth: '0' } }, h('div', { class: 'colors', style: { margin: '4px' } },
    COLORS.map((c) => h('button', { class: c === current ? 'on' : '', style: { background: c }, onclick: () => { closeMenu(); onPick(c); } }))));
  placeMenu(m, x, y);
}

/* =========================================================================
 * Characters and places
 * ========================================================================= */
function renderEntities(kind) {
  const cfg = ENTITY[kind];
  const ent = ui.entitySel[kind] && byId(project[kind], ui.entitySel[kind]);
  if (ent) return entitySheet(kind, ent);

  const frag = h('div', {});
  frag.append(h('div', { class: 'gallery-head' }, h('h1', {}, cfg.title), h('button', { class: 'btn', onclick: () => addEntity(kind) }, icon('plus'), `New ${cfg.one}`)));
  if (!project[kind].length) {
    frag.append(h('div', { class: 'empty', style: { marginTop: '10vh' } },
      h('h2', {}, kind === 'characters' ? 'Who is this story about?' : 'Where does it happen?'),
      h('p', {}, kind === 'characters'
        ? 'Keep a sheet for everyone who matters: what they want, what stands in the way, how they talk. Tag them in scenes to see where they appear.'
        : 'Give each place a sheet with its mood, sights and sounds. Set scenes there to keep track of where the story goes.'),
      h('button', { class: 'btn primary', onclick: () => addEntity(kind) }, icon('plus'), `Add a ${cfg.one}`)));
    return frag;
  }
  frag.append(h('div', { class: 'gallery' }, project[kind].map((e) => {
    const n = appearances(kind, e).length;
    return h('button', { class: 'ent-card', onclick: () => { ui.entitySel[kind] = e.id; renderAll(); } },
      avatar(e), h('b', {}, e.name || `Unnamed ${cfg.one}`), h('span', { class: 'role' }, [e.role, n ? plural(n, 'scene') : null].filter(Boolean).join(' · ')),
      e.summary ? h('p', {}, e.summary) : null);
  })));
  return frag;
}

function appearances(kind, e) {
  const out = [];
  for (const ch of project.manuscript) {
    for (const s of ch.children) {
      let how = null;
      if (kind === 'characters') {
        if (s.pov === e.id) how = 'Point of view';
        else if (s.characters.includes(e.id)) how = 'In scene';
      } else if (s.location === e.id) how = 'Setting';
      if (!how && mentions(kind, e, plainOf(s))) how = 'Mentioned';
      if (how) out.push({ s, ch, how });
    }
  }
  return out;
}

function entitySheet(kind, e) {
  const cfg = ENTITY[kind];
  const sheet = h('div', { class: 'sheet' });
  let av = avatar(e);
  const refreshHead = () => { const a = avatar(e); av.replaceWith(a); av = a; };
  const colors = h('div', { class: 'colors' }, COLORS.map((c) => h('button', { class: c === e.color ? 'on' : '', style: { background: c }, title: 'Colour',
    onclick: (ev) => { e.color = c; $$('button', colors).forEach((b) => b.classList.remove('on')); ev.currentTarget.classList.add('on'); touch(); renderSidebar(); renderView(); } })));
  sheet.append(h('div', { class: 'sheet-head' }, av, h('div', { style: { flex: '1', minWidth: '0' } },
    bound(e, 'name', { class: 'plain sheet-name', placeholder: `Unnamed ${cfg.one}` }, () => { refreshHead(); softSidebar(); }),
    bound(e, 'role', { class: 'plain sheet-role', placeholder: cfg.rolePh }, softSidebar),
    colors)));

  sheet.append(h('div', { class: 'sheet-sec' }, h('label', { class: 'sec-label' }, 'In a sentence'),
    bound(e, 'summary', { multiline: true, class: 'field serif-area', rows: 2, placeholder: kind === 'characters' ? 'Who are they, really?' : 'What is this place like?' })));

  const fields = h('div', { class: 'fields' });
  const drawFields = () => {
    fields.replaceChildren();
    e.fields.forEach((f, i) => {
      const key = h('input', { class: 'plain key', value: f.k, placeholder: 'Field' });
      key.addEventListener('input', () => { f.k = key.value; touch(); });
      const val = autosize(h('textarea', { class: 'field', rows: 1, value: f.v, placeholder: '…' }));
      val.addEventListener('input', () => { f.v = val.value; touch(); });
      fields.append(key, val, h('button', { class: 'icon-btn x', title: 'Remove field', onclick: () => { e.fields.splice(i, 1); touch(); drawFields(); } }, icon('x')));
    });
  };
  drawFields();
  sheet.append(h('div', { class: 'sheet-sec' }, h('label', { class: 'sec-label' }, 'Details'), fields,
    h('button', { class: 'btn ghost small', style: { marginTop: '8px' }, onclick: () => { e.fields.push({ k: '', v: '' }); touch(); drawFields(); $$('.key', fields).pop()?.focus(); } }, icon('plus'), 'Add a field')));

  sheet.append(h('div', { class: 'sheet-sec' }, h('label', { class: 'sec-label' }, 'Also known as'),
    bound(e, 'aliases', { class: 'field', placeholder: 'Nicknames or titles, separated by commas. Used to spot mentions in your scenes.' })));

  sheet.append(h('div', { class: 'sheet-sec' }, h('label', { class: 'sec-label' }, 'Notes'),
    bound(e, 'notes', { multiline: true, class: 'field serif-area', rows: 5, placeholder: 'Backstory, research, ideas…' })));

  const apps = appearances(kind, e);
  sheet.append(h('div', { class: 'sheet-sec' }, h('label', { class: 'sec-label' }, `Appears in ${plural(apps.length, 'scene')}`),
    apps.length ? h('div', { class: 'appear' }, apps.map(({ s, ch, how }) => h('button', { onclick: () => select('scene', s.id) },
      h('span', { class: `dot st-${s.status}` }), h('span', {}, s.title || 'Untitled scene'), h('span', { class: 'muted' }, ch.title), h('span', { class: 'how' }, how))))
      : h('p', { class: 'muted', style: { margin: 0 } }, kind === 'characters' ? 'Not in any scene yet. Tag them from a scene’s details panel, or on the Story map.' : 'No scenes are set here yet.')));

  sheet.append(h('div', { class: 'sheet-foot' },
    h('button', { class: 'btn ghost small', onclick: () => { ui.entitySel[kind] = null; renderAll(); } }, `← All ${cfg.title.toLowerCase()}`),
    h('button', { class: 'btn ghost small danger', onclick: () => deleteEntity(kind, e) }, `Delete ${cfg.one}`)));
  return sheet;
}

/* =========================================================================
 * Notes
 * ========================================================================= */
function renderNotes() {
  const n = ui.noteSel && byId(project.notes, ui.noteSel);
  if (!n) {
    return h('div', { class: 'empty' }, h('h2', {}, 'Notes'), h('p', {}, 'Research, world rules, stray lines of dialogue, things you’ll need later. Anything that isn’t the manuscript.'),
      h('button', { class: 'btn primary', onclick: addNote }, icon('plus'), 'New note'));
  }
  return h('div', { class: 'page' }, h('div', { class: 'eyebrow' }, 'Note'),
    bound(n, 'title', { class: 'plain doc-title', placeholder: 'Untitled note' }, softSidebar),
    editorFor(n, 'Write anything…'));
}

/* =========================================================================
 * Structural edits
 * ========================================================================= */
function currentChapter() {
  if (ui.sel.kind === 'chapter') return chapterOf(ui.sel.id);
  if (ui.sel.kind === 'scene') return locateScene(ui.sel.id)?.ch;
  return project.manuscript[project.manuscript.length - 1];
}
function addScene(chId, index, { stay = false } = {}) {
  flushEditors();
  let ch = chId ? chapterOf(chId) : currentChapter();
  if (!ch) { ch = mkChapter('Chapter One'); project.manuscript.push(ch); }
  if (index == null) index = ui.sel.kind === 'scene' && ch === locateScene(ui.sel.id)?.ch ? locateScene(ui.sel.id).index + 1 : ch.children.length;
  const s = mkScene();
  ch.children.splice(index, 0, s);
  ui.collapsed.delete(ch.id);
  touch();
  if (stay) { renderAll(); return s; }
  select('scene', s.id);
  setTimeout(focusTitle, 30);
  return s;
}
const NUM_WORDS = ['One', 'Two', 'Three', 'Four', 'Five', 'Six', 'Seven', 'Eight', 'Nine', 'Ten', 'Eleven', 'Twelve', 'Thirteen', 'Fourteen', 'Fifteen', 'Sixteen', 'Seventeen', 'Eighteen', 'Nineteen', 'Twenty'];
function addChapter({ stay = false, after } = {}) {
  flushEditors();
  const n = project.manuscript.length + 1;
  const ch = mkChapter(`Chapter ${NUM_WORDS[n - 1] || n}`);
  ch.children.push(mkScene());
  const i = after ? project.manuscript.indexOf(after) + 1 : project.manuscript.length;
  project.manuscript.splice(i, 0, ch);
  touch();
  if (stay) { renderAll(); return; }
  select('chapter', ch.id);
  setTimeout(focusTitle, 30);
}
async function deleteScene(s) {
  const l = locateScene(s.id);
  if (!l) return;
  if (wcOf(s) > 0 && !(await confirmBox(`Delete “${s.title || 'Untitled scene'}” and its ${plural(wcOf(s), 'word')}? Snapshots keep older versions for a while.`))) return;
  l.ch.children.splice(l.index, 1);
  touch();
  if (ui.sel.kind === 'scene' && ui.sel.id === s.id) {
    const next = l.ch.children[l.index] || l.ch.children[l.index - 1];
    ui.sel = next ? { kind: 'scene', id: next.id } : { kind: 'chapter', id: l.ch.id };
  }
  renderAll();
}
async function deleteChapter(ch) {
  const words = chapterWC(ch);
  if ((words > 0 || ch.children.length > 1) && !(await confirmBox(`Delete “${ch.title}” with ${plural(ch.children.length, 'scene')} and ${plural(words, 'word')}?`))) return;
  const i = project.manuscript.indexOf(ch);
  project.manuscript.splice(i, 1);
  touch();
  ui.sel = { kind: 'book', id: null };
  renderAll();
}
function duplicateScene(s) {
  const l = locateScene(s.id);
  const copy = { ...JSON.parse(JSON.stringify(s, (k, v) => (k.startsWith('_') ? undefined : v))), id: uid('s'), title: `${s.title || 'Untitled scene'} (copy)` };
  l.ch.children.splice(l.index + 1, 0, copy);
  touch();
  select('scene', copy.id);
}
function moveScene(id, toChId, index) {
  const l = locateScene(id);
  const to = chapterOf(toChId);
  if (!l || !to) return;
  l.ch.children.splice(l.index, 1);
  if (l.ch === to && l.index < index) index--;
  to.children.splice(clamp(index, 0, to.children.length), 0, l.scene);
  touch();
  renderAll();
}
function moveChapter(id, index) {
  const i = project.manuscript.findIndex((c) => c.id === id);
  if (i < 0) return;
  const [ch] = project.manuscript.splice(i, 1);
  if (i < index) index--;
  project.manuscript.splice(clamp(index, 0, project.manuscript.length), 0, ch);
  touch();
  renderAll();
}
function addEntity(kind) {
  flushEditors();
  const e = mkEntity(kind);
  project[kind].push(e);
  ui.view = kind;
  ui.entitySel[kind] = e.id;
  touch();
  renderAll();
  setTimeout(focusTitle, 30);
}
async function deleteEntity(kind, e) {
  if (!(await confirmBox(`Delete ${e.name || `this ${ENTITY[kind].one}`}? Scenes stay, but they lose the tag.`))) return;
  project[kind] = project[kind].filter((x) => x !== e);
  for (const s of scenes()) {
    if (kind === 'characters') { s.characters = s.characters.filter((id) => id !== e.id); if (s.pov === e.id) s.pov = ''; } else if (s.location === e.id) s.location = '';
  }
  ui.entitySel[kind] = null;
  touch();
  renderAll();
}
function addThread() {
  const t = mkThread(`Thread ${project.threads.length + 1}`);
  project.threads.push(t);
  touch();
  if (ui.view !== 'plot') { ui.view = 'plot'; }
  renderAll();
  setTimeout(() => { const el = $(`#thread-${t.id} input`); if (el) { el.scrollIntoView({ block: 'center' }); el.focus(); el.select(); } }, 30);
}
async function deleteThread(t) {
  if (!(await confirmBox(`Delete the plot thread “${t.name}”? Scenes stay, but they lose the tag.`))) return;
  project.threads = project.threads.filter((x) => x !== t);
  for (const s of scenes()) s.threads = s.threads.filter((id) => id !== t.id);
  touch();
  renderAll();
}
function addNote() {
  flushEditors();
  const n = mkNote();
  project.notes.unshift(n);
  ui.view = 'notes';
  ui.noteSel = n.id;
  touch();
  renderAll();
  setTimeout(focusTitle, 30);
}
async function deleteNote(n) {
  if (textOf(n.content).trim() && !(await confirmBox(`Delete the note “${n.title || 'Untitled note'}”?`))) return;
  project.notes = project.notes.filter((x) => x !== n);
  if (ui.noteSel === n.id) ui.noteSel = project.notes[0]?.id || null;
  touch();
  renderAll();
}

/** Create a character, place or thread by name and tag it on the scene, without leaving the page. */
async function quickAdd(kind, s) {
  const label = kind === 'threads' ? 'plot thread' : ENTITY[kind].one;
  const name = await promptBox(`New ${label}`, kind === 'threads' ? 'e.g. The missing log book' : 'Name');
  if (!name) return;
  if (kind === 'threads') {
    const t = mkThread(name);
    project.threads.push(t);
    s.threads.push(t.id);
  } else {
    const e = mkEntity(kind, name);
    project[kind].push(e);
    if (kind === 'characters') s.characters.push(e.id); else s.location = e.id;
  }
  touch();
  renderInspector();
  toast(`Added ${name}. Fill in the details under ${kind === 'threads' ? 'Story map' : ENTITY[kind].title}.`);
}

function sceneMenu(x, y, s) {
  const l = locateScene(s.id);
  popMenu(x, y, [
    { label: 'Open', run: () => select('scene', s.id) },
    { label: 'Rename', run: () => { select('scene', s.id); focusTitle(); } },
    { label: 'New scene after', run: () => addScene(l.ch.id, l.index + 1) },
    { label: 'Duplicate', run: () => duplicateScene(s) },
    '-',
    { head: 'Status' },
    ...STATUSES.map(([v, label]) => ({ label: `${s.status === v ? '✓ ' : ''}${label}`, run: () => { s.status = v; touch(); renderAll(); } })),
    '-',
    { label: 'Delete scene', danger: true, run: () => deleteScene(s) },
  ]);
}
function chapterMenu(x, y, ch) {
  popMenu(x, y, [
    { label: 'Open', run: () => select('chapter', ch.id) },
    { label: 'Rename', run: () => { select('chapter', ch.id); focusTitle(); } },
    { label: 'Add scene', run: () => addScene(ch.id, ch.children.length) },
    { label: 'New chapter after', run: () => addChapter({ after: ch }) },
    '-',
    { label: 'Delete chapter', danger: true, run: () => deleteChapter(ch) },
  ]);
}

/* =========================================================================
 * Drag and drop (binder and board)
 * ========================================================================= */
let drag = null;
const dropline = () => $('#dropline');
function startDrag(e, type, id, el) {
  e.stopPropagation();
  flushEditors();
  drag = { type, id };
  e.dataTransfer.effectAllowed = 'move';
  e.dataTransfer.setData('text/plain', id);
  el.classList.add('dragging');
  el.addEventListener('dragend', () => { el.classList.remove('dragging'); drag = null; hideLine(); }, { once: true });
}
function dropIndex(items, y) {
  for (let i = 0; i < items.length; i++) {
    const r = items[i].getBoundingClientRect();
    if (y < r.top + r.height / 2) return i;
  }
  return items.length;
}
function showLine(left, top, width) {
  const l = dropline();
  Object.assign(l.style, { display: 'block', left: `${left}px`, top: `${top - 1}px`, width: `${width}px` });
}
function hideLine() { dropline().style.display = 'none'; }
function lineAt(container, items, idx) {
  const cr = container.getBoundingClientRect();
  if (!items.length) return showLine(cr.left, cr.top + 3, cr.width);
  const r = idx < items.length ? items[idx].getBoundingClientRect() : items[items.length - 1].getBoundingClientRect();
  const y = idx < items.length ? r.top - 2 : r.bottom + 2;
  showLine(r.left, y, r.width);
}
function sceneDropZone(container, chId, itemSel) {
  container.addEventListener('dragover', (e) => {
    if (drag?.type !== 'scene') return;
    e.preventDefault();
    e.stopPropagation();
    const items = $$(itemSel, container);
    const idx = dropIndex(items, e.clientY);
    lineAt(container, items, idx);
    container._idx = idx;
  });
  container.addEventListener('drop', (e) => {
    if (drag?.type !== 'scene') return;
    e.preventDefault();
    e.stopPropagation();
    hideLine();
    moveScene(drag.id, chId, container._idx ?? 0);
  });
}
document.addEventListener('drop', () => hideLine());

/* =========================================================================
 * Menus, modals, toast
 * ========================================================================= */
let openMenu = null;
function closeMenu() { if (openMenu) { openMenu.remove(); openMenu = null; } }
function placeMenu(m, x, y) {
  document.body.append(m);
  const r = m.getBoundingClientRect();
  m.style.left = `${clamp(x, 8, innerWidth - r.width - 8)}px`;
  m.style.top = `${clamp(y, 8, innerHeight - r.height - 8)}px`;
  openMenu = m;
  setTimeout(() => {
    const away = (e) => { if (!m.contains(e.target)) { if (openMenu === m) closeMenu(); document.removeEventListener('mousedown', away, true); } };
    document.addEventListener('mousedown', away, true);
  });
}
function popMenu(x, y, items) {
  closeMenu();
  const m = h('div', { class: 'menu', role: 'menu' }, items.map((it) => {
    if (it === '-') return h('div', { class: 'menu-sep' });
    if (it.head) return h('div', { class: 'menu-head' }, it.head);
    return h('button', { class: `menu-item${it.danger ? ' danger' : ''}`, disabled: !!it.disabled, onclick: () => { closeMenu(); it.run(); } },
      it.icon ? icon(it.icon) : null, h('span', {}, it.label), it.key ? h('kbd', {}, it.key) : null);
  }));
  placeMenu(m, x, y);
}

function modal({ title, body, actions = [{ label: 'Close', primary: true }], wide = false, onClose }) {
  let done = false;
  const back = h('div', { class: 'modal-back' });
  const close = (value) => {
    if (done) return;
    done = true;
    back.remove();
    document.removeEventListener('keydown', onKey, true);
    onClose?.(value);
  };
  const onKey = (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(undefined); } };
  back.addEventListener('mousedown', (e) => { if (e.target === back) close(undefined); });
  const box = h('div', { class: `modal${wide ? ' wide' : ''}`, role: 'dialog', 'aria-label': title },
    h('h2', {}, title), body,
    h('div', { class: 'modal-actions' }, actions.map((a) => h('button', {
      class: `btn${a.primary ? ' primary' : ''}${a.danger ? ' danger solid' : ''}`,
      onclick: async () => { if (a.run && (await a.run()) === false) return; close(a.value); },
    }, a.label))));
  back.append(box);
  document.body.append(back);
  document.addEventListener('keydown', onKey, true);
  setTimeout(() => (box.querySelector('input, .btn.primary, .btn.danger') || box).focus(), 20);
  return { close, box };
}
function promptBox(title, placeholder = '', okLabel = 'Add') {
  return new Promise((res) => {
    const input = h('input', { class: 'field', placeholder });
    let m = null;
    const submit = () => m.close(input.value.trim() || undefined);
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); submit(); } });
    m = modal({ title, body: input, actions: [{ label: 'Cancel' }, { label: okLabel, primary: true, run: () => { submit(); return false; } }], onClose: (v) => res(v) });
  });
}
function confirmBox(message, { ok = 'Delete', cancel = 'Cancel' } = {}) {
  return new Promise((res) => {
    modal({
      title: ok === 'Delete' ? 'Are you sure?' : 'Just checking',
      body: h('p', {}, message),
      actions: [{ label: cancel, value: false }, { label: ok, value: true, danger: ok === 'Delete', primary: ok !== 'Delete' }],
      onClose: (v) => res(!!v),
    });
  });
}

let toastTimer;
function toast(msg) {
  const t = $('#toast');
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 2600);
}

/* =========================================================================
 * Status bar
 * ========================================================================= */
function renderStatus() {
  if (!project) return;
  const total = totalWC();
  const goal = project.meta.wordGoal;
  const scene = (ui.view === 'write' && ui.sel.kind === 'scene' && locateScene(ui.sel.id)?.scene) || (ui.view === 'write' && ui.activeSceneId && locateScene(ui.activeSceneId)?.scene);
  const parts = [];
  if (scene) parts.push(`Scene ${fmt(wcOf(scene))}`);
  if (ui.view === 'write' && ui.sel.kind === 'chapter') parts.push(`Chapter ${fmt(chapterWC(chapterOf(ui.sel.id) || { children: [] }))}`);
  parts.push(`Book ${fmt(total)}${goal ? ` / ${fmt(goal)}` : ''}`);
  $('#stCounts').textContent = parts.join('   ·   ');

  const today = todayWords();
  const dg = project.meta.dailyGoal;
  const st = $('#stToday');
  st.replaceChildren(`Today ${today >= 0 ? '+' : ''}${fmt(today)}${dg ? ` of ${fmt(dg)}` : ''}`,
    dg ? h('span', { class: 'meter', title: `${Math.round((Math.max(0, today) / dg) * 100)}% of today’s goal` }, h('i', { style: { width: `${clamp((today / dg) * 100, 0, 100)}%` } })) : null);

  const s = $('#stSave');
  let label, cls = '';
  if (!fileHandle) {
    if (HAS_FS) { label = 'Not saved to a file yet: Save…'; cls = 'warn'; } else { label = dirty ? 'In browser backup: Download a copy' : `Downloaded ${ago(lastSaved)}`; cls = dirty ? 'warn' : 'ok'; }
  } else if (saving) label = 'Saving…';
  else if (saveError) { label = 'Couldn’t save. Click to retry'; cls = 'err'; } else if (needsPerm) { label = 'Click to allow saving'; cls = 'warn'; } else if (dirty) label = 'Editing…';
  else { label = `Saved to ${fileName}`; cls = 'ok'; }
  s.textContent = label;
  s.className = `save-state ${cls}`;
  s.title = fileHandle ? `Saving automatically to ${fileName}` : 'Choose where to keep your story';
  document.title = `${dirty ? '• ' : ''}${project.meta.title || 'Untitled'} · Quire`;
}

/* =========================================================================
 * Command palette (Ctrl+K)
 * ========================================================================= */
function commands() {
  const cmd = (label, run, key) => ({ label, run, key, icon: 'cmd', kind: 'Command' });
  return [
    cmd('New scene', () => addScene(), 'Ctrl+Enter'),
    cmd('New chapter', () => addChapter()),
    cmd('New character', () => addEntity('characters')),
    cmd('New place', () => addEntity('locations')),
    cmd('New plot thread', () => addThread()),
    cmd('New note', () => addNote()),
    cmd('Focus mode', () => toggleFocus(), 'Ctrl+Shift+F'),
    cmd('Toggle scene details panel', () => toggleInspector(), 'Ctrl+.'),
    cmd('Toggle sidebar', () => toggleSidebar(), 'Ctrl+\\'),
    cmd('Save', () => saveNow({ interactive: true }), 'Ctrl+S'),
    cmd(HAS_FS ? 'Save as…' : 'Download story file', () => saveAs(), 'Ctrl+Shift+S'),
    cmd('Open a story file…', () => openFile(), 'Ctrl+O'),
    cmd('Export manuscript as Markdown', () => exportText('md')),
    cmd('Export manuscript as plain text', () => exportText('txt')),
    cmd('Print or save manuscript as PDF', () => printManuscript()),
    cmd('Snapshots: restore an earlier version', () => openSnapshots()),
    cmd('Preferences', () => openPrefs()),
    cmd('Theme: Paper', () => setPref('theme', 'paper')),
    cmd('Theme: Sepia', () => setPref('theme', 'sepia')),
    cmd('Theme: Night', () => setPref('theme', 'night')),
    cmd('Theme: Match system', () => setPref('theme', 'auto')),
    cmd('Close story', () => closeProject()),
  ];
}
function paletteItems() {
  const items = [];
  for (const ch of project.manuscript) {
    items.push({ label: ch.title || 'Untitled chapter', sub: plural(ch.children.length, 'scene'), icon: 'book', kind: 'Chapter', run: () => select('chapter', ch.id), text: '' });
    for (const s of ch.children) items.push({ label: s.title || 'Untitled scene', sub: [ch.title, s.synopsis].filter(Boolean).join(' · '), icon: 'file', kind: 'Scene', run: () => select('scene', s.id), text: plainOf(s), nav: true });
  }
  for (const c of project.characters) items.push({ label: c.name || 'Unnamed character', sub: c.role, icon: 'user', kind: 'Character', run: () => { ui.view = 'characters'; ui.entitySel.characters = c.id; renderAll(); }, text: [c.summary, c.notes, ...c.fields.map((f) => f.v)].join('\n'), nav: true });
  for (const l of project.locations) items.push({ label: l.name || 'Unnamed place', sub: l.role, icon: 'pin', kind: 'Place', run: () => { ui.view = 'locations'; ui.entitySel.locations = l.id; renderAll(); }, text: [l.summary, l.notes, ...l.fields.map((f) => f.v)].join('\n'), nav: true });
  for (const t of project.threads) items.push({ label: t.name, sub: t.summary, icon: 'thread', kind: 'Thread', run: () => setView('plot'), text: t.summary });
  for (const n of project.notes) items.push({ label: n.title || 'Untitled note', icon: 'note', kind: 'Note', run: () => { ui.view = 'notes'; ui.noteSel = n.id; renderAll(); }, text: textOf(n.content), nav: true });
  return items.concat(commands());
}
function searchItems(items, q) {
  if (!q) return items.filter((i) => i.nav).slice(0, 40);
  const res = [];
  items.forEach((it, order) => {
    const label = it.label.toLowerCase();
    let score = 0, snippet = null;
    if (label.startsWith(q)) score = 100;
    else if (label.includes(q)) score = 70;
    else if (q.split(/\s+/).every((w) => label.includes(w))) score = 50;
    else if ((it.sub || '').toLowerCase().includes(q)) score = 30;
    if (q.length >= 2 && it.text) {
      const lower = it.text.toLowerCase();
      const at = lower.indexOf(q);
      if (at >= 0) {
        if (!score) score = 20;
        const start = Math.max(0, at - 40);
        const raw = it.text.slice(start, at + q.length + 70).replace(/\s+/g, ' ');
        const rel = it.text.slice(start, at).replace(/\s+/g, ' ').length;
        snippet = { pre: (start ? '…' : '') + raw.slice(0, rel), match: raw.slice(rel, rel + q.length), post: raw.slice(rel + q.length) + '…' };
      }
    }
    if (score) res.push({ ...it, score, snippet, order, q });
  });
  return res.sort((a, b) => b.score - a.score || a.order - b.order).slice(0, 50);
}
function openPalette() {
  if (!project || $('.palette-back')) return;
  flushEditors();
  const items = paletteItems();
  const input = h('input', { class: 'pal-input', placeholder: 'Find a scene, character, place or line… or run a command', spellcheck: 'false' });
  const list = h('div', { class: 'pal-list', role: 'listbox' });
  const back = h('div', { class: 'palette-back' }, h('div', { class: 'palette' }, h('div', { class: 'pal-head' }, icon('search'), input), list));
  let results = [], idx = 0;
  const close = () => back.remove();
  const setActive = (i) => {
    list.children[idx]?.classList.remove('active');
    idx = i;
    const el = list.children[idx];
    el?.classList.add('active');
    el?.scrollIntoView({ block: 'nearest' });
  };
  const run = (i) => {
    const r = results[i];
    if (!r) return;
    close();
    r.run();
    if (r.snippet && r.kind === 'Scene') setTimeout(() => findInView(r.q), 60);
  };
  const draw = () => {
    list.replaceChildren(...results.map((r, i) => h('div', { class: `pal-item${i === idx ? ' active' : ''}`, onmousemove: () => { if (idx !== i) setActive(i); }, onclick: () => run(i) },
      icon(r.icon),
      h('div', { class: 'pal-text' }, h('div', { class: 'pal-label' }, r.label),
        r.snippet ? h('div', { class: 'pal-snippet' }, r.snippet.pre, h('mark', {}, r.snippet.match), r.snippet.post) : r.sub ? h('div', { class: 'pal-sub' }, r.sub) : null),
      r.key ? h('kbd', {}, r.key) : h('span', { class: 'pal-kind' }, r.kind))));
    if (!results.length) list.append(h('div', { class: 'pal-empty' }, 'Nothing found'));
  };
  const update = () => { results = searchItems(items, input.value.trim().toLowerCase()); idx = 0; draw(); };
  input.addEventListener('input', update);
  input.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowDown') { e.preventDefault(); setActive(Math.min(idx + 1, results.length - 1)); } else if (e.key === 'ArrowUp') { e.preventDefault(); setActive(Math.max(idx - 1, 0)); } else if (e.key === 'Enter') { e.preventDefault(); run(idx); } else if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); close(); }
  });
  back.addEventListener('mousedown', (e) => { if (e.target === back) close(); });
  document.body.append(back);
  input.focus();
  update();
}
/** Put the caret on the first match of q inside the editor(s) in view. */
function findInView(q) {
  const ql = q.toLowerCase();
  for (const ed of $$('#view .prose')) {
    const walker = document.createTreeWalker(ed, NodeFilter.SHOW_TEXT);
    let n;
    while ((n = walker.nextNode())) {
      const at = n.data.toLowerCase().indexOf(ql);
      if (at >= 0) {
        ed.focus();
        const r = document.createRange();
        r.setStart(n, at); r.setEnd(n, at + q.length);
        const sel = getSelection(); sel.removeAllRanges(); sel.addRange(r);
        n.parentElement.scrollIntoView({ block: 'center' });
        return;
      }
    }
  }
}

/* =========================================================================
 * Preferences, snapshots, export
 * ========================================================================= */
function applyPrefs() {
  const root = document.documentElement;
  const dark = matchMedia('(prefers-color-scheme: dark)').matches;
  const theme = prefs.theme === 'auto' ? (dark ? 'night' : 'paper') : prefs.theme;
  root.dataset.theme = theme;
  $('meta[name="theme-color"]')?.setAttribute('content', getComputedStyle(root).getPropertyValue('--bg').trim() || '#f7f5f0');
  root.style.setProperty('--prose-font', `var(--${prefs.font})`);
  root.style.setProperty('--prose-size', `${prefs.size}px`);
  root.style.setProperty('--measure', `${prefs.measure}em`);
  document.body.classList.toggle('para-indent', prefs.para === 'indent');
  document.body.classList.toggle('dim', !!prefs.dim);
  document.body.classList.toggle('no-sidebar', !prefs.sidebar);
  if (project) renderToolbar();
}
matchMedia('(prefers-color-scheme: dark)').addEventListener?.('change', () => { if (prefs.theme === 'auto') applyPrefs(); });

function openPrefs() {
  const row = (label, control) => h('div', { class: 'pref-row' }, h('span', {}, label), h('div', {}, control));
  const sw = (key) => {
    const b = h('button', { class: `switch${prefs[key] ? ' on' : ''}`, role: 'switch', 'aria-checked': String(!!prefs[key]) });
    b.addEventListener('click', () => { setPref(key, !prefs[key]); b.classList.toggle('on', prefs[key]); b.setAttribute('aria-checked', String(prefs[key])); });
    return b;
  };
  const range = (key, min, max, step = 1) => {
    const r = h('input', { type: 'range', min: String(min), max: String(max), step: String(step), value: String(prefs[key]) });
    r.addEventListener('input', () => setPref(key, +r.value));
    return r;
  };
  modal({
    title: 'Preferences',
    wide: true,
    body: h('div', {},
      row('Theme', segmented([['auto', 'Auto'], ['paper', 'Paper'], ['sepia', 'Sepia'], ['night', 'Night']], prefs.theme, (v) => setPref('theme', v))),
      row('Typeface', segmented([['serif', 'Serif'], ['sans', 'Sans'], ['mono', 'Mono']], prefs.font, (v) => setPref('font', v))),
      row('Text size', range('size', 15, 26)),
      row('Line length', range('measure', 26, 48)),
      row('Paragraphs', segmented([['indent', 'Book indent'], ['space', 'Spaced']], prefs.para, (v) => setPref('para', v))),
      row('Smart quotes & dashes', sw('smart')),
      row('Typewriter scrolling', sw('typewriter')),
      row('Dim other paragraphs in focus mode', sw('dim')),
      h('p', { class: 'hint', style: { marginTop: '12px' } }, 'Preferences belong to this browser. Your story file is unaffected.')),
    actions: [{ label: 'Done', primary: true }],
  });
}

async function openSnapshots() {
  flushEditors();
  await backupNow();
  const snaps = (await idb.get(`snaps:${project.meta.id}`)) || [];
  const body = h('div', {},
    h('p', {}, 'Quire keeps a snapshot of this story in this browser every ten minutes while you write (the last 30). Restoring replaces the current story; the current version is snapshotted first.'),
    snaps.length ? h('div', { class: 'snap-list' }, snaps.map((sn) => h('div', { class: 'snap' }, icon('history'),
      h('span', {}, new Date(sn.at).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' }), h('span', { class: 'muted' }, ` · ${plural(sn.words, 'word')}`)),
      h('button', { class: 'btn small', onclick: async () => {
        m.close();
        if (!(await confirmBox(`Restore the version from ${ago(sn.at)}? Your current text is snapshotted first.`, { ok: 'Restore' }))) return;
        const snapsNow = (await idb.get(`snaps:${project.meta.id}`)) || [];
        snapsNow.unshift({ at: Date.now(), words: totalWC(), data: serialize() });
        await idb.set(`snaps:${project.meta.id}`, snapsNow.slice(0, 30));
        const handle = fileHandle;
        await openProject(sn.data, handle, { recoverCheck: false });
        touch();
        toast('Snapshot restored');
      } }, 'Restore'))))
      : h('p', { class: 'muted' }, 'No snapshots yet.'));
  const m = modal({ title: 'Snapshots', body, wide: true, actions: [{ label: 'Close', primary: true }] });
}

function exportText(kind) {
  flushEditors();
  const md = kind === 'md';
  const m = project.meta;
  const out = [];
  out.push(md ? `# ${m.title}` : m.title.toUpperCase());
  if (m.author) out.push(md ? `*by ${m.author}*` : `by ${m.author}`);
  for (const ch of project.manuscript) {
    out.push(md ? `## ${ch.title}` : `\n${ch.title}\n${'-'.repeat(Math.min(60, ch.title.length || 3))}`);
    out.push(ch.children.map((s) => (md ? htmlToMd(s.content) : htmlToPlain(s.content))).filter(Boolean).join(md ? '\n\n* * *\n\n' : '\n\n*\n\n'));
  }
  download(`${safeName(m.title)}.${kind}`, `${out.filter(Boolean).join('\n\n')}\n`, md ? 'text/markdown' : 'text/plain');
  toast(`Exported ${md ? 'Markdown' : 'plain text'}`);
}

function printManuscript() {
  flushEditors();
  const m = project.meta;
  const css = `@page{margin:2.4cm 2.2cm}body{font:12pt/1.65 "Palatino Linotype",Palatino,"Book Antiqua",Georgia,serif;color:#111;margin:0}
  .title{text-align:center;padding-top:32vh;break-after:page}.title h1{font-weight:normal;font-size:28pt;margin:0 0 10pt}.title p{font-style:italic;margin:0}
  h2{font-weight:normal;text-align:center;font-size:17pt;margin:3.5cm 0 1.4cm;break-before:page}h2:first-of-type{break-before:auto}
  p{margin:0;text-indent:1.4em;text-align:justify;hyphens:auto;orphans:2;widows:2}.scene p:first-child{text-indent:0}
  .brk{text-align:center;margin:12pt 0;letter-spacing:.5em}`;
  const body = project.manuscript.map((ch) => `<h2>${escHTML(ch.title)}</h2>${ch.children.map((s) => `<div class="scene">${s.content}</div>`).join('<div class="brk">* * *</div>')}`).join('');
  const html = `<!doctype html><html><head><meta charset="utf-8"><title>${escHTML(m.title)}</title><style>${css}</style></head><body><div class="title"><h1>${escHTML(m.title)}</h1>${m.author ? `<p>by ${escHTML(m.author)}</p>` : ''}</div>${body}</body></html>`;
  const frame = h('iframe', { style: { position: 'fixed', right: '0', bottom: '0', width: '0', height: '0', border: '0' } });
  frame.srcdoc = html;
  frame.onload = () => {
    frame.contentWindow.focus();
    frame.contentWindow.print();
    setTimeout(() => frame.remove(), 1500);
  };
  document.body.append(frame);
}

/* =========================================================================
 * Modes and toggles
 * ========================================================================= */
function toggleFocus(on) {
  const want = on ?? !document.body.classList.contains('focus-mode');
  if (want && ui.view !== 'write') setView('write');
  document.body.classList.toggle('focus-mode', want);
  if (want) {
    document.documentElement.requestFullscreen?.().catch(() => {});
    setTimeout(() => { const ed = $('#view .prose'); if (ed && !document.activeElement?.classList.contains('prose')) ed.focus(); }, 60);
  } else if (document.fullscreenElement) document.exitFullscreen?.().catch(() => {});
}
document.addEventListener('fullscreenchange', () => {
  if (!document.fullscreenElement && document.body.classList.contains('focus-mode')) document.body.classList.remove('focus-mode');
});
const isNarrow = () => innerWidth <= 920;
function toggleInspector() {
  flushEditors();
  if (isNarrow()) { ui.inspNarrow = !ui.inspNarrow; renderToolbar(); } else setPref('inspector', !prefs.inspector);
  renderInspector();
}
window.addEventListener('resize', debounce(() => { if (project) renderToolbar(); }, 150));
function toggleSidebar() {
  if (innerWidth <= 760) { document.body.classList.toggle('side-open'); return; }
  setPref('sidebar', !prefs.sidebar);
}

function mainMenu() {
  const r = $('#tbMenu').getBoundingClientRect();
  popMenu(r.right - 240, r.bottom + 4, [
    { label: 'Save', key: 'Ctrl+S', icon: 'file', run: () => saveNow({ interactive: true }) },
    { label: HAS_FS ? 'Save as…' : 'Download story file', key: 'Ctrl+Shift+S', icon: 'restore', run: saveAs },
    { label: 'Open…', key: 'Ctrl+O', icon: 'folder', run: openFile },
    '-',
    { head: 'Export manuscript' },
    { label: 'Markdown (.md)', run: () => exportText('md') },
    { label: 'Plain text (.txt)', run: () => exportText('txt') },
    { label: 'Print or PDF…', run: printManuscript },
    '-',
    { label: 'Snapshots…', icon: 'history', run: openSnapshots },
    { label: 'Preferences…', icon: 'spark', run: openPrefs },
    { label: 'Keyboard shortcuts', run: openShortcuts },
    '-',
    { label: 'Close story', run: closeProject },
  ]);
}
function openShortcuts() {
  const rows = [
    ['Ctrl+K', 'Find anything, run any command'], ['Ctrl+S', 'Save now (Quire also saves automatically)'], ['Ctrl+Shift+S', 'Save as…'],
    ['Ctrl+O', 'Open a story'], ['Ctrl+Enter', 'New scene after this one'], ['Ctrl+Shift+F', 'Focus mode (Esc to leave)'],
    ['Ctrl+.', 'Scene details panel'], ['Ctrl+\\', 'Sidebar'], ['Alt+1 … Alt+6', 'Manuscript, Board, Story map, Characters, Places, Notes'],
    ['Ctrl+B / Ctrl+I', 'Bold / italic'], ['" \' -- ...', 'Become “curly quotes”, em dashes and ellipses'],
  ];
  modal({ title: 'Keyboard shortcuts', wide: true, body: h('div', {}, rows.map(([k, d]) => h('div', { class: 'pref-row' }, h('span', {}, h('kbd', { style: { fontSize: '12.5px', color: 'var(--ink)' } }, k)), h('span', { class: 'muted' }, d)))),
    actions: [{ label: 'Close', primary: true }] });
}

/* =========================================================================
 * Welcome screen
 * ========================================================================= */
function hideWelcome() {
  document.body.classList.remove('welcome-open');
  $('#welcome').replaceChildren();
}
async function showWelcome() {
  document.body.classList.add('welcome-open');
  document.body.classList.remove('focus-mode');
  document.title = 'Quire';
  const w = $('#welcome');
  const recent = HAS_FS ? await getRecent() : [];
  const backups = await listBackups();
  const recentIds = new Set(recent.map((r) => r.id));

  const titleIn = h('input', { class: 'field', placeholder: 'Working title', 'aria-label': 'Working title' });
  const newRow = h('form', { class: 'new-row', hidden: true, onsubmit: (e) => { e.preventDefault(); createNew(titleIn.value.trim()); } },
    titleIn, h('button', { class: 'btn primary', type: 'submit' }, HAS_FS ? 'Create & choose where to save' : 'Create'));

  const card = h('div', { class: 'welcome-card' },
    h('div', { class: 'wordmark' }, 'Quire', h('i', {}, '.')),
    h('p', { class: 'tagline' }, 'A quiet place to write your story.'),
    h('div', { class: 'welcome-actions' },
      h('button', { class: 'btn primary', onclick: () => { newRow.hidden = false; titleIn.focus(); } }, icon('plus'), 'New story'),
      h('button', { class: 'btn', onclick: openFile }, icon('folder'), 'Open a story…'),
      h('button', { class: 'btn ghost', onclick: () => openProject(sampleProject(), null, { recoverCheck: false }).then(() => toast('This is a sample story. Use Save as… to keep a copy.')) }, icon('spark'), 'Explore a sample')),
    newRow);

  if (recent.length) {
    card.append(h('div', { class: 'w-section' }, h('h3', {}, 'Recent stories'),
      recent.map((r) => h('button', { class: 'w-item', onclick: () => openRecent(r) }, icon('book'),
        h('span', { class: 'w-name' }, r.title || r.name), h('span', { class: 'w-meta' }, `${r.name} · ${ago(r.at)}`)))));
  }
  const orphans = backups.filter((b) => !recentIds.has(b.id)).slice(0, 6);
  if (orphans.length) {
    card.append(h('div', { class: 'w-section' }, h('h3', {}, 'Kept in this browser'),
      orphans.map((b) => h('button', { class: 'w-item', title: 'Open the copy Quire keeps in this browser', onclick: () => openProject(b.data, null, { recoverCheck: false }).then(() => toast(HAS_FS ? 'Opened from browser backup. Save… to keep it in a file.' : 'Opened from browser backup.')) },
        icon('history'), h('span', { class: 'w-name' }, b.title || 'Untitled'), h('span', { class: 'w-meta' }, `${plural(b.words, 'word')} · ${ago(b.at)}`)))));
  }
  card.append(h('p', { class: 'w-foot' }, HAS_FS
    ? [h('b', {}, 'Your story is one file. '), 'Keep it anywhere: a USB stick, Dropbox, OneDrive, iCloud. Quire saves to it as you write, works offline, and keeps backup snapshots in this browser.']
    : [h('b', {}, 'This browser can’t save straight to files. '), 'Quire still works and keeps your writing in this browser, but use Download to keep a file. Chrome or Edge on Windows or Mac gives the full experience.']));
  w.replaceChildren(card);
}

/* =========================================================================
 * Sample story
 * ========================================================================= */
function sampleProject() {
  const P = (...ps) => ps.map((p) => `<p>${p}</p>`).join('');
  const p = newProject('The Lantern Keeper');
  p.meta.author = 'A. Writer';
  p.meta.wordGoal = 60000;
  const wren = { id: uid('ch'), name: 'Wren Halloway', role: 'Protagonist', color: COLORS[0], aliases: 'the keeper’s girl', summary: 'Seventeen, stubborn, keeping her dead father’s lighthouse lit because nobody told her to stop.',
    fields: [{ k: 'Goal', v: 'Keep the light burning until the relief keeper comes.' }, { k: 'Motivation', v: 'If the light goes out, then he is really gone.' }, { k: 'Conflict', v: 'The oil is running out, and the harbour wants the light shut.' }, { k: 'Appearance', v: 'Salt-stiff braid, her father’s oilskin, cuffs rolled four times.' }, { k: 'Voice', v: 'Short sentences. Counts things when she is scared.' }], notes: '' };
  const ansel = { id: uid('ch'), name: 'Ansel Marr', role: 'Harbourmaster', color: COLORS[1], aliases: 'the harbourmaster', summary: 'Kind in the way of men who have already decided what is best for you.',
    fields: [{ k: 'Goal', v: 'Close Skerry Light and bring Wren ashore.' }, { k: 'Motivation', v: 'He owes her father a debt nobody knows about.' }, { k: 'Conflict', v: 'Wren will not leave.' }, { k: 'Appearance', v: '' }, { k: 'Voice', v: 'Calls everyone “lass” or “lad”. Never raises his voice.' }], notes: '' };
  const grey = { id: uid('ch'), name: 'The Grey Man', role: 'Stranger', color: COLORS[3], aliases: 'the stranger', summary: 'Came out of the fog in a boat with no oars. Knows the name of every keeper since 1840.',
    fields: [{ k: 'Goal', v: '?' }, { k: 'Appearance', v: 'Wet wool, no shoes, eyes the colour of the fog.' }], notes: 'Do not explain him too early.' };
  const light = { id: uid('lo'), name: 'Skerry Light', role: 'Lighthouse', color: COLORS[4], aliases: 'the light, the tower', summary: 'A white tower on a black rock, a mile and a half off Port Caddow.',
    fields: [{ k: 'Atmosphere', v: 'Paraffin, wet stone, the clock-tick of the lens turning.' }, { k: 'Sights & sounds', v: '114 steps. The gallery rail sings in a westerly.' }, { k: 'Significance', v: 'The last place her father was alive.' }], notes: '' };
  const port = { id: uid('lo'), name: 'Port Caddow', role: 'Fishing village', color: COLORS[2], aliases: 'the harbour, Caddow', summary: 'Forty houses, two chapels, one opinion about everything.',
    fields: [{ k: 'Atmosphere', v: 'Herring, tar, gossip.' }, { k: 'Sights & sounds', v: 'Gulls, the bell on the breakwater.' }, { k: 'Significance', v: 'Where everyone thinks Wren belongs.' }], notes: '' };
  const tA = { id: uid('t'), name: 'The stranger’s secret', color: COLORS[3], summary: 'Who is the Grey Man, and why has he come to Skerry now?' };
  const tB = { id: uid('t'), name: 'Wren and her father', color: COLORS[0], summary: 'Grief, and the letting go she keeps refusing.' };
  const tC = { id: uid('t'), name: 'The storm', color: COLORS[5], summary: 'A three-day gale is coming. The light must hold.' };
  p.characters = [wren, ansel, grey];
  p.locations = [light, port];
  p.threads = [tA, tB, tC];

  const sc = (title, status, synopsis, pov, chars, loc, threads, when, content) => ({ ...mkScene(title), status, synopsis, pov, characters: chars, location: loc, threads, when, content: content || '<p><br></p>' });
  const ch1 = { ...mkChapter('I. The Light'), children: [
    sc('Last of the oil', 'revised', 'Wren measures the paraffin and does the arithmetic she has been avoiding.', wren.id, [wren.id], light.id, [tB.id, tC.id], 'Day 1, dusk',
      P('There were eleven cans left in the oil store, and Wren counted them twice because the first time she had come up with twelve and wanted to be wrong.',
        'Eleven cans. Four nights to a can, if she trimmed the wick the way her father had shown her, low and blue and miserly. Forty-four nights. The relief keeper was due in thirty. She had told herself that so often it had worn smooth, like a stone she kept in her pocket.',
        'Above her the lens turned on its bath of mercury, patient as a clock, throwing its long arm out across the water to <em>nobody</em>. That was the part no one in Port Caddow understood. You did not keep the light for the ships you could see.',
        'She shut the store, climbed the hundred and fourteen steps, and lit the lamp at six minutes past four, exactly as the tables said.')),
    sc('A boat in the fog', 'draft', 'A rowing boat with no oars drifts onto the rocks. There is a man in it, and he is not cold.', wren.id, [wren.id, grey.id], light.id, [tA.id], 'Day 2, before dawn',
      P('The fog came in before dawn the way it always did in October, not rolling but simply <em>arriving</em>, as if the sea had breathed out.',
        'Wren heard the boat before she saw it: wood on rock, a slow grinding knock, like someone at a door who knows they will be let in.')),
  ] };
  const ch2 = { ...mkChapter('II. The Harbour'), children: [
    sc('Ansel’s warning', 'draft', 'The harbourmaster rows out to tell Wren the Trinity Board means to shut the light. She tells him about the stranger instead. Mistake.', wren.id, [wren.id, ansel.id], light.id, [tB.id, tA.id], 'Day 2, noon', ''),
    sc('What the tide brought', 'idea', 'In Port Caddow, Ansel finds something on the strand that should have gone down with Wren’s father.', ansel.id, [ansel.id], port.id, [tA.id, tC.id], 'Day 2, evening', ''),
  ] };
  p.manuscript = [ch1, ch2];
  p.notes = [{ id: uid('n'), title: 'Rules of the light', content: P('Lamp lit at sunset, by the tables. Never late.', 'Wick trimmed every four hours.', 'The log is written in ink, never pencil, and never torn.', '<em>Ideas:</em> the log could be how the Grey Man proves who he is.') }];
  return p;
}

/* =========================================================================
 * Keyboard, window events, startup
 * ========================================================================= */
document.addEventListener('keydown', (e) => {
  const mod = e.ctrlKey || e.metaKey;
  const k = e.key.toLowerCase();
  if (!project) {
    if (mod && k === 'o') { e.preventDefault(); openFile(); }
    return;
  }
  if ($('.modal-back')) return;
  if (mod && k === 's' && !e.shiftKey) { e.preventDefault(); saveNow({ interactive: true }).then(() => { if (fileHandle && !saveError) toast(`Saved to ${fileName}`); }); } else if (mod && k === 's' && e.shiftKey) { e.preventDefault(); saveAs(); } else if (mod && k === 'k') { e.preventDefault(); openPalette(); } else if (mod && k === 'o') { e.preventDefault(); openFile(); } else if (mod && e.shiftKey && k === 'f') { e.preventDefault(); toggleFocus(); } else if (mod && e.key === '\\') { e.preventDefault(); toggleSidebar(); } else if (mod && e.key === '.') { e.preventDefault(); toggleInspector(); } else if (mod && e.key === 'Enter') { e.preventDefault(); addScene(); } else if (e.altKey && !mod && /^Digit[1-6]$/.test(e.code)) { e.preventDefault(); setView(VIEWS[+e.code.slice(5) - 1][0]); } else if (e.key === 'Escape' && document.body.classList.contains('focus-mode')) { toggleFocus(false); } else if (e.key === 'Escape') closeMenu();
});

window.addEventListener('beforeunload', (e) => {
  if (!project) return;
  flushEditors();
  if (dirty) { try { localStorage.setItem('quire:emergency', serialize()); } catch { /* too big or blocked */ } }
  if (dirty) { saveNow(); e.preventDefault(); e.returnValue = ''; }
});
document.addEventListener('visibilitychange', () => { if (document.hidden && project) { flushEditors(); if (dirty) saveNow(); else if (changeSeq) backupNow(); } });

// Drop a .quire file anywhere to open it.
window.addEventListener('dragover', (e) => { if (!drag && e.dataTransfer?.types?.includes('Files')) e.preventDefault(); });
window.addEventListener('drop', async (e) => {
  if (drag || !e.dataTransfer?.files?.length) return;
  e.preventDefault();
  const item = e.dataTransfer.items?.[0];
  if (item?.getAsFileSystemHandle) {
    const handle = await item.getAsFileSystemHandle();
    if (handle?.kind === 'file') { if (project) await saveNow(); openHandle(handle); return; }
  }
  const f = e.dataTransfer.files[0];
  if (project) await saveNow();
  if (await openProject(await f.text(), null)) fileName = f.name;
});

async function recoverEmergency() {
  try {
    const raw = localStorage.getItem('quire:emergency');
    if (!raw) return;
    const p = JSON.parse(raw);
    const b = await idb.get(`backup:${p.meta.id}`);
    if (!b || p.meta.modified > b.modified) {
      await idb.set(`backup:${p.meta.id}`, { id: p.meta.id, title: p.meta.title, modified: p.meta.modified, at: Date.now(), words: countWords(p.manuscript.flatMap((c) => c.children).map((s) => textOf(s.content)).join('\n')), data: raw });
    }
    localStorage.removeItem('quire:emergency');
  } catch (e) { console.warn('Emergency recovery skipped', e); }
}

function init() {
  applyPrefs();
  try { document.execCommand('defaultParagraphSeparator', false, 'p'); } catch { /* old browsers */ }
  $('#tbSidebar').append(icon('panelL'));
  $('#tbSearch').append(icon('search'));
  $('#tbFocus').append(icon('focus'));
  $('#tbInspector').append(icon('panelR'));
  $('#tbMenu').append(icon('more'));
  $('#tbSidebar').onclick = toggleSidebar;
  $('#tbSearch').onclick = openPalette;
  $('#tbFocus').onclick = () => toggleFocus();
  $('#tbInspector').onclick = toggleInspector;
  $('#tbMenu').onclick = mainMenu;
  $('#stSave').onclick = () => (fileHandle ? saveNow({ interactive: true }) : saveAs());
  $('#projTitle').onclick = () => select('book');

  if ('launchQueue' in window) {
    window.launchQueue.setConsumer(async (params) => { if (params.files?.length) openHandle(params.files[0]); });
  }
  if ('serviceWorker' in navigator && /^https?:$/.test(location.protocol) && $('link[rel="manifest"]')) {
    navigator.serviceWorker.register('sw.js').catch((e) => console.warn('Service worker not registered', e));
  }
  recoverEmergency().then(showWelcome);
}

// A small hook for automated tests and the console. Not part of the file format.
window.quire = { get project() { return project; }, sanitizeHTML, countWords, openProject, serialize: () => serialize(), sampleProject, version: VERSION };

init();
})();
