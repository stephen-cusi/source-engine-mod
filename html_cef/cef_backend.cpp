#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <windows.h>

#include "html_backend_abi.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_parser.h"
#include "include/cef_process_message.h"
#include "include/cef_resource_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_scheme.h"

struct HtmlView;

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  void OnContextInitialized() override { ready = true; }
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
    registrar->AddCustomScheme("asset", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                                        CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
  }
  bool ready = false;
 private:
  IMPLEMENT_REFCOUNTING(BrowserApp);
};

struct HtmlBackend {
  CefRefPtr<BrowserApp> app;
  std::vector<HtmlView *> views;
};

struct QueuedEvent {
  uint32_t type;
  uint64_t id;
  int32_t code;
  std::string a;
  std::string b;
};

class AssetHandler;
struct SharedState {
  std::mutex mutex;
  std::deque<QueuedEvent> events;
  std::map<uint64_t, CefRefPtr<AssetHandler>> requests;
  std::map<uint64_t, std::pair<CefRefPtr<CefFrame>, std::string>> callbacks;
  std::set<std::pair<std::string, std::string>> bindings;
  uint64_t next_id = 1;
  bool closed = false;
};
static std::mutex g_browser_mutex;
static std::map<int, std::weak_ptr<SharedState>> g_browsers;

class BrowserClient final : public CefClient,
                            public CefLifeSpanHandler,
                            public CefRenderHandler,
                            public CefLoadHandler,
                            public CefRequestHandler,
                            public CefDisplayHandler {
 public:
  explicit BrowserClient(HtmlView *view) : view_(view) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request,
                      bool, bool) override {
    const std::string url = request->GetURL().ToString();
    CefURLParts parts;
    if (!CefParseURL(url, parts)) return true;
    const std::string scheme = CefString(&parts.scheme).ToString();
    return scheme != "http" && scheme != "https" && scheme != "asset" &&
           scheme != "data" && url != "about:blank";
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                                CefProcessId source, CefRefPtr<CefProcessMessage> message) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect &rect) override;
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList &rects,
               const void *buffer, int width, int height) override;
  void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, TransitionType) override;
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) override;
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                   const CefString &text, const CefString &url) override;
  void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                       const CefString &url) override;
  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString &title) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t severity,
                        const CefString &message, const CefString &source, int line) override;
 private:
  HtmlView *view_;
  IMPLEMENT_REFCOUNTING(BrowserClient);
};

struct HtmlView {
  HtmlBackend *backend;
  uint32_t width;
  uint32_t height;
  uint32_t frame_width = 0;
  uint32_t frame_height = 0;
  bool starting = false;
  bool closing = false;
  bool document_ready = false;
  bool end_pending = false;
  int end_status = 0;
  uint64_t end_time = 0;
  std::string end_url;
  uint64_t sequence = 0;
  std::string url = "about:blank";
  std::string starting_url;
  CefRefPtr<CefBrowser> browser;
  CefRefPtr<BrowserClient> client;
  std::vector<uint8_t> pixels;
  std::vector<HtmlRect> dirty;
  std::shared_ptr<SharedState> shared = std::make_shared<SharedState>();
  QueuedEvent current{};
};

static void Event(HtmlView *v, uint32_t type, int32_t code, const std::string &a,
                  const std::string &b = {}, uint64_t id = 0) {
  std::lock_guard<std::mutex> lock(v->shared->mutex);
  v->shared->events.push_back({type, id, code, a, b});
}

