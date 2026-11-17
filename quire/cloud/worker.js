/* Quire cloud: a Cloudflare Worker that serves the Quire app and a tiny sync API backed by R2.
 *
 *   GET    /api/ping                         -> { ok, version }
 *   GET    /api/stories                      -> { stories: [{ id, title, words, modified, rev, size, uploaded }] }
 *   GET    /api/stories/:id                  -> the .quire JSON, ETag = revision. If-None-Match -> 304
 *   PUT    /api/stories/:id                  -> store. If-Match: <rev> (update) or If-None-Match: * (create).
 *                                               412 if someone else saved first. Returns the new ETag.
 *   DELETE /api/stories/:id                  -> moves the story to trash/ (recoverable from the R2 dashboard)
 *   GET    /api/stories/:id/history          -> { versions: [{ key, at, size, words }] } newest first
 *   GET    /api/stories/:id/history/:key     -> an earlier version
 *
 * Auth: every /api request needs "Authorization: Bearer <QUIRE_TOKEN>" (a secret you set with
 * `wrangler secret put QUIRE_TOKEN`). Bearer tokens rather than cookies, so a Quire.html opened
 * from a local file can sync too. Everything else is served from the static app (env.ASSETS).
 */
const VERSION = '0.2.0';
const MAX_BYTES = 50 * 1024 * 1024;
const HISTORY_EVERY_MS = 10 * 60 * 1000; // keep at most one history version per 10 minutes...
const HISTORY_KEEP = 200; //            ...and the newest 200 of them

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, PUT, DELETE, OPTIONS',
  'Access-Control-Allow-Headers': 'Authorization, Content-Type, If-Match, If-None-Match',
  'Access-Control-Expose-Headers': 'ETag',
  'Access-Control-Max-Age': '86400',
};
const json = (obj, status = 200, headers = {}) => new Response(JSON.stringify(obj), {
  status, headers: { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ...CORS, ...headers },
});
const err = (status, message) => json({ error: message }, status);
const unquote = (s) => (s || '').trim().replace(/^W\//, '').replace(/^"|"$/g, '');
const storyKey = (id) => `stories/${id}.quire`;
const ID_RE = /^[A-Za-z0-9_-]{1,64}$/;

function sameSecret(a, b) {
  const x = new TextEncoder().encode(a), y = new TextEncoder().encode(b);
  let diff = x.length ^ y.length;
  for (let i = 0; i < Math.max(x.length, y.length); i++) diff |= (x[i] || 0) ^ (y[i] || 0);
  return diff === 0;
}

async function handleApi(req, env, url) {
  if (req.method === 'OPTIONS') return new Response(null, { status: 204, headers: CORS });
  if (!env.QUIRE_TOKEN) return err(500, 'The server has no QUIRE_TOKEN secret set.');
  const auth = req.headers.get('Authorization') || '';
  if (!auth.startsWith('Bearer ') || !sameSecret(auth.slice(7), env.QUIRE_TOKEN)) return err(401, 'Unauthorized');
  const bucket = env.STORIES;
  if (!bucket) return err(500, 'The server has no STORIES R2 bucket bound.');

  const parts = url.pathname.replace(/^\/api\/?/, '').split('/').filter(Boolean).map(decodeURIComponent);

  if (parts[0] === 'ping' && req.method === 'GET') return json({ ok: true, version: VERSION });

  if (parts[0] !== 'stories') return err(404, 'Not found');

  if (parts.length === 1 && req.method === 'GET') {
    const stories = [];
    let cursor;
    do {
      const page = await bucket.list({ prefix: 'stories/', cursor, include: ['customMetadata'] });
      for (const o of page.objects) {
        const meta = o.customMetadata || {};
        stories.push({ id: o.key.slice(8, -6), title: meta.title || '', words: +meta.words || 0, modified: meta.modified || '', rev: o.httpEtag, size: o.size, uploaded: o.uploaded });
      }
      cursor = page.truncated ? page.cursor : undefined;
    } while (cursor);
    stories.sort((a, b) => String(b.modified).localeCompare(String(a.modified)));
    return json({ stories });
  }

  const id = parts[1];
  if (!id || !ID_RE.test(id)) return err(400, 'Bad story id');
  const key = storyKey(id);

  if (parts.length === 2 && req.method === 'GET') {
    const obj = await bucket.get(key);
    if (!obj) return err(404, 'No such story');
    if (unquote(req.headers.get('If-None-Match')) === obj.etag) return new Response(null, { status: 304, headers: { ETag: obj.httpEtag, ...CORS } });
    return new Response(obj.body, { headers: { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ETag: obj.httpEtag, ...CORS } });
  }

  if (parts.length === 2 && req.method === 'PUT') {
    const text = await req.text();
    if (text.length > MAX_BYTES) return err(413, 'Story too large');
    let doc;
    try { doc = JSON.parse(text); } catch { return err(400, 'Not JSON'); }
    if (!doc || doc.format !== 'quire' || !doc.meta || doc.meta.id !== id) return err(400, 'Not a Quire story, or the id does not match');

    const ifMatch = unquote(req.headers.get('If-Match'));
    const createOnly = (req.headers.get('If-None-Match') || '').trim() === '*';
    const current = await bucket.head(key);
    if (createOnly && current) return err(412, 'Story already exists');
    if (!createOnly && !ifMatch) return err(428, 'If-Match or If-None-Match: * required');
    if (ifMatch && (!current || current.etag !== ifMatch)) return json({ error: 'Changed by someone else' }, 412, current ? { ETag: current.httpEtag } : {});

    const words = countWords(doc);
    const now = Date.now();
    const lastHistory = +(current?.customMetadata?.lastHistory || 0);
    const takeHistory = !current || now - lastHistory >= HISTORY_EVERY_MS;
    const customMetadata = {
      title: String(doc.meta.title || '').slice(0, 200), words: String(words), modified: String(doc.meta.modified || ''),
      lastHistory: String(takeHistory ? now : lastHistory),
    };
    const put = await bucket.put(key, text, {
      httpMetadata: { contentType: 'application/json' }, customMetadata,
      onlyIf: current ? { etagMatches: current.etag } : undefined,
    });
    if (!put) return err(412, 'Changed by someone else');
    if (takeHistory) {
      await bucket.put(`history/${id}/${new Date(now).toISOString()}.quire`, text, { customMetadata: { words: String(words) } });
      await pruneHistory(bucket, id);
    }
    return json({ ok: true, rev: put.httpEtag }, 200, { ETag: put.httpEtag });
  }

  if (parts.length === 2 && req.method === 'DELETE') {
    const obj = await bucket.get(key);
    if (!obj) return err(404, 'No such story');
    await bucket.put(`trash/${id}/${new Date().toISOString()}.quire`, await obj.text());
    await bucket.delete(key);
    return json({ ok: true });
  }

  if (parts[2] === 'history' && req.method === 'GET') {
    if (parts.length === 3) {
      const versions = [];
      let cursor;
      do {
        const page = await bucket.list({ prefix: `history/${id}/`, cursor, include: ['customMetadata'] });
        for (const o of page.objects) versions.push({ key: o.key.split('/').pop().replace(/\.quire$/, ''), at: o.key.split('/').pop().replace(/\.quire$/, ''), size: o.size, words: +(o.customMetadata?.words || 0) });
        cursor = page.truncated ? page.cursor : undefined;
      } while (cursor);
      versions.sort((a, b) => b.at.localeCompare(a.at));
      return json({ versions });
    }
    if (parts.length === 4) {
      const obj = await bucket.get(`history/${id}/${parts[3]}.quire`);
      if (!obj) return err(404, 'No such version');
      return new Response(obj.body, { headers: { 'Content-Type': 'application/json; charset=utf-8', ...CORS } });
    }
  }
  return err(405, 'Method not allowed');
}

async function pruneHistory(bucket, id) {
  const keys = [];
  let cursor;
  do {
    const page = await bucket.list({ prefix: `history/${id}/`, cursor });
    keys.push(...page.objects.map((o) => o.key));
    cursor = page.truncated ? page.cursor : undefined;
  } while (cursor);
  keys.sort();
  const extra = keys.slice(0, Math.max(0, keys.length - HISTORY_KEEP));
  if (extra.length) await bucket.delete(extra);
}

function countWords(doc) {
  let n = 0;
  for (const c of doc.manuscript || []) {
    for (const s of c.children || []) {
      const text = String(s.content || '').replace(/<[^>]*>/g, ' ').replace(/&[a-z#0-9]+;/gi, ' ');
      n += (text.match(/[\p{L}\p{N}]+(?:['’-][\p{L}\p{N}]+)*/gu) || []).length;
    }
  }
  return n;
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    if (url.pathname === '/api' || url.pathname.startsWith('/api/')) {
      try { return await handleApi(req, env, url); } catch (e) { return err(500, String(e && e.message || e)); }
    }
    if (env.ASSETS) return env.ASSETS.fetch(req);
    return new Response('Not found', { status: 404 });
  },
};
