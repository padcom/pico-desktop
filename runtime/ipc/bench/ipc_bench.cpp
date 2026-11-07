// IPC benchmark for ADR-0004: the host side of the backend link, driving the Pico echo backend
// (packages/communicator-pico/bench/echo.pi) over stdin/stdout pipes and over loopback TCP.
// Usage: pd_ipc_bench <echo executable> [--quick]
// Prints Markdown tables (docs/ipc-benchmark.md records the results).
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "pico_desktop/ipc/cbor.hpp"
#include "pico_desktop/ipc/process.hpp"

namespace ipc = pico_desktop::ipc;
using Clock = std::chrono::steady_clock;

namespace {

struct Link {
  pid_t pid = -1;
  int read_fd = -1;
  int write_fd = -1;
  ipc::FrameBuffer frames{16 * 1024 * 1024};
  std::vector<int> extra_fds;
};

double micros(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }

std::vector<std::byte> request(std::uint64_t id, const std::vector<std::byte>& payload) {
  return ipc::frame(ipc::Writer().map(5).uint(0).uint(1).uint(1).uint(0).uint(2).uint(id).uint(3).text("bench.echo")
                        .uint(4).bytes(payload).take());
}

/// The next message, or nothing on EOF, error or timeout.
std::optional<std::vector<std::byte>> read_message(Link& link, int timeout_ms) {
  std::byte chunk[65536];
  while (true) {
    if (auto m = link.frames.next()) {
      return m;
    }
    pollfd p{link.read_fd, POLLIN, 0};
    if (poll(&p, 1, timeout_ms) <= 0) {
      return std::nullopt;
    }
    const ssize_t n = ::read(link.read_fd, chunk, sizeof chunk);
    if (n <= 0) {
      return std::nullopt;
    }
    link.frames.push(std::span(chunk, static_cast<std::size_t>(n)));
  }
}

bool expect_reply(Link& link, std::uint64_t id) {
  const auto m = read_message(link, 10'000);
  if (!m) {
    return false;
  }
  const auto r = ipc::read_envelope(*m);
  return r && r.header->kind == ipc::Kind::response && r.header->id == id;
}

std::optional<Link> start_stdio(const std::string& echo) {
  std::string error;
  const auto child = ipc::spawn_piped(echo, {}, error);
  if (!child) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return std::nullopt;
  }
  Link link;
  link.pid = child->pid;
  link.read_fd = child->from_child;
  link.write_fd = child->to_child;
  return link;
}

std::optional<Link> start_tcp(const std::string& echo) {
  const int listener = socket(AF_INET, SOCK_STREAM, 0);
  fcntl(listener, F_SETFD, FD_CLOEXEC);  // (macOS has no SOCK_CLOEXEC)
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t len = sizeof addr;
  if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(listener, 1) != 0 ||
      getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    std::perror("listen");
    return std::nullopt;
  }
  std::string error;
  const auto child = ipc::spawn_piped(echo, {"--tcp=" + std::to_string(ntohs(addr.sin_port))}, error);
  if (!child) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return std::nullopt;
  }
  const int conn = accept(listener, nullptr, nullptr);
  fcntl(conn, F_SETFD, FD_CLOEXEC);
  close(listener);
  const int one = 1;
  setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  Link link;
  link.pid = child->pid;
  link.read_fd = conn;
  link.write_fd = conn;
  link.extra_fds = {child->to_child, child->from_child};
  return link;
}

void stop(Link& link) {
  if (link.write_fd == link.read_fd) {
    shutdown(link.write_fd, SHUT_WR);
  } else {
    close(link.write_fd);
  }
  int status = 0;
  waitpid(link.pid, &status, 0);
  close(link.read_fd);
  for (const int fd : link.extra_fds) {
    close(fd);
  }
}

struct Stats {
  double p50 = 0, p95 = 0, p99 = 0;
};

Stats percentiles(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  const auto at = [&](double q) { return v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))]; };
  return {at(0.5), at(0.95), at(0.99)};
}

/// Sequential round trips of `size`-byte payloads: latency percentiles (µs) and MB/s each way.
std::pair<Stats, double> sequential(Link& link, std::size_t size, int count, std::uint64_t& id) {
  const std::vector<std::byte> payload(size, std::byte{0x5a});
  std::vector<double> samples;
  const auto begin = Clock::now();
  for (int i = 0; i < count; ++i) {
    const auto t0 = Clock::now();
    if (!ipc::write_all(link.write_fd, request(++id, payload)) || !expect_reply(link, id)) {
      std::fprintf(stderr, "round trip failed\n");
      std::exit(1);
    }
    samples.push_back(micros(Clock::now() - t0));
  }
  const double seconds = micros(Clock::now() - begin) / 1e6;
  return {percentiles(samples), static_cast<double>(size) * count / seconds / 1e6};
}