class AssetHandler final : public CefResourceHandler {
 public:
  explicit AssetHandler(std::shared_ptr<SharedState> state) : state_(std::move(state)) {}
  bool Open(CefRefPtr<CefRequest> request, bool &handle, CefRefPtr<CefCallback> cb) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || state_->requests.size() >= 512) { handle = true; return false; }
    handle = false;
    callback_ = cb;
    id_ = state_->next_id++;
    state_->requests[id_] = this;
    state_->events.push_back({HTML_EVENT_ASSET_REQUEST, id_, 0, request->GetURL().ToString(), {}});
    return true;
  }
  void Complete(int status, const char *mime, const uint8_t *data, size_t length) {
    CefRefPtr<CefCallback> callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!callback_) return;
      status_ = status;
      mime_ = mime ? mime : "application/octet-stream";
      if (data && length) data_.assign(data, data + length);
      callback.swap(callback_);
    }
    callback->Continue();
  }
  void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t &length, CefString &) override {
    std::lock_guard<std::mutex> lock(mutex_);
    response->SetStatus(status_);
    const size_t separator = mime_.find(';');
    std::string type = mime_.substr(0, separator);
    const size_t last = type.find_last_not_of(" \t");
    if (last != std::string::npos) type.resize(last + 1);
    response->SetMimeType(type);
    if (separator != std::string::npos) {
      const size_t charset = mime_.find("charset=", separator + 1);
      if (charset != std::string::npos) {
        const size_t begin = charset + 8;
        const size_t end = mime_.find_first_of("; \t", begin);
        response->SetCharset(mime_.substr(begin, end - begin));
      }
    }
    length = static_cast<int64_t>(data_.size());
  }
  bool Read(void *out, int limit, int &read, CefRefPtr<CefResourceReadCallback>) override {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = std::min(static_cast<size_t>(std::max(0, limit)), data_.size() - offset_);
    if (!count) { read = 0; return false; }
    std::memcpy(out, data_.data() + offset_, count);
    offset_ += count;
    read = static_cast<int>(count);
    return true;
  }
  void Cancel() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->requests.erase(id_);
  }
 private:
  std::shared_ptr<SharedState> state_;
  std::mutex mutex_;
  CefRefPtr<CefCallback> callback_;
  uint64_t id_ = 0;
  int status_ = 404;
  size_t offset_ = 0;
  std::string mime_;
  std::vector<uint8_t> data_;
  IMPLEMENT_REFCOUNTING(AssetHandler);
};

