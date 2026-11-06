// Decoding payloads the host reads itself (ipc::Value).
#include <cstdio>
#include <cstdlib>

#include "pico_desktop/ipc/cbor.hpp"
#include "pico_desktop/ipc/value.hpp"

using namespace pico_desktop::ipc;

static int failures = 0;

#define CHECK(expr)                                                         \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #expr); \
      ++failures;                                                           \
    }                                                                       \
  } while (0)

int main() {
  // {"title": "Hi", "n": 300, "neg": -5, "list": ["a", "b"], "on": true, "off": null, "f": 1.5, "b": h'0102'}
  const auto bytes = Writer()
                         .map(8)
                         .text("title").text("Hi")
                         .text("n").uint(300)
                         .text("neg").negative(4)
                         .text("list").array(2).text("a").text("b")
                         .text("on").boolean(true)
                         .text("off").null()
                         .text("f").raw(from_hex("fb3ff8000000000000"))
                         .text("b").bytes(from_hex("0102"))
                         .take();
  const auto v = decode_value(bytes);
  CHECK(v && v->type == Value::Type::map && v->keys.size() == 8);
  if (v) {
    CHECK(v->get("title") && v->get("title")->as_text() == "Hi");
    CHECK(v->get("n") && v->get("n")->as_uint() == 300u);
    CHECK(v->get("neg") && v->get("neg")->type == Value::Type::negative && v->get("neg")->integer == 4);
    CHECK(v->get("list") && v->get("list")->items.size() == 2 && v->get("list")->items[1].as_text() == "b");
    CHECK(v->get("on") && v->get("on")->as_bool() == true);
    CHECK(!v->get("off"));      // null counts as absent
    CHECK(!v->get("missing"));
    CHECK(v->get("f") && v->get("f")->number == 1.5);
    CHECK(v->get("b") && v->get("b")->text == std::string("\x01\x02"));
    CHECK(!v->get("title")->as_uint());  // the wrong type
  }
  CHECK(decode_value(from_hex("f93e00"))->number == 1.5);  // a half float
  CHECK(!decode_value(from_hex("a1016161")));              // a uint key: not a payload map
  CHECK(!decode_value(from_hex("9f01ff")));                // indefinite: not in the profile
  CHECK(!decode_value(from_hex("0102")));                  // two items
  CHECK(!decode_value(from_hex("63616263ff")));            // trailing garbage / truncated
  if (failures == 0) {
    std::puts("ipc.value: all checks passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
