#include "bridge.hpp"

#include "host.hpp"
#include "include/cef_command_line.h"
#include "include/cef_process_message.h"

namespace pico_desktop::host::cef {

namespace {

constexpr std::size_t kMaxMessageBytes = 1 << 20;  // bounded until F2 specifies frame limits

/// `picoDesktop.send(text)`: hands the text to the browser process, tagged with this context.
class SendHandler : public CefV8Handler {
 public:
  explicit SendHandler(std::int32_t context_id) : context_id_(context_id) {}

  bool Execute(const CefString& name, CefRefPtr<CefV8Value> object, const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& retval, CefString& exception) override {
    if (arguments.size() != 1 || !arguments[0]->IsString()) {
      exception = "picoDesktop.send(text) takes one string";
      return true;
    }
    const CefString text = arguments[0]->GetStringValue();
    if (text.length() > kMaxMessageBytes) {
      exception = "picoDesktop.send: message too large";
      return true;
    }
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
    if (!frame) {
      exception = "picoDesktop.send: no frame";
      return true;
    }
    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(kBridgeSend);
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    args->SetInt(0, context_id_);
    args->SetString(1, text);
    frame->SendProcessMessage(PID_BROWSER, message);
    retval = CefV8Value::CreateUndefined();
    return true;
  }

 private:
  std::int32_t context_id_;
  IMPLEMENT_REFCOUNTING(SendHandler);
};

}  // namespace

RendererBridge::RendererBridge() {
  CefRefPtr<CefCommandLine> line = CefCommandLine::GetGlobalCommandLine();
  policy_.app_id = line->GetSwitchValue(kSwitchAppId).ToString();
  policy_.scheme = line->GetSwitchValue(kSwitchOrigin).ToString() == "https" ? OriginScheme::https : OriginScheme::app;
  if (line->HasSwitch(kSwitchDevOrigin)) {
    policy_.dev_origin = line->GetSwitchValue(kSwitchDevOrigin).ToString();
  }
  policy_.bridge_in_subframes = line->GetSwitchValue(kSwitchBridgeSubframes).ToString() == "1";
}

void RendererBridge::on_context_created(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
  // The renderer gets no policy when the browser didn't pass one: then nothing is trusted.
  if (policy_.app_id.empty() || !bridge_allowed(policy_, frame->GetURL().ToString(), frame->IsMain())) {
    return;
  }
  const std::int32_t id = next_id_++;
  contexts_[id] = {context, frame->GetIdentifier()};

  constexpr auto fixed = static_cast<cef_v8_propertyattribute_t>(V8_PROPERTY_ATTRIBUTE_READONLY |
                                                                 V8_PROPERTY_ATTRIBUTE_DONTDELETE);
  CefRefPtr<CefV8Value> bridge = CefV8Value::CreateObject(nullptr, nullptr);
  bridge->SetValue("hostVersion", CefV8Value::CreateString(PD_VERSION), fixed);
  bridge->SetValue("send", CefV8Value::CreateFunction("send", new SendHandler(id)), fixed);
  bridge->SetValue("onmessage", CefV8Value::CreateNull(), V8_PROPERTY_ATTRIBUTE_DONTDELETE);
  context->GetGlobal()->SetValue("picoDesktop", bridge, fixed);
}

void RendererBridge::on_context_released(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
  for (auto it = contexts_.begin(); it != contexts_.end(); ++it) {
    if (it->second.context->IsSame(context)) {
      contexts_.erase(it);
      return;
    }
  }
}

bool RendererBridge::on_reply(CefRefPtr<CefFrame> frame, CefRefPtr<CefProcessMessage> message) {
  if (message->GetName() != kBridgeReply) {
    return false;
  }
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  const auto it = contexts_.find(args->GetInt(0));
  // A reply for a context that's gone (navigation, reload) or that belongs to another frame is
  // dropped: it must never reach the page that replaced the sender.
  if (it == contexts_.end() || it->second.frame_id != frame->GetIdentifier()) {
    return true;
  }
  CefRefPtr<CefV8Context> context = it->second.context;
  if (!context->Enter()) {
    return true;
  }
  CefRefPtr<CefV8Value> bridge = context->GetGlobal()->GetValue("picoDesktop");
  CefRefPtr<CefV8Value> handler = bridge ? bridge->GetValue("onmessage") : nullptr;
  if (handler && handler->IsFunction()) {
    CefV8ValueList call_args{CefV8Value::CreateString(args->GetString(1)),
                             CefV8Value::CreateInt(static_cast<std::int32_t>(contexts_.size()))};
    handler->ExecuteFunction(bridge, call_args);
  }
  context->Exit();
  return true;
}

}  // namespace pico_desktop::host::cef
