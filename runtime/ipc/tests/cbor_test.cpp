// The host's CBOR checks against the shared vectors (packages/protocol/vectors): every valid
// item passes the profile check, every invalid one fails it, and envelopes are read (or refused
// with the same code) as the TypeScript and Pico sides do.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "pico_desktop/ipc/cbor.hpp"

namespace ipc = pico_desktop::ipc;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

static std::string read(const std::string& path) {
  std::ifstream in(path);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

/// The text of a JSON section (`"name": [ … ]`) of a vectors file.
static std::string section(const std::string& json, const std::string& name) {
  const auto at = json.find("\"" + name + "\": [");
  if (at == std::string::npos) {
    return {};
  }
  int depth = 0;
  for (std::size_t i = json.find('[', at); i < json.size(); ++i) {
    depth += json[i] == '[' ? 1 : json[i] == ']' ? -1 : 0;
    if (depth == 0) {
      return json.substr(at, i - at);
    }
  }
  return {};
}

/// Every value of `"field": "…"` in `text`, in order (the vectors' strings never contain quotes
/// in these fields).
static std::vector<std::string> strings(const std::string& text, const std::string& field) {
  std::vector<std::string> out;
  const std::string key = "\"" + field + "\": \"";
  for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + 1)) {
    const auto begin = at + key.size();
    out.push_back(text.substr(begin, text.find('"', begin) - begin));
  }
  return out;
}

static void profile(const std::string& dir) {
  const std::string json = read(dir + "/cbor.json");
  check(!json.empty(), "cbor.json is readable");
  std::size_t valid = 0;
  for (const char* name : {"roundtrip", "decode"}) {
    const std::string part = section(json, name);
    const auto names = strings(part, "name");
    const auto hexes = strings(part, "hex");
    for (std::size_t i = 0; i < hexes.size(); ++i, ++valid) {
      const auto problem = ipc::check_profile(ipc::from_hex(hexes[i]));
      check(!problem, std::string(name) + " " + names[i] + ": " + problem.value_or(""));
    }
  }
  const std::string invalid = section(json, "invalid");
  const auto names = strings(invalid, "name");
  const auto hexes = strings(invalid, "hex");
  for (std::size_t i = 0; i < hexes.size(); ++i) {
    check(ipc::check_profile(ipc::from_hex(hexes[i])).has_value(), "invalid " + names[i] + " is rejected");
  }
  check(valid > 50 && hexes.size() > 20, "the vectors were found");
}

static void envelopes(const std::string& dir) {
  const std::string json = read(dir + "/envelope.json");
  const std::string valid = section(json, "valid");
  const auto names = strings(valid, "name");
  const auto hexes = strings(valid, "hex");
  for (std::size_t i = 0; i < hexes.size(); ++i) {
    const auto result = ipc::read_envelope(ipc::from_hex(hexes[i]));
    check(static_cast<bool>(result), "envelope " + names[i] + ": " + result.refusal.message);
  }
  const std::string invalid = section(json, "invalid");
  const auto bad_names = strings(invalid, "name");
  const auto bad_hexes = strings(invalid, "hex");
  const auto codes = strings(invalid, "code");
  for (std::size_t i = 0; i < bad_hexes.size(); ++i) {
    const auto result = ipc::read_envelope(ipc::from_hex(bad_hexes[i]));
    check(!result && result.refusal.code == codes[i],
          "envelope " + bad_names[i] + " is refused with " + codes[i] + " (got " + result.refusal.code + ")");
  }
  check(hexes.size() > 10 && bad_hexes.size() > 15, "the envelope vectors were found");
}

static void header_and_window() {
  // {0: 1, 1: 0 (request), 2: 7, 3: "serial.list", 4: {}}
  const auto request = ipc::Writer().map(5).uint(0).uint(1).uint(1).uint(0).uint(2).uint(7).uint(3).text("serial.list")
                           .uint(4).map(0).take();
  const auto read = ipc::read_envelope(request);
  check(read && read.header->kind == ipc::Kind::request && read.header->id == 7u &&
            read.header->name == std::string("serial.list") && !read.header->window,
        "a request's routing fields");

  const auto routed = ipc::with_window(request, 3);
  const auto routed_read = ipc::read_envelope(routed);
  check(routed_read && routed_read.header->window == 3u, "with_window adds w");
  check(ipc::with_window(routed, 4).empty(), "with_window refuses an envelope that already has w");
  check(ipc::without_window(routed) == request, "without_window gives back the original bytes");
  check(ipc::with_window(ipc::from_hex("80"), 1).empty(), "with_window refuses a non-envelope");

  // Large values and lengths use the right head forms.
  const auto big = ipc::Writer().uint(1000000000000ull).take();
  check(ipc::to_hex(big) == "1b000000e8d4a51000", "uint head forms");
  const std::string long_text(300, 'x');
  check(!ipc::check_profile(ipc::Writer().text(long_text).take()), "a 300-byte text");
  check(ipc::check_profile(ipc::from_hex("62c0af")).has_value(), "an overlong UTF-8 sequence is rejected");
  check(ipc::check_profile(ipc::from_hex("63eda080")).has_value(), "an encoded surrogate is rejected");
  check(ipc::check_profile(ipc::from_hex("64f4908080")).has_value(), "a code point above U+10FFFF is rejected");
  // {1: 0, 1: 2}, the second key 1 written with a one-byte argument.
  check(ipc::check_profile(ipc::from_hex("a219000100180102")).has_value(),
        "duplicate keys in different head forms are rejected");
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "packages/protocol/vectors";
  profile(dir);
  envelopes(dir);
  header_and_window();
  if (failures == 0) {
    std::puts("ipc.cbor: all checks passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
