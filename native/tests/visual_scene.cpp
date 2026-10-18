#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string>

#include "mundo_x/visual_scene.hpp"

namespace {
using mundo_x::VisualScene;
std::size_t checks = 0;

void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

template <class Exception, class Action>
void rejects(Action action, const char* message) {
  ++checks;
  try { action(); }
  catch (const Exception&) { return; }
  throw std::runtime_error(message);
}

std::array<std::uint32_t, 4> bits(const std::array<float, 4>& values) {
  std::array<std::uint32_t, 4> result{};
  static_assert(sizeof(result) == sizeof(values), "unexpected binary32 array layout");
  std::memcpy(result.data(), values.data(), sizeof(result));
  return result;
}

void golden_transform_cases() {
  struct Golden {
    std::uint32_t count;
    std::int32_t seed;
    std::uint32_t id, state;
    std::array<std::uint32_t, 4> expected;
  };
  // Captured from the independent Android ObjectSceneModel on Java 21 using
  // Float.floatToRawIntBits; compare exact bits, not approximate decimal values.
  const Golden vectors[] = {
    {1, 2026, 0, 0, {0x00000000u, 0x00000000u, 0x3fef999au, 0x3f35b5b6u}},
    {1, 2026, 0, 1, {0x00000000u, 0x00000000u, 0x400e3333u, 0x3f35b5b6u}},
    {1, 2026, 0, 6, {0x00000000u, 0x00000000u, 0x407e3333u, 0x3f35b5b6u}},
    {1, 2026, 0, 7, {0x00000000u, 0x00000000u, 0x00000000u, 0x3f35b5b6u}},
    {5, INT32_MIN, 4, 0, {0x00000000u, 0x00000000u, 0x3f91999au, 0x3f58d8d9u}},
    {5, INT32_MIN, 4, 1, {0x00000000u, 0x00000000u, 0x3fbe6667u, 0x3f58d8d9u}},
    {5, INT32_MIN, 4, 6, {0x00000000u, 0x00000000u, 0x404f3333u, 0x3f58d8d9u}},
    {5, INT32_MIN, 4, 7, {0x00000000u, 0x00000000u, 0x00000000u, 0x3f58d8d9u}},
    {5, INT32_MAX, 0, 0, {0xc00ccccdu, 0xc00ccccdu, 0x3f593333u, 0x3ea4a4a5u}},
    {5, INT32_MAX, 0, 1, {0xc00ccccdu, 0xc00ccccdu, 0x3f996666u, 0x3ea4a4a5u}},
    {5, INT32_MAX, 0, 6, {0xc00ccccdu, 0xc00ccccdu, 0x403cb333u, 0x3ea4a4a5u}},
    {5, INT32_MAX, 0, 7, {0xc00ccccdu, 0xc00ccccdu, 0x00000000u, 0x3ea4a4a5u}},
    {65536, 2026, 65535, 0, {0x438c4000u, 0x438c4000u, 0x3fc3999au, 0x3ea8a8a9u}},
    {65536, 2026, 65535, 1, {0x438c4000u, 0x438c4000u, 0x3ff06667u, 0x3ea8a8a9u}},
    {65536, 2026, 65535, 6, {0x438c4000u, 0x438c4000u, 0x40683333u, 0x3ea8a8a9u}},
    {65536, 2026, 65535, 7, {0x438c4000u, 0x438c4000u, 0x00000000u, 0x3ea8a8a9u}},
    {65537, INT32_MIN, 65535, 0, {0xc38ccccdu, 0x438bb333u, 0x40114ccdu, 0x3f3dbdbeu}},
    {65537, INT32_MIN, 65535, 1, {0xc38ccccdu, 0x438bb333u, 0x4027b333u, 0x3f3dbdbeu}},
    {65537, INT32_MIN, 65535, 6, {0xc38ccccdu, 0x438bb333u, 0x408bd99au, 0x3f3dbdbeu}},
    {65537, INT32_MIN, 65535, 7, {0xc38ccccdu, 0x438bb333u, 0x00000000u, 0x3f3dbdbeu}},
    {65537, INT32_MAX, 65536, 0, {0xc38bb333u, 0x438bb333u, 0x4016cccdu, 0x3eb2b2b3u}},
    {65537, INT32_MAX, 65536, 1, {0xc38bb333u, 0x438bb333u, 0x402d3333u, 0x3eb2b2b3u}},
    {65537, INT32_MAX, 65536, 6, {0xc38bb333u, 0x438bb333u, 0x408e999au, 0x3eb2b2b3u}},
    {65537, INT32_MAX, 65536, 7, {0xc38bb333u, 0x438bb333u, 0x00000000u, 0x3eb2b2b3u}},
    {1048576, INT32_MIN, 524800, 0, {0x3f8ccccdu, 0x3f8ccccdu, 0x3fdd999au, 0x3eeeeeefu}},
    {1048576, INT32_MIN, 524800, 1, {0x3f8ccccdu, 0x3f8ccccdu, 0x40053333u, 0x3eeeeeefu}},
    {1048576, INT32_MIN, 524800, 6, {0x3f8ccccdu, 0x3f8ccccdu, 0x40753333u, 0x3eeeeeefu}},
    {1048576, INT32_MIN, 524800, 7, {0x3f8ccccdu, 0x3f8ccccdu, 0x00000000u, 0x3eeeeeefu}},
    {1048576, INT32_MAX, 1048575, 0, {0x448ca99au, 0x448ca99au, 0x3f353333u, 0x3e9c9c9du}},
    {1048576, INT32_MAX, 1048575, 1, {0x448ca99au, 0x448ca99au, 0x3f876666u, 0x3e9c9c9du}},
    {1048576, INT32_MAX, 1048575, 6, {0x448ca99au, 0x448ca99au, 0x4033b333u, 0x3e9c9c9du}},
    {1048576, INT32_MAX, 1048575, 7, {0x448ca99au, 0x448ca99au, 0x00000000u, 0x3e9c9c9du}}
  };
  for (const auto& vector : vectors) {
    VisualScene scene(vector.count, vector.seed);
    scene.edit(vector.id, vector.state);
    require(bits(scene.transform(vector.id)) == vector.expected,
            "transform differs from Java binary32 fixture");
  }
}

void finite_grid_cases() {
  rejects<std::invalid_argument>([] { VisualScene(0, 0); }, "zero objects accepted");
  rejects<std::invalid_argument>([] { VisualScene(1048577, 0); }, "count above limit accepted");
  rejects<std::invalid_argument>([] { VisualScene(UINT32_MAX, 0); }, "wrapped negative count accepted");
  for (std::uint32_t side = 1; side <= 1024; ++side) {
    VisualScene square(side * side, -137);
    require(square.side() == side, "square count has incorrect grid side");
    require(square.logical_rule_bytes() == 32 && square.edit_count() == 0,
            "base rule payload depends on population");
    if (side < 1024)
      require(VisualScene(side * side + 1, 0).side() == side + 1,
              "partial final grid row was lost");
  }
  VisualScene scene(65537, INT32_MIN);
  require(scene.count() == 65537 && scene.seed() == INT32_MIN && scene.version() == 0,
          "constructor metadata changed");
  for (const auto id : {65537U, UINT32_MAX}) {
    rejects<std::invalid_argument>([&] { scene.transform(id); }, "out-of-scene transform accepted");
    rejects<std::invalid_argument>([&] { scene.edit_state(id); }, "out-of-scene edit lookup accepted");
    rejects<std::invalid_argument>([&] { scene.edit(id, 1); }, "out-of-scene edit accepted");
  }
  rejects<std::invalid_argument>([&] { scene.edit(0, 8); }, "state 8 accepted");
  rejects<std::invalid_argument>([&] { scene.edit(0, UINT32_MAX); }, "wrapped negative state accepted");
  require(scene.version() == 0 && scene.edit_count() == 0, "failed edits changed scene");
}

void persistent_edit_cases() {
  VisualScene scene(71, -137);
  std::array<std::array<std::uint32_t, 4>, 71> original{};
  for (std::uint32_t id = 0; id < 71; ++id) original[id] = bits(scene.transform(id));
  require(scene.edit(5, 3), "raising did not report change");
  require(!scene.edit(5, 3) && scene.version() == 1, "same-state edit advanced revision");
  require(scene.edit_count() == 1 && scene.logical_rule_bytes() == 40,
          "one exception payload incorrect");
  for (std::uint32_t id = 0; id < 71; ++id) {
    const auto actual = bits(scene.transform(id));
    if (id == 5) {
      require(actual[2] != original[id][2], "raising left height unchanged");
      require(actual[0] == original[id][0] && actual[1] == original[id][1] &&
                  actual[3] == original[id][3], "raising changed identity or tone");
    } else require(actual == original[id], "edit changed another object");
  }
  require(scene.edit(5, 7) && scene.transform(5)[2] == 0.0f, "hide failed");
  require(scene.edit(5, 0), "restoring did not report change");
  require(bits(scene.transform(5)) == original[5], "restore lost exact original transform");
  require(scene.version() == 3 && scene.edit_count() == 0 && scene.logical_rule_bytes() == 32,
          "restoring retained an exception");
  require(!scene.edit(5, 0) && scene.version() == 3, "unmodified restore changed revision");
  scene.edit(20, 6); scene.edit(9, 1); scene.edit(2, 7);
  const std::string expected = "MUNDOX_OBJECTS_V1\ncount=71\nseed=-137\nversion=6\nedits=3\n"
                               "2:7\n9:1\n20:6\n";
  require(scene.export_text() == expected, "canonical export is not sorted/persistent");
  auto imported = VisualScene::import_text(expected);
  require(imported.export_text() == expected, "import/export altered canonical scene");
  for (std::uint32_t id = 0; id < 71; ++id)
    require(bits(imported.transform(id)) == bits(scene.transform(id)), "roundtrip lost transform");
  imported.clear_edits();
  require(imported.version() == 7 && imported.edit_count() == 0, "clear did not advance once");
  imported.clear_edits();
  require(imported.version() == 7, "empty clear advanced revision");
  require(scene.export_text() == expected, "independent imported scene aliases original");
  for (std::uint32_t id = 0; id < 71; ++id)
    require(bits(imported.transform(id)) == original[id], "clear failed to restore rule");
}

void edit_capacity_cases() {
  VisualScene scene(1048576, INT32_MAX);
  for (std::uint32_t id = 0; id < 4096; ++id)
    require(scene.edit(id, 1 + id % 7), "legal sparse insertion failed");
  require(scene.edit_count() == 4096 && scene.version() == 4096 &&
              scene.logical_rule_bytes() == 32800, "full sparse payload/revision incorrect");
  const auto full = scene.export_text();
  rejects<std::logic_error>([&] { scene.edit(1048575, 7); }, "sparse limit was ignored");
  require(scene.export_text() == full, "capacity failure partly changed scene");
  require(!scene.edit(0, 1), "same state at capacity did not remain a no-op");
  require(scene.edit(0, 7), "updating existing state at capacity failed");
  require(scene.edit(0, 0) && scene.edit(1048575, 6), "freed sparse entry was not reusable");
  const auto maximum = scene.export_text();
  require(maximum.size() <= VisualScene::max_export_chars, "legal scene exceeds text limit");
  require(VisualScene::import_text(maximum).export_text() == maximum,
          "maximum sparse scene roundtrip failed");
}

std::string document(const std::string& count, const std::string& seed,
                     const std::string& version, const std::string& size,
                     const std::string& edits = "") {
  return "MUNDOX_OBJECTS_V1\ncount=" + count + "\nseed=" + seed +
      "\nversion=" + version + "\nedits=" + size + '\n' + edits;
}

void parser_cases() {
  const auto empty = document("1", "0", "0", "0");
  require(VisualScene::import_text(empty.substr(0, empty.size() - 1)).export_text() == empty,
          "optional final LF rejected");
  require(VisualScene::import_text(document("0001", "-0", "-0", "00")).export_text() == empty,
          "Java leading-zero/-0 compatibility lost");
  require(VisualScene::import_text(document("1", "2147483647", "0", "1", "-0:07\n"))
              .export_text() == document("1", "2147483647", "0", "1", "0:7\n"),
          "canonicalization of valid edit integers failed");
  require(VisualScene::import_text(document("1", "-2147483648", "0", "0")).seed() == INT32_MIN,
          "minimum signed seed rejected");
  const std::string bad[] = {
    "", "MUNDOX_OBJECTS_V2\ncount=1\nseed=0\nversion=0\nedits=0\n",
    empty + '\n', empty + "unexpected", " " + empty,
    "MUNDOX_OBJECTS_V1\r\ncount=1\nseed=0\nversion=0\nedits=0\n",
    document("0", "0", "0", "0"), document("-1", "0", "0", "0"),
    document("1048577", "0", "0", "0"), document("2147483648", "0", "0", "0"),
    document("+1", "0", "0", "0"), document(" 1", "0", "0", "0"),
    document("1 ", "0", "0", "0"), document("1\r", "0", "0", "0"),
    document("", "0", "0", "0"), document("-", "0", "0", "0"),
    document("1", "2147483648", "0", "0"), document("1", "-2147483649", "0", "0"),
    document("1", "+0", "0", "0"), document("1", "0x0", "0", "0"),
    document("1", "0", "-1", "0"), document("1", "0", "9223372036854775808", "0"),
    document("1", "0", "-9223372036854775808", "0"),
    document("1", "0", "-9223372036854775809", "0"),
    document("1", "0", "18446744073709551616", "0"),
    document("1", "0", "0", "-1"), document("1", "0", "0", "4097"),
    document("1", "0", "0", "1"), document("1", "0", "0", "0", "0:1\n"),
    document("2", "0", "0", "2", "1:1\n0:2\n"),
    document("2", "0", "0", "2", "0:1\n00:2\n"),
    document("1", "0", "0", "1", "1:1\n"),
    document("1", "0", "0", "1", "-1:1\n"),
    document("1", "0", "0", "1", "0:0\n"),
    document("1", "0", "0", "1", "0:8\n"),
    document("1", "0", "0", "1", "0:-1\n"),
    document("1", "0", "0", "1", "0:1:2\n"),
    document("1", "0", "0", "1", ":1\n"),
    document("1", "0", "0", "1", "0:\n"),
    document("1", "0", "0", "1", "0:+1\n"),
    document("1", "0", "0", "1", "0:1\r\n"),
    document("1", "0", "0", "1", "2147483648:1\n"),
    document("1", "0", "0", "1", "0:2147483648\n"),
    "MUNDOX_OBJECTS_V1\nseed=0\ncount=1\nversion=0\nedits=0\n",
    document("\xd9\xa1", "0", "0", "0"),
    document(std::string("1\0", 2), "0", "0", "0")
  };
  VisualScene protected_scene(37, -23);
  protected_scene.edit(36, 7);
  const auto before = protected_scene.export_text();
  for (const auto& malformed : bad) {
    rejects<std::invalid_argument>([&] { protected_scene = VisualScene::import_text(malformed); },
                                   "malformed scene accepted");
    require(protected_scene.export_text() == before, "failed import partly replaced target scene");
  }
  const auto exact_limit = document(std::string(65536 - empty.size(), '0') + "1", "0", "0", "0");
  require(exact_limit.size() == VisualScene::max_export_chars, "text-limit fixture incorrect");
  require(VisualScene::import_text(exact_limit).export_text() == empty,
          "legal text exactly at size limit rejected");
  rejects<std::invalid_argument>([&] { VisualScene::import_text(exact_limit + '\n'); },
                                 "text above size limit accepted");
}

void exhausted_version_cases() {
  auto empty = VisualScene::import_text(document("2", "0", "9223372036854775807", "0"));
  require(empty.version() == VisualScene::max_version, "maximum version not retained");
  require(!empty.edit(0, 0), "no-op edit at maximum version rejected");
  empty.clear_edits();
  const auto initial = empty.export_text();
  rejects<std::logic_error>([&] { empty.edit(0, 1); }, "exhausted version advanced");
  require(empty.export_text() == initial, "version failure inserted edit");
  auto scene = VisualScene::import_text(document("2", "0", "9223372036854775806", "1", "0:1\n"));
  require(scene.edit(0, 6) && scene.version() == VisualScene::max_version,
          "last valid revision was lost");
  const auto maximum = scene.export_text();
  require(!scene.edit(0, 6), "same edit failed at exhausted revision");
  rejects<std::logic_error>([&] { scene.edit(0, 7); }, "update overflowed revision");
  rejects<std::logic_error>([&] { scene.edit(0, 0); }, "removal overflowed revision");
  rejects<std::logic_error>([&] { scene.edit(1, 1); }, "insertion overflowed revision");
  rejects<std::logic_error>([&] { scene.clear_edits(); }, "clear overflowed revision");
  require(scene.export_text() == maximum, "exhausted operation changed scene");
  require(VisualScene::import_text(maximum).export_text() == maximum,
          "maximum revision scene failed roundtrip");
}

void locale_case() {
  struct GroupedNumbers : std::numpunct<char> {
    char do_thousands_sep() const override { return '_'; }
    std::string do_grouping() const override { return "\3"; }
  };
  const auto previous = std::locale();
  std::locale::global(std::locale(previous, new GroupedNumbers));
  try {
    VisualScene scene(1048576, INT32_MIN);
    scene.edit(1048575, 7);
    require(scene.export_text() == document("1048576", "-2147483648", "1", "1", "1048575:7\n"),
            "global locale corrupted canonical decimal format");
    std::locale::global(previous);
  } catch (...) {
    std::locale::global(previous);
    throw;
  }
}
}  // namespace

int main() {
  try {
    golden_transform_cases(); finite_grid_cases(); persistent_edit_cases();
    edit_capacity_cases(); parser_cases(); exhausted_version_cases(); locale_case();
    std::cout << "PASS visual_scene checks=" << checks << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL visual_scene " << error.what() << '\n';
    return 1;
  }
}
