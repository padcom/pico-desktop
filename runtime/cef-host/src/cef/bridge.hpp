// The renderer side of the native bridge: `window.picoDesktop`, injected only into frames the
// policy trusts (the app's origin, main frame), and removed with the context. It carries protocol
// envelopes as ArrayBuffers: `send(buffer)` and `onmessage(buffer, liveContexts)`.
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "include/cef_v8.h"
#include "pico_desktop/host/policy.hpp"

namespace pico_desktop::host::cef {

class RendererBridge {
 public:
  /// Reads the policy from the switches the browser process passed to this renderer.
  RendererBridge();

  void on_context_created(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context);
  void on_context_released(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context);
  /// A `kBridgeReply` for one of this renderer's contexts; true when it was one.
  bool on_reply(CefRefPtr<CefFrame> frame, CefRefPtr<CefProcessMessage> message);

  /// How many contexts currently have the bridge (reported in replies, for tests).
  std::size_t live_contexts() const { return contexts_.size(); }

 private:
  struct Live {
    CefRefPtr<CefV8Context> context;
    CefString frame_id;
  };

  Policy policy_;
  std::int32_t next_id_ = 1;
  std::map<std::int32_t, Live> contexts_;
};

}  // namespace pico_desktop::host::cef