class AssetFactory final : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
                                       const CefString &, CefRefPtr<CefRequest>) override {
    if (!browser) return nullptr;
    std::lock_guard<std::mutex> lock(g_browser_mutex);
    auto it = g_browsers.find(browser->GetIdentifier());
    auto state = it == g_browsers.end() ? nullptr : it->second.lock();
    return state ? new AssetHandler(std::move(state)) : nullptr;
  }
 private:
  IMPLEMENT_REFCOUNTING(AssetFactory);
};

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  view_->browser = browser;
  {
    std::lock_guard<std::mutex> lock(g_browser_mutex);
    g_browsers[browser->GetIdentifier()] = view_->shared;
  }
  if (view_->closing) browser->GetHost()->CloseBrowser(true);
  else if (view_->url != view_->starting_url) browser->GetMainFrame()->LoadURL(view_->url);
}
void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  {
    std::lock_guard<std::mutex> lock(g_browser_mutex);
    g_browsers.erase(browser->GetIdentifier());
  }
  view_->browser = nullptr;
  view_->starting = false;
}
void BrowserClient::GetViewRect(CefRefPtr<CefBrowser>, CefRect &rect) {
  rect = CefRect(0, 0, std::max(1u, view_->width), std::max(1u, view_->height));
}
void BrowserClient::OnPaint(CefRefPtr<CefBrowser>, PaintElementType type,
                            const RectList &rects, const void *buffer, int width, int height) {
  if (type != PET_VIEW || !buffer || width <= 0 || height <= 0) return;
  const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  const uint8_t *begin = static_cast<const uint8_t *>(buffer);
  view_->pixels.assign(begin, begin + size);
  view_->frame_width = static_cast<uint32_t>(width);
  view_->frame_height = static_cast<uint32_t>(height);
  view_->dirty.clear();
  for (const CefRect &r : rects) view_->dirty.push_back({r.x, r.y, r.width, r.height});
  ++view_->sequence;
}
void BrowserClient::OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, TransitionType) {
  if (frame->IsMain()) {
    view_->document_ready = false;
    view_->end_pending = false;
    {
      std::lock_guard<std::mutex> lock(view_->shared->mutex);
      view_->shared->callbacks.clear();
      auto &events = view_->shared->events;
      events.erase(std::remove_if(events.begin(), events.end(), [](const QueuedEvent &event) {
        return event.type == HTML_EVENT_JS_CALL;
      }), events.end());
    }
    Event(view_, HTML_EVENT_LOAD_START, 0, frame->GetURL().ToString());
  }
}
bool BrowserClient::OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                                             CefProcessId source, CefRefPtr<CefProcessMessage> message) {
  if (source == PID_RENDERER && message->GetName() == "hl2sb.ready") {
    if (frame->IsMain() && !view_->document_ready) {
      view_->document_ready = true;
      Event(view_, HTML_EVENT_DOCUMENT_READY, 0, frame->GetURL().ToString());
      if (view_->end_pending) {
        Event(view_, HTML_EVENT_LOAD_END, view_->end_status, view_->end_url);
        view_->end_pending = false;
      }
    }
    return true;
  }
  if (source != PID_RENDERER || message->GetName() != "hl2sb.call") return false;
  auto args = message->GetArgumentList();
  std::string json = args->GetString(0).ToString();
  if (json.size() > 65536) return true;
  CefRefPtr<CefValue> parsed = CefParseJSON(json, JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_LIST) return true;
  auto list = parsed->GetList();
  if (list->GetSize() != 3 || list->GetType(0) != VTYPE_STRING ||
      list->GetType(1) != VTYPE_STRING || list->GetType(2) != VTYPE_LIST) return true;
  std::lock_guard<std::mutex> lock(view_->shared->mutex);
  auto key = std::make_pair(list->GetString(0).ToString(), list->GetString(1).ToString());
  if (view_->shared->closed || !view_->shared->bindings.count(key) ||
      view_->shared->callbacks.size() >= 512) return true;
  uint64_t id = view_->shared->next_id++;
  view_->shared->callbacks[id] = {frame, args->GetString(1).ToString()};
  view_->shared->events.push_back({HTML_EVENT_JS_CALL, id, 0, std::move(json), {}});
  return true;
}
void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) {
  if (frame->IsMain()) {
    if (view_->document_ready) Event(view_, HTML_EVENT_LOAD_END, status, frame->GetURL().ToString());
    else {
      view_->end_pending = true;
      view_->end_status = status;
      view_->end_url = frame->GetURL().ToString();
      view_->end_time = GetTickCount64();
    }
  }
}
void BrowserClient::OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                                const CefString &text, const CefString &url) {
  if (frame->IsMain()) Event(view_, HTML_EVENT_LOAD_ERROR, code, url.ToString(), text.ToString());
}
void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                                    const CefString &url) {
  if (frame->IsMain()) Event(view_, HTML_EVENT_ADDRESS_CHANGED, 0, url.ToString());
}
void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser>, const CefString &title) {
  Event(view_, HTML_EVENT_TITLE, 0, title.ToString());
}
bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t severity,
                                     const CefString &message, const CefString &source, int line) {
  Event(view_, HTML_EVENT_CONSOLE, static_cast<int32_t>(severity), message.ToString(), source.ToString(),
        static_cast<uint32_t>(line));
  return false;
}

static HtmlBackend *Create(const char *cache, const char *helper) {
  if (!helper || !*helper) return nullptr;
  CefMainArgs args(GetModuleHandleW(nullptr));
  CefSettings settings;
  settings.no_sandbox = true;
  settings.windowless_rendering_enabled = true;
  CefString(&settings.user_agent) = "Mozilla/5.0 (Windows NT; Valve Source Client) "
      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.198 Safari/537.36 GMod/13";
  CefString(&settings.browser_subprocess_path) = helper;
  std::wstring helper_path = CefString(helper).ToWString();
  const auto separator = helper_path.find_last_of(L"\\/");
  if (separator != std::wstring::npos) {
    std::wstring directory = helper_path.substr(0, separator);
    CefString(&settings.resources_dir_path) = directory;
    CefString(&settings.locales_dir_path) = directory + L"\\locales";
  }
  if (cache && *cache) {
    CefString(&settings.root_cache_path) = cache;
    CefString(&settings.cache_path) = cache;
  }
  HtmlBackend *backend = new HtmlBackend;
  backend->app = new BrowserApp;
  if (!CefInitialize(args, settings, backend->app, nullptr)) {
    delete backend;
    return nullptr;
  }
  CefRegisterSchemeHandlerFactory("asset", "", new AssetFactory);
  return backend;
}

