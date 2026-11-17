// Tests cloud/worker.js against an in-memory R2. Usage: node tools/test-worker.mjs
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { memoryBucket } from './dev-server.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const worker = (await import(pathToFileURL(join(root, 'cloud', 'worker.js')).href)).default;
const env = { QUIRE_TOKEN: 's3cret', STORIES: memoryBucket() };
const call = (path, { method = 'GET', body, headers = {}, token = 's3cret' } = {}) =>
  worker.fetch(new Request(`https://quire.example${path}`, { method, body, headers: { ...(token ? { Authorization: `Bearer ${token}` } : {}), ...headers } }), env);

let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'}  ${msg}`); if (!cond) failures++; };
const doc = (title, text) => JSON.stringify({ format: 'quire', version: 1, meta: { id: 'p1', title, modified: new Date().toISOString() }, manuscript: [{ id: 'c1', title: 'C', children: [{ id: 's1', content: `<p>${text}</p>` }] }] });

check((await call('/api/ping', { token: null })).status === 401, 'no token is rejected');
check((await call('/api/ping', { token: 'wrong' })).status === 401, 'wrong token is rejected');
check((await (await call('/api/ping')).json()).ok, 'ping');
const pre = await call('/api/stories', { method: 'OPTIONS', token: null });
check(pre.status === 204 && pre.headers.get('Access-Control-Allow-Headers').includes('If-Match'), 'CORS preflight works without a token');

const c1 = await call('/api/stories/p1', { method: 'PUT', body: doc('Book', 'one two three'), headers: { 'If-None-Match': '*' } });
const rev1 = c1.headers.get('ETag');
check(c1.status === 200 && /^".+"$/.test(rev1), `create returns a revision ${rev1}`);
check((await call('/api/stories/p1', { method: 'PUT', body: doc('Book', 'x'), headers: { 'If-None-Match': '*' } })).status === 412, 'create refuses to overwrite');
check((await call('/api/stories/p1', { method: 'PUT', body: doc('Book', 'x') })).status === 428, 'update without a precondition is refused');

const g = await call('/api/stories/p1');
check(g.status === 200 && g.headers.get('ETag') === rev1 && (await g.json()).meta.title === 'Book', 'get returns the story and its revision');
check((await call('/api/stories/p1', { headers: { 'If-None-Match': rev1 } })).status === 304, 'unchanged story answers 304');

const u2 = await call('/api/stories/p1', { method: 'PUT', body: doc('Book 2', 'one two three four'), headers: { 'If-Match': rev1 } });
const rev2 = u2.headers.get('ETag');
check(u2.status === 200 && rev2 !== rev1, 'update with the right revision');
check((await call('/api/stories/p1', { method: 'PUT', body: doc('Book 3', 'stale'), headers: { 'If-Match': rev1 } })).status === 412, 'update from a stale revision is refused (412)');

const list = await (await call('/api/stories')).json();
check(list.stories.length === 1 && list.stories[0].rev === rev2 && list.stories[0].title === 'Book 2' && list.stories[0].words === 4, `list shows title, words and current revision: ${JSON.stringify(list.stories[0])}`);

const hist = await (await call('/api/stories/p1/history')).json();
check(hist.versions.length === 1, `history keeps one version per 10 minutes (${hist.versions.length})`);
const v = await call(`/api/stories/p1/history/${encodeURIComponent(hist.versions[0].key)}`);
check(v.status === 200 && (await v.json()).meta.title === 'Book', 'an old version can be fetched');

check((await call('/api/stories/bad%20id')).status === 400, 'bad ids are refused');
check((await call('/api/stories/p2', { method: 'PUT', body: doc('x', 'y'), headers: { 'If-None-Match': '*' } })).status === 400, 'a body whose id does not match the URL is refused');
check((await call('/api/stories/p1', { method: 'PUT', body: 'not json', headers: { 'If-Match': rev2 } })).status === 400, 'non-JSON is refused');

check((await call('/api/stories/p1', { method: 'DELETE' })).status === 200, 'delete');
check((await call('/api/stories/p1')).status === 404 && [...env.STORIES._objects.keys()].some((k) => k.startsWith('trash/p1/')), 'deleted story goes to trash');

console.log(failures ? `\n${failures} worker check(s) failed` : '\nall worker checks passed');
process.exit(failures ? 1 : 0);
