// Unit tests for app/merge.js. Usage: node tools/test-merge.mjs
import { createRequire } from 'node:module';
const { merge, sameDoc } = createRequire(import.meta.url)('../app/merge.js');

let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'}  ${msg}`); if (!cond) failures++; };
const clone = (x) => JSON.parse(JSON.stringify(x));
const P = (t) => `<p>${t}</p>`;

function story() {
  return {
    format: 'quire', version: 1, app: 'Quire test',
    meta: { id: 'p1', title: 'Book', author: '', modified: '2026-01-01T00:00:00Z', wordGoal: 1000 },
    manuscript: [
      { id: 'c1', title: 'One', synopsis: '', notes: '', children: [
        { id: 's1', title: 'A', status: 'draft', synopsis: '', notes: '', pov: '', characters: ['x'], location: '', threads: [], content: P('alpha') },
        { id: 's2', title: 'B', status: 'draft', synopsis: '', notes: '', pov: '', characters: [], location: '', threads: [], content: P('bravo') },
        { id: 's3', title: 'C', status: 'draft', synopsis: '', notes: '', pov: '', characters: [], location: '', threads: [], content: P('charlie') },
      ] },
      { id: 'c2', title: 'Two', synopsis: '', notes: '', children: [
        { id: 's4', title: 'D', status: 'draft', synopsis: '', notes: '', pov: '', characters: [], location: '', threads: [], content: P('delta') },
      ] },
    ],
    characters: [{ id: 'x', name: 'Xan', summary: 'old', notes: '' }, { id: 'y', name: 'Yara', summary: '', notes: '' }],
    locations: [], threads: [{ id: 't1', name: 'Plot', summary: '' }], notes: [],
    stats: { daily: { '2026-01-01': { start: 0, end: 10 } } },
  };
}
const scene = (p, id) => p.manuscript.flatMap((c) => c.children).find((s) => s.id === id);
const order = (p) => p.manuscript.map((c) => `${c.id}:${c.children.map((s) => s.id).join(',')}`).join(' ');