static void Start(HtmlView *v) {
  if (v->starting || v->browser || v->closing) return;
  CefWindowInfo window;
  window.SetAsWindowless(nullptr);
  CefBrowserSettings settings;
  v->client = new BrowserClient(v);
  auto info = CefDictionaryValue::Create();
  auto bindings = CefListValue::Create();
  {
    std::lock_guard<std::mutex> lock(v->shared->mutex);
    size_t i = 0;
    for (const auto &binding : v->shared->bindings) {
      bindings->SetString(i++, binding.first);
      bindings->SetString(i++, binding.second);
    }
  }
  info->SetList("hl2sb.bindings", bindings);
  v->starting_url = v->url;
  v->starting = CefBrowserHost::CreateBrowser(window, v->client, v->url, settings, info, nullptr);
}
static void Pump(HtmlBackend *b) {
  if (!b) return;
  CefDoMessageLoopWork();
  if (b->app->ready) for (HtmlView *v : b->views) {
    Start(v);
    if (v->end_pending && GetTickCount64() - v->end_time >= 2000) {
      Event(v, HTML_EVENT_LOAD_END, v->end_status, v->end_url);
      v->end_pending = false;
    }
  }
}
static HtmlView *ViewCreate(HtmlBackend *b, uint32_t width, uint32_t height) {
  if (!b) return nullptr;
  HtmlView *v = new HtmlView{};
  v->backend = b;
  v->width = std::max(1u, width);
  v->height = std::max(1u, height);
  b->views.push_back(v);
  return v;
}
static void ViewDestroy(HtmlView *v) {
  if (!v) return;
  v->closing = true;
  {
    std::lock_guard<std::mutex> lock(v->shared->mutex);
    v->shared->closed = true;
    v->shared->requests.clear();
    v->shared->callbacks.clear();
  }
  if (v->browser) v->browser->GetHost()->CloseBrowser(true);
  while (v->starting) CefDoMessageLoopWork();
  auto &views = v->backend->views;
  views.erase(std::remove(views.begin(), views.end(), v), views.end());
  delete v;
}
static void Destroy(HtmlBackend *b) {
  if (!b) return;
  while (!b->views.empty()) ViewDestroy(b->views.back());
  b->app = nullptr;
  CefShutdown();
  delete b;
}
static void Resize(HtmlView *v, uint32_t width, uint32_t height) {
  if (!v) return;
  width = std::max(1u, width);
  height = std::max(1u, height);
  if (width == v->width && height == v->height) return;
  v->width = width;
  v->height = height;
  if (v->browser) v->browser->GetHost()->WasResized();
}
static void Navigate(HtmlView *v, const char *url) {
  if (!v || !url) return;
  v->url = url;
  if (v->browser) v->browser->GetMainFrame()->LoadURL(url);
}
static void Back(HtmlView *v) { if (v && v->browser) v->browser->GoBack(); }
static void Forward(HtmlView *v) { if (v && v->browser) v->browser->GoForward(); }
static void Reload(HtmlView *v) { if (v && v->browser) v->browser->Reload(); }
static void Stop(HtmlView *v) { if (v && v->browser) v->browser->StopLoad(); }
static void Eval(HtmlView *v, const char *js) {
  if (v && v->browser && js) v->browser->GetMainFrame()->ExecuteJavaScript(js, v->browser->GetMainFrame()->GetURL(), 0);
}
static void RegisterJS(HtmlView *v, const char *object, const char *function) {
  if (!v || !object || !function) return;
  {
    std::lock_guard<std::mutex> lock(v->shared->mutex);
    v->shared->bindings.emplace(object, function);
  }
  if (!v->browser) return;
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("hl2sb.register");
  message->GetArgumentList()->SetString(0, object);
  message->GetArgumentList()->SetString(1, function);
  v->browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
}
static void Reply(HtmlView *v, uint64_t id, const char *json) {
  if (!v) return;
  std::pair<CefRefPtr<CefFrame>, std::string> target;
  {
    std::lock_guard<std::mutex> lock(v->shared->mutex);
    auto it = v->shared->callbacks.find(id);
    if (it == v->shared->callbacks.end()) return;
    target = it->second;
    v->shared->callbacks.erase(it);
  }
  if (!target.first || !target.first->IsValid()) return;
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("hl2sb.reply");
  message->GetArgumentList()->SetString(0, target.second);
  message->GetArgumentList()->SetString(1, json ? json : "[]");
  target.first->SendProcessMessage(PID_RENDERER, message);
}
static void AssetReply(HtmlView *v, uint64_t id, int32_t status, const char *mime,
                       const uint8_t *data, size_t length) {
  if (!v) return;
  CefRefPtr<AssetHandler> handler;
  {
    std::lock_guard<std::mutex> lock(v->shared->mutex);
    auto it = v->shared->requests.find(id);
    if (it == v->shared->requests.end()) return;
    handler = it->second;
    v->shared->requests.erase(it);
  }
  handler->Complete(status, mime, data, length);
}
static int Poll(HtmlView *v, HtmlEvent *out) {
  if (!v || !out) return 0;
  std::lock_guard<std::mutex> lock(v->shared->mutex);
  if (v->shared->events.empty()) return 0;
  v->current = std::move(v->shared->events.front());
  v->shared->events.pop_front();
  *out = {v->current.type, v->current.id, v->current.code,
          v->current.a.c_str(), v->current.b.c_str()};
  return 1;
}
static void Mouse(HtmlView *v, int32_t type, int32_t x, int32_t y, int32_t button,
                  uint32_t modifiers, int32_t dx, int32_t dy) {
  if (!v || !v->browser) return;
  CefMouseEvent event;
  event.x = x; event.y = y; event.modifiers = modifiers;
  CefRefPtr<CefBrowserHost> host = v->browser->GetHost();
  if (type == 0) host->SendMouseMoveEvent(event, false);
  else if (type == 1 || type == 2) {
    auto b = button == 1 ? MBT_MIDDLE : button == 2 ? MBT_RIGHT : MBT_LEFT;
    if (type == 1) host->SetFocus(true);
    host->SendMouseClickEvent(event, b, type == 2, 1);
  } else if (type == 3) host->SendMouseWheelEvent(event, dx, dy);
}
static void Key(HtmlView *v, int32_t type, int32_t native, int32_t key,
                uint32_t modifiers, uint32_t character) {
  if (!v || !v->browser) return;
  CefKeyEvent event;
  event.type = type == 2 ? KEYEVENT_CHAR : type == 1 ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;
  event.native_key_code = native;
  event.windows_key_code = type == 2 ? character : key;
  event.character = static_cast<char16_t>(character);
  event.unmodified_character = static_cast<char16_t>(character);
  event.modifiers = modifiers;
  v->browser->GetHost()->SendKeyEvent(event);
}
static void Ime(HtmlView *v, int32_t stage, const char *utf8) {
  if (!v || !v->browser) return;
  auto host = v->browser->GetHost();
  if (stage == 0) return;
  CefString text(utf8 ? utf8 : "");
  if (stage == 1) {
    const uint32_t length = static_cast<uint32_t>(text.length());
    host->ImeSetComposition(text, {}, CefRange(UINT32_MAX, UINT32_MAX), CefRange(length, length));
  } else if (stage == 2) {
    host->ImeCommitText(text, CefRange(UINT32_MAX, UINT32_MAX), 0);
  }
}
static int Lock(HtmlView *v, HtmlFrame *frame) {
  if (!v || !frame || v->pixels.empty()) return 0;
  *frame = {v->pixels.data(), v->frame_width, v->frame_height, v->frame_width * 4, v->sequence,
            v->dirty.data(), v->dirty.size()};
  return 1;
}
static void Unlock(HtmlView *) {}

extern "C" __declspec(dllexport) const HtmlBackendVTable *hl2sb_html_get_backend() {
  static const HtmlBackendVTable table = {
    HL2SB_HTML_ABI_VERSION, sizeof(HtmlBackendVTable), Create, Destroy, Pump,
    ViewCreate, ViewDestroy, Resize, Navigate, Back, Forward, Reload, Stop, Eval,
    RegisterJS, Reply, AssetReply, Poll, Mouse, Key, Ime, Lock, Unlock
  };
  return &table;
}
