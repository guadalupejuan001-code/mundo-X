#include "mundo_x/infinite_address.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
std::size_t checks = 0;
std::size_t failures = 0;

void expect(bool condition, const char* context) {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL " << context << '\n';
  }
}

template<class Exception, class Function>
void rejects(Function function, const char* context) {
  bool rejected = false;
  try {
    function();
  } catch (const Exception&) {
    rejected = true;
  } catch (const std::exception& error) {
    std::cerr << "Unexpected exception: " << error.what() << '\n';
  }
  expect(rejected, context);
}

void canonical_inputs() {
  using mundo_x::infinite_detail::canonical_address;
  for (const auto& item : std::vector<std::pair<std::string, std::string>>{
           {"0", "0"}, {"1", "1"}, {"000", "0"}, {"0001", "1"},
           {"00020", "20"}, {"001000", "1000"},
           {"18446744073709551616", "18446744073709551616"},
           {"00018446744073709551616", "18446744073709551616"}}) {
    const auto original = item.first;
    const auto actual = canonical_address(original, original.size());
    expect(actual == item.second, "Decimal address normalization");
    expect(canonical_address(actual, actual.size()) == actual,
           "Canonicalization is idempotent");
    expect(original == item.first, "Canonicalization preserves the input");
  }

  const std::string power100 = "1" + std::string(100, '0');
  const std::string thousand_digits(1000, '9');
  expect(canonical_address(power100, 101) == power100, "10^100 is exact");
  expect(canonical_address(thousand_digits, 1000) == thousand_digits,
         "A thousand-digit ID needs no machine integer");
  expect(canonical_address(std::string(1000, '0'), 1000) == "0",
         "Leading zero aliases within the budget normalize to zero");
  expect(canonical_address("9", std::numeric_limits<std::size_t>::max()) == "9",
         "A large configured budget does not allocate that budget");

  rejects<std::length_error>([&] { canonical_address(power100, 100); },
                            "Input one digit above the budget is rejected");
  rejects<std::length_error>([] { canonical_address(std::string(1001, '0'), 1000); },
                            "Leading zeros cannot bypass the input budget");
  rejects<std::length_error>([] { canonical_address("0001", 1); },
                            "A short normalized alias still pays for its input");
  rejects<std::length_error>([] { canonical_address("0", 0); },
                            "A zero digit budget accepts no address");
  rejects<std::invalid_argument>([] { canonical_address("", 1000); },
                                "An empty address is not zero");

  const std::vector<std::string> invalid{
      "+1", "-1", "-0", " 1", "1 ", "1\t", "\n1", "1\r", "1\n0",
      "1e3", "1E3", "1.0", "0x10", "1_000", "1,000", "NaN", "inf",
      std::string("12\0" "3", 4), u8"\u0661", u8"\uff11", u8"\u00b2",
      u8"\u22121", u8"1\u00a0"};
  for (const auto& value : invalid)
    rejects<std::invalid_argument>([&] { canonical_address(value, 1000); },
                                  "Only nonempty ASCII decimal IDs are accepted");

  // Check every single-byte spelling, including signed-char/high-bit bytes.
  // This catches accidental locale-sensitive digit classification.
  for (unsigned byte = 0; byte <= 255; ++byte) {
    const std::string value(1, static_cast<char>(byte));
    if (byte >= static_cast<unsigned>('0') && byte <= static_cast<unsigned>('9'))
      expect(canonical_address(value, 1) == value, "ASCII decimal byte accepted");
    else
      rejects<std::invalid_argument>([&] { canonical_address(value, 1); },
                                    "A nondecimal byte is rejected");
  }
}

void bell_partners() {
  using mundo_x::infinite_detail::canonical_address;
  using mundo_x::infinite_detail::is_odd;
  using mundo_x::infinite_detail::partner_address;
  const std::string power100 = "1" + std::string(100, '0');
  const std::string power100plus1 = "1" + std::string(99, '0') + "1";
  const std::string thousand_even = std::string(999, '9') + "8";
  const std::string thousand_odd(1000, '9');
  const std::vector<std::pair<std::string, std::string>> pairs{
      {"0", "1"}, {"2", "3"}, {"4", "5"}, {"6", "7"}, {"8", "9"},
      {"10", "11"}, {"98", "99"}, {"998", "999"},
      {"18446744073709551614", "18446744073709551615"},
      {"18446744073709551616", "18446744073709551617"},
      {power100, power100plus1}, {thousand_even, thousand_odd}};
  for (const auto& pair : pairs) {
    expect(!is_odd(pair.first) && is_odd(pair.second), "Large-ID parity is exact");
    expect(partner_address(pair.first) == pair.second, "Even Bell ID maps to next ID");
    expect(partner_address(pair.second) == pair.first, "Odd Bell ID maps to previous ID");
    expect(partner_address(partner_address(pair.first)) == pair.first,
           "Bell partner mapping is an involution");
    expect(partner_address(pair.first).size() == pair.first.size() &&
               partner_address(pair.second).size() == pair.second.size(),
           "Partner lookup cannot increase the digit budget");
    expect(canonical_address(partner_address(pair.second), pair.second.size()) == pair.first,
           "Partner output stays canonical even for 1 to 0");
  }
  expect(partner_address(canonical_address("000999", 6)) == "998",
         "An input alias identifies the same Bell pair");

  for (const auto& value : std::vector<std::string>{"", "00", "01", "+1", "-1", "1 ", "a0", u8"\u0661"}) {
    rejects<std::invalid_argument>([&] { is_odd(value); },
                                  "Parity rejects a noncanonical key");
    rejects<std::invalid_argument>([&] { partner_address(value); },
                                  "Partner lookup rejects a noncanonical key");
  }
}

void numeric_ordering() {
  using mundo_x::infinite_detail::AddressLess;
  using mundo_x::infinite_detail::canonical_address;
  const std::vector<std::string> ordered{
      "0", "1", "2", "9", "10", "99", "100", "998", "999",
      "18446744073709551615", "18446744073709551616",
      "1" + std::string(100, '0'), std::string(1000, '9')};
  auto shuffled = ordered;
  std::reverse(shuffled.begin(), shuffled.end());
  std::sort(shuffled.begin(), shuffled.end(), AddressLess{});
  expect(shuffled == ordered, "Address ordering is numeric beyond machine precision");
  const AddressLess less;
  for (std::size_t i = 0; i < ordered.size(); ++i) {
    expect(!less(ordered[i], ordered[i]), "Ordering is irreflexive");
    for (std::size_t j = i + 1; j < ordered.size(); ++j)
      expect(less(ordered[i], ordered[j]) && !less(ordered[j], ordered[i]),
             "Canonical numeric ordering is consistent across lengths");
  }
  std::set<std::string, AddressLess> aliases;
  for (const std::string& value : {std::string("0"), std::string("000"), std::string("1"),
                                  std::string("001"), std::string("10"), std::string("010")})
    aliases.insert(canonical_address(value, 3));
  expect(aliases.size() == 3, "Canonical aliases cannot create duplicate map entries");
}
}  // namespace

int main() {
  try {
    canonical_inputs();
    bell_partners();
    numeric_ordering();
  } catch (const std::exception& error) {
    std::cerr << "Unexpected exception: " << error.what() << '\n';
    return 1;
  }
  std::cout << "infinite_address checks=" << checks << " failures=" << failures << '\n';
  return failures == 0 ? 0 : 1;
}
