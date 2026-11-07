// Fuzzing the code that reads untrusted bytes (a page's and the backend's messages): random
// bytes and mutations of valid envelopes through check_profile, read_envelope, with_field,
// without_window, raw_field and decode_value. Nothing may crash, hang or read out of bounds (run
// it under the `sanitize` preset); anything read_envelope accepts must also pass the rewriting
// functions. Deterministic (a fixed seed); `pd_ipc_fuzz_test <iterations> <seed>` explores more.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "pico_desktop/ipc/cbor.hpp"
#include "pico_desktop/ipc/value.hpp"

using namespace pico_desktop::ipc;
using Bytes = std::vector<std::byte>;

static int failures = 0;

#define CHECK(expr)                                                         \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #expr); \
      ++failures;                                                           \
    }                                                                       \
  } while (0)

namespace {

// Valid envelopes to mutate: each kind, with payloads of every CBOR major type.
std::vector<Bytes> seeds() {
  std::vector<Bytes> out;
  out.push_back(Writer().map(3).uint(0).uint(1).uint(1).uint(7).uint(3).text("hello").take());
  out.push_back(Writer()
                    .map(6)
                    .uint(0).uint(1)
                    .uint(1).uint(0)
                    .uint(2).uint(42)
                    .uint(3).text("greeting.hello")
                    .uint(4).map(4)
                    .text("a").array(3).uint(1).negative(1).text("x")
                    .text("b").bytes(from_hex("00ff10"))
                    .text("c").boolean(true)
                    .text("d").raw(from_hex("fb3ff8000000000000"))
                    .uint(6).uint(3)
                    .take());
  out.push_back(Writer().map(4).uint(0).uint(1).uint(1).uint(2).uint(2).uint(9)
                    .uint(5).map(2).uint(0).text("BUSY").uint(1).text("later").take());
  out.push_back(Writer().map(5).uint(0).uint(1).uint(1).uint(3).uint(3).text("tick").uint(8).uint(5)
                    .uint(9).uint(2).take());
  out.push_back(Writer().map(3).uint(0).uint(1).uint(1).uint(6).uint(2).uint(1).take());
  out.push_back(Writer().map(3).uint(0).uint(1).uint(1).uint(5).uint(8).uint(4).take());
  return out;
}

Bytes mutate(Bytes b, std::mt19937_64& rng) {
  std::uniform_int_distribution<int> op(0, 5);
  std::uniform_int_distribution<int> byte(0, 255);
  const int n = 1 + static_cast<int>(rng() % 4);
  for (int i = 0; i < n; ++i) {
    const std::size_t at = b.empty() ? 0 : rng() % b.size();
    switch (op(rng)) {
      case 0:  // flip bits
        if (!b.empty()) b[at] ^= static_cast<std::byte>(1 << (rng() % 8));
        break;
      case 1:  // replace a byte
        if (!b.empty()) b[at] = static_cast<std::byte>(byte(rng));
        break;
      case 2:  // insert
        b.insert(b.begin() + static_cast<long>(at), static_cast<std::byte>(byte(rng)));
        break;
      case 3:  // delete
        if (!b.empty()) b.erase(b.begin() + static_cast<long>(at));
        break;
      case 4:  // truncate
        b.resize(at);
        break;
      default:  // an interesting byte: lengths at the edges, break, tags, simple values
        if (!b.empty()) {
          static constexpr std::uint8_t edges[] = {0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1f, 0x5f, 0x7f, 0x9f,
                                                   0xbf, 0xc0, 0xd8, 0xf7, 0xf8, 0xf9, 0xff, 0x00};
          b[at] = static_cast<std::byte>(edges[rng() % sizeof edges]);
        }
    }
  }
  return b;
}

Bytes random_bytes(std::mt19937_64& rng) {
  Bytes b(rng() % 48);
  for (auto& x : b) {
    x = static_cast<std::byte>(rng() & 0xff);
  }
  return b;
}

void exercise(const Bytes& b) {
  const bool profile_ok = !check_profile(b).has_value();
  const auto read = read_envelope(b);
  if (read) {
    CHECK(profile_ok);  // every accepted envelope follows the profile
    // Routing rewrites what it accepted; the results must be accepted envelopes again.
    const auto tagged = read.header->window ? Bytes(b.begin(), b.end()) : with_window(b, 7);
    const auto untagged = without_window(tagged);
    CHECK(read_envelope(tagged) && read_envelope(untagged));
    const auto renumbered = with_field(b, kKeyId, 99);
    CHECK(!renumbered.empty() && read_envelope(renumbered));
  }
  if (const auto payload = raw_field(b, 4)) {
    (void)decode_value(*payload);
  }
  (void)decode_value(b);
}

}  // namespace

int main(int argc, char** argv) {
  const long iterations = argc > 1 ? std::strtol(argv[1], nullptr, 10) : 200'000;
  const auto seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 0x5eedULL;
  std::mt19937_64 rng(seed);
  const auto corpus = seeds();
  for (const auto& s : corpus) {
    CHECK(read_envelope(s));  // the seeds themselves are valid
  }
  long accepted = 0;
  const auto started = std::chrono::steady_clock::now();
  for (long i = 0; i < iterations; ++i) {
    if (i > 0 && i % 50'000 == 0) {
      const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
      std::printf("ipc.fuzz: %ld inputs in %.1f s\n", i, elapsed.count());
      std::fflush(stdout);
    }
    const Bytes b = (i % 4 == 0) ? random_bytes(rng) : mutate(corpus[rng() % corpus.size()], rng);
    exercise(b);
    accepted += read_envelope(b) ? 1 : 0;
  }
  if (failures != 0) {
    std::fprintf(stderr, "ipc.fuzz: failures with seed %llu\n", static_cast<unsigned long long>(seed));
  } else {
    std::printf("ipc.fuzz: %ld inputs (seed %llu), %ld accepted envelopes, no failures\n", iterations,
                static_cast<unsigned long long>(seed), accepted);
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
