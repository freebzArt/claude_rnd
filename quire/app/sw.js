/* Quire service worker (installed app only; a Quire.html opened from a file needs none).
 *
 * Cache first: once installed, Quire opens from this cache without touching the network at all,
 * so a dead connection, a captive Wi-Fi portal or a flaky plane link can't stop it loading.
 * Updates download in the background and are offered in the app ("Update") rather than forced.
 * The sync API (/api/...) is never cached. The build stamps CACHE with a hash of the app files. */
const CACHE = 'quire-dev';
const SHELL = ['./', 'index.html', 'styles.css', 'merge.js', 'docx.js', 'app.js', 'manifest.webmanifest', 'icon.svg', 'Quire.html'];

// Hosts often redirect /index.html to / (Cloudflare does). Browsers refuse to hand a redirected response
// to a page load, so store a clean copy of every file instead of the raw response.
async function precache() {
  const cache = await caches.open(CACHE);
  await Promise.all(SHELL.map(async (u) => {
    const res = await fetch(new Request(u, { cache: 'reload' }));
    if (!res.ok) throw new Error(`precache ${u}: ${res.status}`);
    const clean = res.redirected ? new Response(await res.blob(), { status: res.status, statusText: res.statusText, headers: res.headers }) : res;
    await cache.put(u, clean);
  }));
}
self.addEventListener('install', (e) => { e.waitUntil(precache()); });
self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k.startsWith('quire-') && k !== CACHE).map((k) => caches.delete(k))))
    .then(() => self.clients.claim()));
});
self.addEventListener('message', (e) => { if (e.data === 'skipWaiting') self.skipWaiting(); });

self.addEventListener('fetch', (e) => {
  const req = e.request;
  const url = new URL(req.url);
  if (req.method !== 'GET' || url.origin !== location.origin || url.pathname.startsWith('/api/')) return;
  if (req.mode === 'navigate') {
    e.respondWith(caches.match('index.html', { cacheName: CACHE }).then((hit) => hit || fetch(req)));
    return;
  }
  e.respondWith(caches.match(req, { cacheName: CACHE, ignoreSearch: true }).then((hit) => hit || fetch(req)));
});
