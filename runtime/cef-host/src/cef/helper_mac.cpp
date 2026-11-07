// macOS: the helper apps' executable (renderer, GPU, utility, … processes). They run the same
// HostApp as Linux's subprocesses do: the renderer side of the bridge lives here.
#include "app.hpp"
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

#if defined(CEF_USE_SANDBOX)
#include "include/cef_sandbox_mac.h"
#endif

int main(int argc, char* argv[]) {
#if defined(CEF_USE_SANDBOX)
  // The helper's sandbox first, then the framework (loaded at run time, as the sandbox requires).
  CefScopedSandboxContext sandbox_context;
  if (!sandbox_context.Initialize(argc, argv)) {
    return 1;
  }
#endif
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInHelper()) {
    return 1;
  }
  CefMainArgs main_args(argc, argv);
  CefRefPtr<pico_desktop::host::cef::HostApp> app = new pico_desktop::host::cef::HostApp(false);
  return CefExecuteProcess(main_args, app, nullptr);
}