{ // 1. one side edits, other untouched
  const b = story(), l = clone(b), r = clone(b);
  scene(r, 's2').content = P('bravo remote');
  const m = merge(b, l, r);
  check(scene(m.project, 's2').content === P('bravo remote') && !m.conflicts.length, 'remote-only edit is taken');
}
{ // 2. both edit different scenes
  const b = story(), l = clone(b), r = clone(b);
  scene(l, 's1').content = P('alpha local');
  scene(r, 's3').content = P('charlie remote');
  const m = merge(b, l, r);
  check(scene(m.project, 's1').content === P('alpha local') && scene(m.project, 's3').content === P('charlie remote') && !m.conflicts.length, 'edits to different scenes both survive');
}
{ // 3. both edit the same scene: keep both
  const b = story(), l = clone(b), r = clone(b);
  scene(l, 's2').content = P('bravo local');
  scene(r, 's2').content = P('bravo remote');
  const m = merge(b, l, r);
  const kids = m.project.manuscript[0].children;
  check(kids[1].content === P('bravo local') && kids[2].content === P('bravo remote') && kids[2].title === 'B (other version)' && m.copies === 1,
    `same scene edited twice keeps both, side by side: ${kids.map((s) => s.title).join(' | ')}`);
}
{ // 4. additions on both sides land in the right places
  const b = story(), l = clone(b), r = clone(b);
  l.manuscript[0].children.splice(1, 0, { ...clone(scene(b, 's1')), id: 'sL', title: 'new local', content: P('l') });
  r.manuscript[1].children.unshift({ ...clone(scene(b, 's4')), id: 'sR', title: 'new remote', content: P('r') });
  const m = merge(b, l, r);
  check(order(m.project) === 'c1:s1,sL,s2,s3 c2:sR,s4', `additions from both sides are placed: ${order(m.project)}`);
}
{ // 5. deletes
  const b = story(), l = clone(b), r = clone(b);
  r.manuscript[0].children = r.manuscript[0].children.filter((s) => s.id !== 's3'); // remote deletes C
  r.manuscript[0].children = r.manuscript[0].children.filter((s) => s.id !== 's2'); // remote deletes B...
  scene(l, 's2').content = P('bravo edited'); // ...but local edited B
  const m = merge(b, l, r);
  check(!scene(m.project, 's3') && scene(m.project, 's2')?.content === P('bravo edited'), 'delete of an untouched scene wins; delete of an edited scene does not');
}
{ // 6. remote reorders, local edits text
  const b = story(), l = clone(b), r = clone(b);
  r.manuscript[0].children.reverse();
  scene(l, 's1').content = P('alpha local');
  const m = merge(b, l, r);
  check(order(m.project) === 'c1:s3,s2,s1 c2:s4' && scene(m.project, 's1').content === P('alpha local'), `remote reorder plus local edit: ${order(m.project)}`);
}
{ // 7. scene moved to another chapter remotely, edited locally
  const b = story(), l = clone(b), r = clone(b);
  const [moved] = r.manuscript[0].children.splice(0, 1);
  r.manuscript[1].children.push(moved);
  scene(l, 's1').content = P('alpha local');
  const m = merge(b, l, r);
  check(order(m.project) === 'c1:s2,s3 c2:s4,s1' && scene(m.project, 's1').content === P('alpha local'), `move plus edit: ${order(m.project)}`);
}
{ // 8. tag sets
  const b = story(), l = clone(b), r = clone(b);
  scene(l, 's1').characters.push('y');
  scene(r, 's1').characters = [];
  scene(r, 's1').threads = ['t1'];
  const m = merge(b, l, r);
  const s = scene(m.project, 's1');
  check(JSON.stringify(s.characters) === '["y"]' && JSON.stringify(s.threads) === '["t1"]', `tag additions and removals merge: ${JSON.stringify(s.characters)} ${JSON.stringify(s.threads)}`);
}
{ // 9. no common base
  const l = story(), r = story();
  scene(r, 's4').content = P('delta remote');
  r.manuscript[1].children.push({ ...clone(scene(r, 's4')), id: 's5', title: 'E', content: P('echo') });
  const m = merge(null, l, r);
  check(scene(m.project, 's5') && m.copies === 1 && order(m.project).startsWith('c1:s1,s2,s3 c2:s4,'), `without a base nothing is lost: ${order(m.project)}`);
}
{ // 10. chapter deleted remotely, scene added to it locally
  const b = story(), l = clone(b), r = clone(b);
  r.manuscript.splice(1, 1);
  l.manuscript[1].children.push({ ...clone(scene(b, 's4')), id: 's9', title: 'new', content: P('new') });
  const m = merge(b, l, r);
  check(m.project.manuscript.length === 2 && scene(m.project, 's9') && !scene(m.project, 's4'), `chapter comes back for new work; untouched scene stays deleted: ${order(m.project)}`);
}
{ // 11. both edit a character summary
  const b = story(), l = clone(b), r = clone(b);
  l.characters[0].summary = 'local idea';
  r.characters[0].summary = 'remote idea';
  const m = merge(b, l, r);
  check(m.project.characters[0].summary.includes('local idea') && m.project.characters[0].summary.includes('remote idea'), 'conflicting summaries are both kept');
}
{ // 12. meta and stats
  const b = story(), l = clone(b), r = clone(b);
  r.meta.title = 'New title';
  r.meta.modified = '2026-02-01T00:00:00Z';
  l.stats.daily['2026-01-02'] = { start: 10, end: 50 };
  r.stats.daily['2026-01-01'] = { start: 0, end: 30 };
  const m = merge(b, l, r);
  check(m.project.meta.title === 'New title' && m.project.meta.modified === '2026-02-01T00:00:00Z' && m.project.stats.daily['2026-01-02'].end === 50 && m.project.stats.daily['2026-01-01'].end === 30, 'meta and daily stats merge');
}
{ // 13. identical sides, and sameDoc
  const b = story(), l = clone(b);
  l.meta.modified = 'later';
  l.app = 'other';
  check(sameDoc(b, l) && !sameDoc(b, { ...l, meta: { ...l.meta, title: 'x' } }), 'sameDoc ignores save-time fields');
  const m = merge(b, l, clone(l));
  check(JSON.stringify(m.project) === JSON.stringify(l), 'merging identical copies changes nothing');
}
{ // 14. unknown fields from a newer version survive
  const b = story(), l = clone(b), r = clone(b);
  r.futureThing = { a: 1 };
  scene(r, 's1').mood = 'tense';
  const m = merge(b, l, r);
  check(m.project.futureThing?.a === 1 && scene(m.project, 's1').mood === 'tense', 'unknown fields are kept');
}

console.log(failures ? `\n${failures} merge check(s) failed` : '\nall merge checks passed');
process.exit(failures ? 1 : 0);
