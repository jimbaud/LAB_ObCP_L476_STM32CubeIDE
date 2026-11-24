/* Une copie cohérente de l'application par version et par répertoire d'hébergement. */
const scopeURL = self.registration.scope;
const prefix = "obcp-control:" + scopeURL + ":";
const cacheName = prefix + "1.0.0";
const shell = new URL("index.html", scopeURL).href;
const assets = ["index.html","styles.css","protocol.js","charts.js","app.js","manifest.webmanifest",
  "assets/icon-192.png","assets/icon-512.png","assets/obcp.webp"].map(p => new URL(p,scopeURL).href);

self.addEventListener("install", event => {
  event.waitUntil(caches.open(cacheName).then(cache => cache.addAll(assets.map(url => new Request(url,{cache:"reload"})))));
});
self.addEventListener("activate", event => {
  event.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names.filter(name => name.startsWith(prefix) && name !== cacheName).map(name => caches.delete(name)));
    await self.clients.claim();
  })());
});
self.addEventListener("fetch", event => {
  const req = event.request;
  if (req.method !== "GET") return;
  const url = new URL(req.url);
  const home = url.origin + url.pathname === scopeURL || url.origin + url.pathname === shell;
  const key = home && req.mode === "navigate" ? shell : req.url;
  if (!assets.includes(key)) return;
  event.respondWith(caches.open(cacheName).then(async cache => (await cache.match(key)) || fetch(req)));
});
