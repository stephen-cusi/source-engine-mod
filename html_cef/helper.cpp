#include <windows.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_process_message.h"
#include "include/cef_parser.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_scheme.h"
#include "include/cef_v8.h"

class RendererApp final : public CefApp, public CefRenderProcessHandler, public CefV8Handler {
 public:
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> info) override {
    if (!info || !info->HasKey("hl2sb.bindings")) return;
    auto bindings = info->GetList("hl2sb.bindings");
    if (!bindings) return;
    for (size_t i = 0; i + 1 < bindings->GetSize(); i += 2)
      bindings_[browser->GetIdentifier()].emplace(bindings->GetString(i).ToString(),
                                                  bindings->GetString(i + 1).ToString());
  }
  void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) override {
    bindings_.erase(browser->GetIdentifier());
  }
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
    registrar->AddCustomScheme("asset", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                                        CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
  }
  void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override {
    CefRefPtr<CefV8Value> global = context->GetGlobal();
    // Capture the V8 intrinsic before page scripts can replace JSON.parse.
    CefRefPtr<CefV8Value> json = global->GetValue("JSON");
    CefRefPtr<CefV8Value> parse = json ? json->GetValue("parse") : nullptr;
    if (parse && parse->IsFunction()) parsers_.push_back({context, parse});
    if (frame->IsMain()) {
      global->SetValue("__hl2sb_domready", CefV8Value::CreateFunction("__hl2sb_domready", this),
                       V8_PROPERTY_ATTRIBUTE_READONLY);
      CefRefPtr<CefV8Value> value;
      CefRefPtr<CefV8Exception> exception;
      context->Eval("document.addEventListener('DOMContentLoaded',function(){window.__hl2sb_domready()},{once:true})",
                    "", 0, value, exception);
    }
    for (const auto &binding : bindings_[browser->GetIdentifier()]) Install(context, binding.first, binding.second);
  }
  void OnContextReleased(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                         CefRefPtr<CefV8Context> context) override {
    for (auto it = callbacks_.begin(); it != callbacks_.end();) {
      if (it->second.context->IsSame(context)) it = callbacks_.erase(it);
      else ++it;
    }
    for (auto it = parsers_.begin(); it != parsers_.end();) {
      if (it->context->IsSame(context)) it = parsers_.erase(it);
      else ++it;
    }
  }
  bool Execute(const CefString &name, CefRefPtr<CefV8Value>, const CefV8ValueList &args,
               CefRefPtr<CefV8Value> &retval, CefString &exception) override {
    if (name == "__hl2sb_domready") {
      auto context = CefV8Context::GetCurrentContext();
      if (context && context->GetFrame())
        context->GetFrame()->SendProcessMessage(PID_BROWSER, CefProcessMessage::Create("hl2sb.ready"));
      retval = CefV8Value::CreateUndefined();
      return true;
    }
    if (args.empty() || !args[0]->IsString()) { exception = "Invalid bridge arguments"; return true; }
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    if (!context || !context->GetFrame()) return true;
    std::string id = std::to_string(++next_id_);
    if (args.size() > 1 && args[1]->IsFunction()) {
      if (callbacks_.size() >= 512) { exception = "Too many pending bridge callbacks"; return true; }
      callbacks_[id] = {context, args[1]};
    }
    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("hl2sb.call");
    message->GetArgumentList()->SetString(0, args[0]->GetStringValue());
    message->GetArgumentList()->SetString(1, id);
    context->GetFrame()->SendProcessMessage(PID_BROWSER, message);
    retval = CefV8Value::CreateUndefined();
    return true;
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                CefProcessId source, CefRefPtr<CefProcessMessage> message) override {
    if (source == PID_BROWSER && message->GetName() == "hl2sb.register") {
      auto args = message->GetArgumentList();
      std::string object = args->GetString(0).ToString(), function = args->GetString(1).ToString();
      bindings_[browser->GetIdentifier()].emplace(object, function);
      CefRefPtr<CefV8Context> context = frame->GetV8Context();
      if (context && context->IsValid() && context->Enter()) {
        Install(context, object, function);
        context->Exit();
      }
      return true;
    }
    if (source != PID_BROWSER || message->GetName() != "hl2sb.reply") return false;
    auto it = callbacks_.find(message->GetArgumentList()->GetString(0).ToString());
    if (it == callbacks_.end()) return true;
    Callback callback = it->second;
    callbacks_.erase(it);
    if (!callback.context->IsValid() || !callback.context->Enter()) return true;
    CefRefPtr<CefV8Value> parse;
    for (const auto &entry : parsers_) {
      if (entry.context->IsSame(callback.context)) { parse = entry.parse; break; }
    }
    CefV8ValueList input{CefV8Value::CreateString(message->GetArgumentList()->GetString(1))};
    CefRefPtr<CefV8Value> array = parse ? parse->ExecuteFunction(nullptr, input) : nullptr;
    CefV8ValueList results;
    if (array && array->IsArray()) {
      for (int i = 0; i < array->GetArrayLength(); ++i) results.push_back(array->GetValue(i));
      callback.function->ExecuteFunction(nullptr, results);
    }
    callback.context->Exit();
    return true;
  }
 private:
  class BindingHandler final : public CefV8Handler {
   public:
    BindingHandler(RendererApp *owner, const std::string &object, const std::string &function)
        : owner_(owner), object_(object), function_(function) {}
    bool Execute(const CefString &, CefRefPtr<CefV8Value>, const CefV8ValueList &args,
                 CefRefPtr<CefV8Value> &retval, CefString &exception) override {
      CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
      if (!context) { exception = "Missing JavaScript context"; return true; }
      const bool has_callback = !args.empty() && args.back()->IsFunction();
      const size_t count = args.size() - (has_callback ? 1 : 0);
      CefRefPtr<CefV8Value> arguments = CefV8Value::CreateArray(static_cast<int>(count));
      for (size_t i = 0; i < count; ++i) arguments->SetValue(static_cast<int>(i), args[i]);
      CefRefPtr<CefV8Value> packet = CefV8Value::CreateArray(3);
      packet->SetValue(0, CefV8Value::CreateString(object_));
      packet->SetValue(1, CefV8Value::CreateString(function_));
      packet->SetValue(2, arguments);
      CefRefPtr<CefV8Value> json = context->GetGlobal()->GetValue("JSON");
      CefRefPtr<CefV8Value> stringify = json ? json->GetValue("stringify") : nullptr;
      CefV8ValueList input{packet};
      CefRefPtr<CefV8Value> encoded = stringify && stringify->IsFunction()
          ? stringify->ExecuteFunction(json, input) : nullptr;
      if (!encoded || !encoded->IsString()) { exception = "Cannot serialize bridge arguments"; return true; }
      CefV8ValueList dispatch{encoded};
      if (has_callback) dispatch.push_back(args.back());
      return owner_->Execute("__hl2sb_native", nullptr, dispatch, retval, exception);
    }
   private:
    RendererApp *owner_;
    std::string object_, function_;
    IMPLEMENT_REFCOUNTING(BindingHandler);
  };
  void Install(CefRefPtr<CefV8Context> context, const std::string &object,
               const std::string &function) {
    CefRefPtr<CefV8Value> global = context->GetGlobal();
    CefRefPtr<CefV8Value> target = global->GetValue(object);
    if (!target || target->IsUndefined() || target->IsNull()) {
      target = CefV8Value::CreateObject(nullptr, nullptr);
      if (!global->SetValue(object, target, V8_PROPERTY_ATTRIBUTE_NONE)) return;
    }
    if (!target->IsObject()) return;
    target->SetValue(function, CefV8Value::CreateFunction(function,
        new BindingHandler(this, object, function)), V8_PROPERTY_ATTRIBUTE_NONE);
  }
  struct Callback { CefRefPtr<CefV8Context> context; CefRefPtr<CefV8Value> function; };
  struct ContextParser { CefRefPtr<CefV8Context> context; CefRefPtr<CefV8Value> parse; };
  std::vector<ContextParser> parsers_;
  std::map<int, std::set<std::pair<std::string, std::string>>> bindings_;
  uint64_t next_id_ = 0;
  std::map<std::string, Callback> callbacks_;
  IMPLEMENT_REFCOUNTING(RendererApp);
};

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  CefMainArgs args(instance);
  CefRefPtr<RendererApp> app(new RendererApp);
  return CefExecuteProcess(args, app, nullptr);
}
