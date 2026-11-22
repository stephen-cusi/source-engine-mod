# Cefrium 0.17.1 query and resource constraints

This records the behavior reviewed in the supplied Cefrium 0.17.1 sources.
It is not a claim that the Android game browser is ready to use.

- `CefriumBrowser.setQueryHandler` requires an explicit origin policy. Its
  default policy scope is the main frame; subframes require opt-in. Sandboxed
  and other opaque origins are rejected unless the application explicitly
  selects the unsafe all-origins policy.
- The SDK compares the sender's serialized origin with the frame's current
  committed origin before calling Java. This prevents a request from one
  origin being authorized using a different current origin, but an origin is
  not a document identifier: two successive same-origin documents compare
  equal. A Java navigation counter alone cannot authenticate a late request
  that was delivered after the counter changed.
- The native message router removes pending requests when CEF reports query
  cancellation. The public Java `QueryHandler` exposes a callback to reply but
  no cancellation callback identifying the corresponding request. Application
  state must therefore have bounded pending requests and reject stale replies;
  this does not by itself prove that every late request is from the current
  document.
- `addDocumentStartJavaScript` can register a persistent script before the
  first page load and can remove it for future documents. A script installed
  with a wildcard rule still runs on unrelated origins. Do not enable a Lua
  callback channel for those origins or use `anyOrigin()` as a fallback.
- The SDK's request interceptor returns a block/allow decision, not response
  data. Its Android file delegate serves only APK assets and resources. The
  SDK initializes the browser process without a custom CefApp; registering
  `asset` as a custom scheme in all relevant processes requires changes to the
  Cefrium native runtime and a separately implemented game VFS handler.

The launcher bridge provider remains disabled until these constraints and the
resource protocol have been resolved and verified on a device.
