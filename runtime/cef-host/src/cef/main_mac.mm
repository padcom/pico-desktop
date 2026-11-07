// macOS: the app's main executable (the browser process). Chromium's subprocesses run in the
// helper apps (helper_mac.cpp) inside the bundle's Frameworks directory. CEF needs an
// NSApplication that implements CefAppProtocol, created before CefInitialize, and the framework
// loaded at run time.
#import <Cocoa/Cocoa.h>

// Built like the helpers, with the sandbox (PROJECT_DESCRIPTION.md §6, helper_mac.cpp).
#if !defined(CEF_USE_SANDBOX)
#error "CEF_USE_SANDBOX isn't defined: the helpers would run without the sandbox"
#endif

#include "host.hpp"
#include "include/cef_application_mac.h"
#include "include/wrapper/cef_library_loader.h"

@interface PicoDesktopApplication : NSApplication <CefAppProtocol> {
 @private
  BOOL handlingSendEvent_;
}
@end

@implementation PicoDesktopApplication
- (BOOL)isHandlingSendEvent {
  return handlingSendEvent_;
}
- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  handlingSendEvent_ = handlingSendEvent;
}
- (void)sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sendingEventScoper;
  [super sendEvent:event];
}
// Cmd+Q (or the Dock's Quit): close the window the orderly way, as its close button does; the
// message loop ends once it's gone.
- (void)terminate:(id)sender {
  pico_desktop::host::cef::close_app_window();
}
@end

int main(int argc, char* argv[]) {
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) {
    return 1;
  }
  @autoreleasepool {
    [PicoDesktopApplication sharedApplication];
    return pico_desktop::host::cef::run_browser(argc, argv);
  }
}
