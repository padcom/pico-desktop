// Inspecting Chromium's child processes on Windows from the browser process: their command line
// (`--type=`), their token's integrity level, and whether they're in a job object (#11).
#include "pico_desktop/host/sandbox.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <map>
#include <memory>
#endif

namespace pico_desktop::host {

#if defined(_WIN32)

namespace {

/// Closes a HANDLE when it goes.
struct HandleCloser {
  void operator()(HANDLE handle) const {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  }
};
using Handle = std::unique_ptr<void, HandleCloser>;

/// Every process's parent, from a Toolhelp snapshot.
std::map<DWORD, DWORD> parents() {
  std::map<DWORD, DWORD> parent;
  const Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (snapshot.get() == INVALID_HANDLE_VALUE) {
    return parent;
  }
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL more = Process32FirstW(snapshot.get(), &entry); more; more = Process32NextW(snapshot.get(), &entry)) {
    parent[entry.th32ProcessID] = entry.th32ParentProcessID;
  }
  return parent;
}

bool descends_from(DWORD pid, DWORD root, const std::map<DWORD, DWORD>& parent) {
  DWORD p = pid;
  for (int depth = 0; depth < 64; ++depth) {
    const auto it = parent.find(p);
    if (it == parent.end() || it->second == 0 || it->second == p) {
      return false;
    }
    if (it->second == root) {
      return true;
    }
    p = it->second;
  }
  return false;
}

/// The process's command line (NtQueryInformationProcess, ProcessCommandLineInformation: Windows
/// 8.1 and later), ASCII-narrowed: only `--type=` is wanted from it.
std::string command_line(HANDLE process) {
  using Query = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  static const auto query = reinterpret_cast<Query>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
  constexpr ULONG kProcessCommandLineInformation = 60;
  if (query == nullptr) {
    return {};
  }
  ULONG size = 0;
  query(process, kProcessCommandLineInformation, nullptr, 0, &size);
  if (size == 0) {
    return {};
  }
  std::unique_ptr<std::byte[]> buffer(new std::byte[size]);
  if (query(process, kProcessCommandLineInformation, buffer.get(), size, &size) < 0) {
    return {};
  }
  // A UNICODE_STRING followed by its characters.
  struct UnicodeString {
    USHORT length;
    USHORT maximum_length;
    PWSTR buffer;
  };
  const auto* text = reinterpret_cast<const UnicodeString*>(buffer.get());
  std::string narrow;
  for (USHORT i = 0; i < text->length / sizeof(wchar_t); ++i) {
    const wchar_t c = text->buffer[i];
    narrow += c < 0x80 ? static_cast<char>(c) : '?';
  }
  return narrow;
}

/// The token's mandatory integrity RID, or -1.
long integrity_level(HANDLE process) {
  HANDLE raw = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) {
    return -1;
  }
  const Handle token(raw);
  DWORD size = 0;
  GetTokenInformation(token.get(), TokenIntegrityLevel, nullptr, 0, &size);
  if (size == 0) {
    return -1;
  }
  std::unique_ptr<std::byte[]> buffer(new std::byte[size]);
  if (!GetTokenInformation(token.get(), TokenIntegrityLevel, buffer.get(), size, &size)) {
    return -1;
  }
  const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.get());
  const DWORD count = *GetSidSubAuthorityCount(label->Label.Sid);
  return count == 0 ? -1 : static_cast<long>(*GetSidSubAuthority(label->Label.Sid, count - 1));
}

}  // namespace

std::vector<WindowsProcessSandbox> inspect_descendants_windows(unsigned long root_pid) {
  std::vector<WindowsProcessSandbox> result;
  const auto parent = parents();
  for (const auto& [pid, ppid] : parent) {
    if (!descends_from(pid, static_cast<DWORD>(root_pid), parent)) {
      continue;
    }
    const Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (process == nullptr) {
      result.push_back({pid, "unknown", -1, false});  // reported, and a renderer can't hide this way
      continue;
    }
    const std::string type = parse_cmdline_type(command_line(process.get()));
    if (type.empty()) {
      continue;  // not a Chromium child
    }
    BOOL in_job = FALSE;
    IsProcessInJob(process.get(), nullptr, &in_job);
    result.push_back({pid, type, integrity_level(process.get()), in_job != FALSE});
  }
  return result;
}

#else

std::vector<WindowsProcessSandbox> inspect_descendants_windows(unsigned long) { return {}; }

#endif

}  // namespace pico_desktop::host
