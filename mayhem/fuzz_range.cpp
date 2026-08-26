// Harness for RE/flex's Unicode / POSIX character-class layer and the regex CONVERTER
// (lib/unicode.cpp, lib/posix.cpp, lib/convert.cpp) — the code that turns `\p{...}` class
// names and library-specific regex syntax into RE/flex's internal form.
//
// The original Mayhem target only called reflex::Unicode::range() (a table lookup, ~no edges).
// That call is kept verbatim as the first step, and the harness additionally drives the POSIX
// table, the Unicode composition/case tables and reflex::convert(), which is the real parser
// consuming those tables.
//
// Input layout (FuzzedDataProvider):
//   [0]                     flag byte selecting a subset of reflex::convert_flag
//   ConsumeRandomLengthString  class name passed to Unicode::range / posix::range
//   remainder                  regex pattern handed to reflex::convert()
#include <stdint.h>
#include <stddef.h>
#include <string>

#include <fuzzer/FuzzedDataProvider.h>

#include <reflex/unicode.h>
#include <reflex/posix.h>
#include <reflex/convert.h>
#include <reflex/error.h>

#include "fuzz_watchdog.h"

// The regex-library signature accepted by reflex::Matcher (see matcher.h) — enables the full
// set of modifiers/escapes so the converter's branches are reachable.
static const char *kSignature = "imsx#=^:abcdefhijklnrstuvwxzABDHLNQSUW0<>?";

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  // Bound the CPU time this single input may consume (see fuzz_watchdog.h).
  mayhem_fuzz::Watchdog watchdog;

  if (size < 2 || size > 4096)
    return 0;

  FuzzedDataProvider provider(data, size);

  // Flag byte: a subset of reflex::convert_flag (unicode/lex/u4/anycase/multiline/dotall/
  // freespace/notnewline/permissive/recap/basic), masked so the value is always meaningful.
  const uint8_t flagbits = provider.ConsumeIntegral<uint8_t>();
  reflex::convert_flag_type flags = reflex::convert_flag::none;
  if (flagbits & 0x01) flags |= reflex::convert_flag::unicode;
  if (flagbits & 0x02) flags |= reflex::convert_flag::lex;
  if (flagbits & 0x04) flags |= reflex::convert_flag::u4;
  if (flagbits & 0x08) flags |= reflex::convert_flag::anycase;
  if (flagbits & 0x10) flags |= reflex::convert_flag::multiline;
  if (flagbits & 0x20) flags |= reflex::convert_flag::dotall;
  if (flagbits & 0x40) flags |= reflex::convert_flag::freespace;
  if (flagbits & 0x80) flags |= reflex::convert_flag::notnewline;

  // 1) The original target's code path: a NUL-terminated class name looked up in the Unicode
  //    and POSIX character-class tables.
  const std::string name = provider.ConsumeRandomLengthString();
  reflex::Unicode::range(name.c_str());
  reflex::Posix::range(name.c_str());

  // 2) The Unicode composition + case-mapping tables. Code points are kept inside the valid
  //    Unicode scalar range — out-of-range values are not a legal input to these functions.
  if (name.size() >= 2)
  {
    const int prev = static_cast<unsigned char>(name[0]) << 8 | static_cast<unsigned char>(name[1]);
    const int next = static_cast<int>(name.size()) & 0x10FFFF;
    reflex::Unicode::compose(prev & 0x10FFFF, next);
    reflex::Unicode::toupper(prev & 0x10FFFF);
    reflex::Unicode::tolower(prev & 0x10FFFF);
    reflex::Unicode::invcase(prev & 0x10FFFF);
  }

  // 3) The converter itself — the main consumer of the class tables above (lib/convert.cpp).
  std::string pattern = provider.ConsumeRemainingBytesAsString();
  if (pattern.empty() || pattern.size() > 256)
    return 0;
  try
  {
    reflex::convert(pattern, kSignature, flags);
  }
  catch (const reflex::regex_error &)
  {
    // rejected by the converter — not a defect
  }
  return 0;
}
