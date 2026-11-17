/* Quire three-way merge.
 *
 * Given the version two copies last agreed on (base), this computer's copy (local) and the
 * other copy (remote: the cloud, or the file as changed on disk by another computer), produce
 * one story that keeps both sides' work.
 *
 * Every chapter, scene, character, place, thread and note has a stable id, so the merge works
 * per item and per field:
 *   - a field changed on one side only takes that side's value;
 *   - tag lists (characters and threads on a scene) merge as sets: additions and removals from both;
 *   - an item deleted on one side and untouched on the other is deleted; edited on the other, it stays;
 *   - order follows whichever side rearranged; new items land next to their old neighbours;
 *   - if both sides rewrote the same scene's prose, BOTH versions are kept, the other one as a new
 *     scene placed right after it and marked "(other version)". Nothing is ever silently dropped.
 *   - both sides edited the same notes/synopsis/summary: both texts are kept, one after the other.
 *
 * Pure functions, no DOM: loaded as a classic script in the app, and by Node for tests.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.QuireMerge = api;
})(typeof self !== 'undefined' ? self : this, () => {
  'use strict';

  function stable(v) {
    if (Array.isArray(v)) return `[${v.map(stable).join(',')}]`;
    if (v && typeof v === 'object') {
      return `{${Object.keys(v).filter((k) => v[k] !== undefined).sort().map((k) => `${JSON.stringify(k)}:${stable(v[k])}`).join(',')}}`;
    }
    return JSON.stringify(v === undefined ? null : v);
  }
  const same = (a, b) => stable(a) === stable(b);
  const clone = (v) => (v === undefined ? undefined : JSON.parse(JSON.stringify(v)));
  const newId = (p) => p + Math.random().toString(36).slice(2, 8) + Date.now().toString(36).slice(-4);

  const ID_SETS = new Set(['characters', 'threads']);
  const LONG_TEXT = new Set(['synopsis', 'notes', 'summary']);
  const OTHER = '\n\n--- other version (kept by sync) ---\n';

  /** Set-merge two edited copies of an id list. Keeps local order, appends remote additions. */
  function mergeIdSet(b, l, r) {
    const B = new Set(b || []), L = new Set(l || []), R = new Set(r || []);
    const out = [];
    for (const x of l || []) if (!(B.has(x) && !R.has(x)) && !out.includes(x)) out.push(x);
    for (const x of r || []) if (!L.has(x) && !B.has(x) && !out.includes(x)) out.push(x);
    return out;
  }

  /** Resolve a field both sides changed differently. */
  function resolve(k, lv, rv, ctx, item, kind) {
    if (k === 'modified' || k === 'created') return (k === 'modified' ? lv > rv : lv < rv) ? lv : rv;
    if (kind === 'scene' && k === 'content') {
      ctx.contentConflicts.push({ id: item.id, remote: rv });
      return lv;
    }
    if (kind === 'note' && k === 'content') {
      ctx.conflicts.push({ kind, id: item.id, field: k, title: item.title });
      return `${lv}<p>— other version (kept by sync) —</p>${rv}`;
    }
    if (LONG_TEXT.has(k) && typeof lv === 'string' && typeof rv === 'string') {
      if (rv.includes(lv)) return rv;
      if (lv.includes(rv)) return lv;
      ctx.conflicts.push({ kind, id: item.id, field: k, title: item.title || item.name });
      return lv + OTHER + rv;
    }
    ctx.conflicts.push({ kind, id: item.id, field: k, title: item.title || item.name, kept: 'local' });
    return clone(lv);
  }

  function mergeFields(b, l, r, ctx, kind, skip) {
    const out = {};
    const keys = new Set([...Object.keys(l), ...Object.keys(r)]);
    for (const k of keys) {
      if (skip && skip.has(k)) continue;
      const lv = l[k], rv = r[k], bv = b ? b[k] : undefined;
      if (!(k in r)) { out[k] = clone(lv); continue; }
      if (!(k in l)) { out[k] = clone(rv); continue; }
      if (kind === 'scene' && ID_SETS.has(k) && Array.isArray(lv) && Array.isArray(rv)) { out[k] = mergeIdSet(bv, lv, rv); continue; }
      if (same(lv, rv)) out[k] = clone(lv);
      else if (b && same(bv, lv)) out[k] = clone(rv);
      else if (b && same(bv, rv)) out[k] = clone(lv);
      else out[k] = resolve(k, lv, rv, ctx, l, kind);
    }
    return out;
  }

  const byId = (arr) => { const m = new Map(); for (const x of arr || []) if (x && x.id != null) m.set(x.id, x); return m; };
  const ids = (arr) => (arr || []).map((x) => x.id);

  /** Merge an ordering. Follows whichever side rearranged the items they share; slots in new items. */
  function mergeOrder(b, l, r, keep) {
    b = b || []; l = l || []; r = r || [];
    const inAll = new Set(b.filter((x) => l.includes(x) && r.includes(x)));
    const proj = (seq) => seq.filter((x) => inAll.has(x));
    const lMoved = !same(proj(b), proj(l)), rMoved = !same(proj(b), proj(r));
    const primary = rMoved && !lMoved ? r : l;
    const secondary = primary === l ? r : l;
    const out = primary.filter((x) => keep.has(x));
    const placed = new Set(out);
    let prev = null;
    for (const x of secondary) {
      if (!keep.has(x)) continue;
      if (!placed.has(x)) { out.splice(prev == null ? 0 : out.indexOf(prev) + 1, 0, x); placed.add(x); }
      prev = x;
    }
    for (const x of keep) if (!placed.has(x)) { out.push(x); placed.add(x); }
    return out;
  }

  /** Merge a list of items with ids. Returns a Map id -> merged item (unordered). */
  function mergeItems(bArr, lArr, rArr, ctx, kind, compareSkip) {
    const B = byId(bArr), L = byId(lArr), R = byId(rArr);
    const strip = (x) => { if (!compareSkip) return x; const c = { ...x }; for (const k of compareSkip) delete c[k]; return c; };
    const out = new Map();
    for (const id of new Set([...L.keys(), ...R.keys()])) {
      const b = ctx.hasBase ? B.get(id) : undefined, l = L.get(id), r = R.get(id);
      if (l && r) out.set(id, mergeFields(b, l, r, ctx, kind, compareSkip ? new Set(compareSkip) : null));
      else {
        const only = l || r;
        if (b && same(strip(b), strip(only))) continue; // deleted on the other side, untouched here
        if (b) ctx.conflicts.push({ kind, id, field: 'deleted', title: only.title || only.name, kept: 'edited copy' });
        const c = clone(only);
        if (compareSkip) for (const k of compareSkip) delete c[k];
        out.set(id, c);
      }
    }
    return out;
  }

  function flatScenes(p) {
    const out = [];
    for (const c of (p && p.manuscript) || []) for (const s of c.children || []) out.push({ ...s, _parent: c.id });
    return out;
  }
  const childIds = (p, cid) => { const c = ((p && p.manuscript) || []).find((x) => x.id === cid); return c ? ids(c.children) : []; };

  /**
   * merge(base, local, remote) -> { project, conflicts, copies }
   * base may be null when the two copies have no known common ancestor.
   */
  function merge(base, local, remote) {
    if (same(local, remote)) return { project: clone(local), conflicts: [], copies: 0 };
    const ctx = { hasBase: !!base, conflicts: [], contentConflicts: [] };
    const b = base || {};
    const skipTop = new Set(['manuscript', 'characters', 'locations', 'threads', 'notes', 'meta', 'stats']);
    const out = mergeFields(base ? b : null, local, remote, ctx, 'story', skipTop);

    out.meta = mergeFields(base ? b.meta || {} : null, local.meta || {}, remote.meta || {}, ctx, 'meta');

    // Daily word stats: union of days; for a shared day keep the widest span.
    const daily = {};
    const ld = (local.stats && local.stats.daily) || {}, rd = (remote.stats && remote.stats.daily) || {};
    for (const day of new Set([...Object.keys(ld), ...Object.keys(rd)])) {
      const x = ld[day], y = rd[day];
      daily[day] = x && y ? { start: Math.min(x.start, y.start), end: Math.max(x.end, y.end) } : clone(x || y);
    }
    out.stats = { ...clone(remote.stats || {}), ...clone(local.stats || {}), daily };

    for (const [key, kind] of [['characters', 'character'], ['locations', 'location'], ['threads', 'thread'], ['notes', 'note']]) {
      const merged = mergeItems(b[key], local[key], remote[key], ctx, kind);
      out[key] = mergeOrder(ids(b[key]), ids(local[key]), ids(remote[key]), new Set(merged.keys())).map((id) => merged.get(id));
    }

    // Scenes as one flat list with a parent field, so moving a scene between chapters merges too.
    const scenes = mergeItems(flatScenes(base), flatScenes(local), flatScenes(remote), ctx, 'scene');
    const chapterSkip = ['children'];
    const chapters = mergeItems(b.manuscript, local.manuscript, remote.manuscript, ctx, 'chapter', chapterSkip);
    // A chapter deleted on one side but given new or edited scenes on the other comes back.
    for (const s of scenes.values()) {
      if (chapters.has(s._parent)) continue;
      const src = [local, remote, b].map((p) => ((p && p.manuscript) || []).find((c) => c.id === s._parent)).find(Boolean);
      if (src) { const c = clone(src); delete c.children; chapters.set(c.id, c); } else s._parent = null;
    }
    const chapterOrder = mergeOrder(ids(b.manuscript), ids(local.manuscript), ids(remote.manuscript), new Set(chapters.keys()));
    // Scenes whose chapter vanished entirely go to the last chapter (or a new one).
    const orphans = [...scenes.values()].filter((s) => !s._parent);
    if (orphans.length) {
      let last = chapterOrder[chapterOrder.length - 1];
      if (!last) { last = newId('c'); chapters.set(last, { id: last, title: 'Recovered scenes', synopsis: '', notes: '' }); chapterOrder.push(last); }
      for (const s of orphans) s._parent = last;
    }

    // Both sides rewrote a scene: keep theirs as a new scene right after ours.
    const copyAfter = new Map();
    for (const { id, remote: rv } of ctx.contentConflicts) {
      const s = scenes.get(id);
      const copy = {
        ...clone(s), id: newId('s'), title: `${s.title || 'Untitled scene'} (other version)`, content: rv,
        notes: `Sync kept both versions: this scene was changed on two computers before they could sync. Keep the one you want (or combine them) and delete the other.${s.notes ? `\n\n${s.notes}` : ''}`,
      };
      scenes.set(copy.id, copy);
      copyAfter.set(id, copy.id);
      ctx.conflicts.push({ kind: 'scene', id, field: 'content', title: s.title, copy: copy.id });
    }

    out.manuscript = chapterOrder.map((cid) => {
      const keep = new Set([...scenes.values()].filter((s) => s._parent === cid && ![...copyAfter.values()].includes(s.id)).map((s) => s.id));
      const order = mergeOrder(childIds(base, cid), childIds(local, cid), childIds(remote, cid), keep);
      const withCopies = order.flatMap((id) => (copyAfter.has(id) ? [id, copyAfter.get(id)] : [id]));
      const ch = chapters.get(cid);
      return { ...ch, children: withCopies.map((id) => { const s = { ...scenes.get(id) }; delete s._parent; return s; }) };
    });

    return { project: out, conflicts: ctx.conflicts, copies: copyAfter.size };
  }

  /** True when two serialised stories hold the same work (ignores save-time bookkeeping). */
  function sameDoc(a, b) {
    const norm = (x) => {
      const p = typeof x === 'string' ? JSON.parse(x) : clone(x);
      delete p.app;
      if (p.meta) delete p.meta.modified;
      return stable(p);
    };
    return norm(a) === norm(b);
  }

  return { merge, mergeOrder, mergeIdSet, sameDoc, stable };
});
