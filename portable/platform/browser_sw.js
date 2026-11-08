// Offline cache for the browser build: the page and engine load from the
// network when it is available (so updates arrive) and from this cache when not.
// Game content never passes through here; it stays in IndexedDB.
const CACHE = 'mksc-neo-engine-v1';
const FILES = ['mksc.html', 'mksc.js', 'mksc.wasm'];

self.addEventListener('install', event => {
  event.waitUntil(caches.open(CACHE).then(cache => cache.addAll(FILES.map(file => new Request(file, {cache: 'no-cache'})))).then(() => self.skipWaiting()));
});

self.addEventListener('activate', event => {
  event.waitUntil(caches.keys()
    .then(keys => Promise.all(keys.filter(key => key !== CACHE).map(key => caches.delete(key))))
    .then(() => self.clients.claim()));
});

self.addEventListener('fetch', event => {
  const request = event.request;
  if (request.method !== 'GET' || new URL(request.url).origin !== self.location.origin) return;
  // Revalidate with the server so a new release is picked up on the next load.
  event.respondWith(fetch(request, {cache: 'no-cache'}).then(response => {
    if (response.ok) {
      const copy = response.clone();
      caches.open(CACHE).then(cache => cache.put(request, copy));
    }
    return response;
  }).catch(() => caches.match(request, {ignoreSearch: true})
    .then(cached => cached || Response.error())));
});
