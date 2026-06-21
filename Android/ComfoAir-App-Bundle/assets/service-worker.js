const CACHE_NAME = "comfoair-v2.0.3";
const ASSETS_TO_CACHE = [
  "/",
  "/indexSTA.html",
  "/indexAP.html",
  "/settings.html",
  "/ComfoAirDriver.html",
  "/ComfoAirSettings.html",
  "/MQTTSettings.html",
  "/log.html",
  "/error.html",
  "/success.html",
  "/WifiManager.css",
  "/Zehnder.png",
  "/manifest.json"
];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      return cache.addAll(ASSETS_TO_CACHE);
    })
  );
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((cacheNames) => {
      return Promise.all(
        cacheNames.map((cacheName) => {
          if (cacheName !== CACHE_NAME) {
            return caches.delete(cacheName);
          }
        })
      );
    })
  );
  self.clients.claim();
});

self.addEventListener("fetch", (event) => {
  // Nur GET-Requests cachen
  if (event.request.method !== "GET") {
    return;
  }

  // API-Calls nicht cachen (z.B. /api/comfo/state)
  if (event.request.url.includes("/api/")) {
    event.respondWith(
      fetch(event.request)
        .catch(() => {
          return new Response(
            JSON.stringify({ error: "Offline - API nicht erreichbar" }),
            { status: 503, headers: { "Content-Type": "application/json" } }
          );
        })
    );
    return;
  }

  // Assets aus Cache, bei Fehler Netzwerk
  event.respondWith(
    caches.match(event.request).then((response) => {
      return response || fetch(event.request).then((response) => {
        // Neuen Content in Cache speichern
        if (response && response.status === 200) {
          const clonedResponse = response.clone();
          caches.open(CACHE_NAME).then((cache) => {
            cache.put(event.request, clonedResponse);
          });
        }
        return response;
      });
    })
  );
});
