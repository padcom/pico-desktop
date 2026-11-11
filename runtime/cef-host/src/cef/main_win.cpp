// Windows: the host is a DLL that CEF's console bootstrap (bootstrapc.exe, renamed
// pico-desktop-host.exe and run from the same directory) loads inside the sandbox it sets up,
// for the browser process and for every Chromium subprocess alike (cef_sandbox_win.h).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app.hpp"
#include "host.hpp"
#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"

int RunConsoleMain(int argc, char* argv[], void* sandbox_info, cef_version_info_t* /*version_info*/) {
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromString(::GetCommandLineW());
  if (command_line->HasSwitch("type")) {
    // A Chromium subprocess (renderer, GPU, utility).
    CefMainArgs main_args(::GetModuleHandleW(nullptr));
    CefRefPtr<pico_desktop::host::cef::HostApp> app = new pico_desktop::host::cef::HostApp();
    return CefExecuteProcess(main_args, app, sandbox_info);
  }
  pico_desktop::host::cef::set_sandbox_info(sandbox_info);
  return pico_desktop::host::cef::run_browser(argc, argv);
}