/// `window` requests in flight at a time, `total` in all: messages per second.
double pipelined(Link& link, int window, int total, std::uint64_t& id) {
  const std::vector<std::byte> payload(64, std::byte{0x5a});
  const auto begin = Clock::now();
  for (int sent = 0; sent < total; sent += window) {
    std::vector<std::byte> burst;
    const std::uint64_t first = id + 1;
    for (int i = 0; i < window; ++i) {
      const auto r = request(++id, payload);
      burst.insert(burst.end(), r.begin(), r.end());
    }
    if (!ipc::write_all(link.write_fd, burst)) {
      std::exit(1);
    }
    for (std::uint64_t i = first; i <= id; ++i) {
      if (!expect_reply(link, i)) {
        std::fprintf(stderr, "lost reply %llu\n", static_cast<unsigned long long>(i));
        std::exit(1);
      }
    }
  }
  return total / (micros(Clock::now() - begin) / 1e6);
}

struct Usage {
  double cpu_s = 0;
  long max_rss_kb = 0;
};

Usage usage(int who) {
  rusage u{};
  getrusage(who, &u);
  const auto s = [](const timeval& t) { return static_cast<double>(t.tv_sec) + t.tv_usec / 1e6; };
  return {s(u.ru_utime) + s(u.ru_stime), u.ru_maxrss};
}

void run(const char* transport, const std::function<std::optional<Link>()>& start, bool quick) {
  const int n = quick ? 500 : 5000;
  auto link = start();
  if (!link) {
    std::exit(1);
  }
  std::uint64_t id = 0;
  sequential(*link, 64, 200, id);  // warm-up
  const auto [small, small_mbps] = sequential(*link, 64, n, id);
  const auto [k64, k64_mbps] = sequential(*link, 64 * 1024, quick ? 50 : 300, id);
  const auto [m1, m1_mbps] = sequential(*link, 1024 * 1024, quick ? 10 : 50, id);
  const double w1 = pipelined(*link, 1, n, id);
  const double w10 = pipelined(*link, 10, n * 4, id);
  const double w100 = pipelined(*link, 100, n * 10, id);
  stop(*link);
  std::printf("| %s | %.0f / %.0f / %.0f µs | %.0f µs (%.0f MB/s) | %.1f ms (%.0f MB/s) | %.0f | %.0f | %.0f |\n", transport,
              small.p50, small.p95, small.p99, k64.p50, k64_mbps, m1.p50 / 1000, m1_mbps, w1, w10, w100);
}

/// How long until the host notices a killed backend, with a request pending.
double crash_detection_ms(const std::string& echo) {
  auto link = start_stdio(echo);
  std::uint64_t id = 0;
  sequential(*link, 64, 10, id);
  ipc::write_all(link->write_fd, request(++id, std::vector<std::byte>(64)));
  const auto t0 = Clock::now();
  kill(link->pid, SIGKILL);
  std::optional<std::vector<std::byte>> got;
  while ((got = read_message(*link, 5000))) {
  }  // a reply may still be in the pipe; then EOF
  const double ms = micros(Clock::now() - t0) / 1000;
  int status = 0;
  waitpid(link->pid, &status, 0);
  close(link->read_fd);
  close(link->write_fd);
  return ms;
}

/// A stopped (hung) backend: the link stays open and silent, only a timeout notices.
bool hang_is_silent(const std::string& echo) {
  auto link = start_stdio(echo);
  std::uint64_t id = 0;
  sequential(*link, 64, 10, id);
  kill(link->pid, SIGSTOP);
  ipc::write_all(link->write_fd, request(++id, std::vector<std::byte>(64)));
  const bool silent = !read_message(*link, 300);
  kill(link->pid, SIGKILL);
  int status = 0;
  waitpid(link->pid, &status, 0);
  close(link->read_fd);
  close(link->write_fd);
  return silent;
}

/// Closing the backend's stdin: how long until it has exited.
double shutdown_ms(const std::string& echo) {
  auto link = start_stdio(echo);
  std::uint64_t id = 0;
  sequential(*link, 64, 10, id);
  const auto t0 = Clock::now();
  stop(*link);
  return micros(Clock::now() - t0) / 1000;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: pd_ipc_bench <echo executable> [--quick]\n");
    return 2;
  }
  signal(SIGPIPE, SIG_IGN);
  const std::string echo = argv[1];
  const bool quick = argc > 2 && std::string(argv[2]) == "--quick";
  std::puts("| transport | 64 B round trip p50 / p95 / p99 | 64 KiB p50 | 1 MiB p50 | msg/s, 1 in flight | 10 in flight | 100 in flight |");
  std::puts("|---|---|---|---|---|---|---|");
  run("stdin/stdout pipes", [&] { return start_stdio(echo); }, quick);
  run("loopback TCP", [&] { return start_tcp(echo); }, quick);
  const Usage self = usage(RUSAGE_SELF);
  const Usage children = usage(RUSAGE_CHILDREN);
  std::printf("\nCPU: host %.2f s, backends %.2f s; peak RSS: host %ld KiB, largest backend %ld KiB\n", self.cpu_s,
              children.cpu_s, self.max_rss_kb, children.max_rss_kb);
  std::printf("crash (SIGKILL) with a request pending: noticed after %.2f ms (EOF on the pipe)\n",
              crash_detection_ms(echo));
  std::printf("hung backend (SIGSTOP): the link stays open and silent: %s (only a timeout notices)\n",
              hang_is_silent(echo) ? "yes" : "NO");
  std::printf("clean shutdown (close stdin, wait): %.2f ms\n", shutdown_ms(echo));
  return 0;
}
