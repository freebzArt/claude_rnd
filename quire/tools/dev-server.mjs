// Runs Quire's cloud locally: the real cloud/worker.js with an in-memory (or folder-backed) R2,
// serving dist/web. For trying sync before deploying, and for the automated sync tests.
//
//   node tools/build.mjs && node tools/dev-server.mjs [--port 8787] [--token secret] [--data ./cloud-data]
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join, extname, normalize } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.svg': 'image/svg+xml', '.webmanifest': 'application/manifest+json', '.json': 'application/json' };

/** Enough of the R2 binding API for worker.js. */
export function memoryBucket(file) {
  const objects = new Map(file && existsSync(file) ? Object.entries(JSON.parse(readFileSync(file, 'utf8'))) : []);
  const persist = () => { if (file) { mkdirSync(dirname(file), { recursive: true }); writeFileSync(file, JSON.stringify(Object.fromEntries(objects))); } };
  const meta = (key, o) => ({ key, etag: o.etag, httpEtag: `"${o.etag}"`, size: o.body.length, uploaded: new Date(o.uploaded), customMetadata: o.customMetadata || {} });
  return {
    async head(key) { const o = objects.get(key); return o ? meta(key, o) : null; },
    async get(key) { const o = objects.get(key); if (!o) return null; return { ...meta(key, o), body: o.body, text: async () => o.body }; },
    async put(key, value, opts = {}) {
      const cur = objects.get(key);
      if (opts.onlyIf?.etagMatches && (!cur || cur.etag !== opts.onlyIf.etagMatches)) return null;
      const body = typeof value === 'string' ? value : String(value);
      const o = { body, etag: createHash('md5').update(body).digest('hex'), uploaded: Date.now(), customMetadata: opts.customMetadata || {} };
      objects.set(key, o);
      persist();
      return meta(key, o);
    },
    async delete(keys) { for (const k of [].concat(keys)) objects.delete(k); persist(); },
    async list({ prefix = '' } = {}) {
      return { objects: [...objects.keys()].filter((k) => k.startsWith(prefix)).sort().map((k) => meta(k, objects.get(k))), truncated: false };
    },
    _objects: objects,
  };
}

export async function startServer({ port = 8787, token = 'dev-token', data = null, webDir = join(root, 'dist', 'web') } = {}) {
  const worker = (await import(pathToFileURL(join(root, 'cloud', 'worker.js')).href)).default;
  const bucket = memoryBucket(data ? join(data, 'r2.json') : null);
  const ASSETS = {
    async fetch(req) {
      const url = new URL(req.url);
      let p = normalize(decodeURIComponent(url.pathname)).replace(/^(\.\.[/\\])+/, '');
      if (p.endsWith('/')) p += 'index.html';
      const file = join(webDir, p);
      if (!file.startsWith(webDir) || !existsSync(file)) return new Response('Not found', { status: 404 });
      return new Response(readFileSync(file), { headers: { 'Content-Type': TYPES[extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-cache' } });
    },
  };
  const env = { QUIRE_TOKEN: token, STORIES: bucket, ASSETS };
  const server = createServer(async (req, res) => {
    const chunks = [];
    for await (const c of req) chunks.push(c);
    const body = chunks.length ? Buffer.concat(chunks) : undefined;
    const request = new Request(`http://${req.headers.host}${req.url}`, { method: req.method, headers: req.headers, body: ['GET', 'HEAD'].includes(req.method) ? undefined : body });
    const out = await worker.fetch(request, env);
    res.writeHead(out.status, Object.fromEntries(out.headers));
    res.end(out.body ? Buffer.from(await out.arrayBuffer()) : undefined);
  });
  await new Promise((r) => server.listen(port, '127.0.0.1', r));
  return { server, bucket, port: server.address().port, token, close: () => new Promise((r) => { server.closeAllConnections?.(); server.close(r); }) };
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i > 0 ? process.argv[i + 1] : d; };
  const s = await startServer({ port: +arg('port', 8787), token: arg('token', 'dev-token'), data: arg('data', null) });
  console.log(`Quire cloud (local) on http://localhost:${s.port}\n  sync server URL: http://localhost:${s.port}\n  sync key:        ${s.token}\n  storage:         ${arg('data', null) ? join(arg('data'), 'r2.json') : 'in memory (lost when stopped)'}`);
}
