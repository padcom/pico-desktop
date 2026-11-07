// A backend that exits at once (code 3) and leaves a child holding its stdin and stdout for 30 s,
// like a backend whose helper inherited its pipes: the host must still see the exit (it watches
// the process, not only EOF, #17) and must not wait on the pipes when it ends it (#16). A program,
// not a shell script (hang.sh's stray), so Windows runs it too.
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--hold") == 0) {
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
  }
#if defined(_WIN32)
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring line = L"\"" + std::wstring(self) + L"\" --hold";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);    // the host's pipes: inheritable
  startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  startup.hStdError = nullptr;  // not the test's log, which it removes
  PROCESS_INFORMATION process{};
  if (CreateProcessW(self, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
#else
  if (fork() == 0) {
    close(2);  // not the test's log
    std::this_thread::sleep_for(std::chrono::seconds(30));
    _exit(0);
  }
#endif
  return 3;
}
