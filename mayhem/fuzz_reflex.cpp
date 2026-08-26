// In-process harness for the RE/flex regex engine — the same libreflex code path the
// `reflex` CLI embeds into every generated scanner (pattern parsing and DFA construction
// in lib/pattern.cpp, matching in lib/matcher.cpp).
//
// Input layout: <regex> '\n' <subject> — compile the regex, then scan/find over the subject.
//
// NOTE on the loop bounds: reflex::Matcher::scan() matches at the CURRENT position, so a
// pattern that can match the empty string (e.g. `a*` against `b`) makes it return a
// zero-length match forever without advancing. An unbounded `while (scan())` therefore
// spins on such inputs — a harness bug, not an engine defect. Both loops below stop on a
// zero-length match and are additionally capped, so every input terminates.
#include <stdint.h>
#include <stddef.h>
#include <string>

#include <reflex/matcher.h>
#include <reflex/pattern.h>
#include <reflex/error.h>

#include "fuzz_watchdog.h"

// Upper bound on match iterations per input — plenty for the 1 KiB subjects below.
static const size_t kMaxMatches = 4096;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  // Bound the CPU time this single input may consume (see fuzz_watchdog.h).
  mayhem_fuzz::Watchdog watchdog;

  if (size < 2 || size > 1024)
    return 0;
  std::string in(reinterpret_cast<const char *>(data), size);
  size_t nl = in.find('\n');
  std::string re = in.substr(0, nl == std::string::npos ? in.size() : nl);
  if (re.size() > 128)
    return 0;
  std::string subject = nl == std::string::npos ? std::string() : in.substr(nl + 1);
  try
  {
    reflex::Pattern pattern(re);
    reflex::Matcher matcher(pattern, subject);
    for (size_t i = 0; i < kMaxMatches && matcher.scan() != 0; ++i)
      if (matcher.size() == 0)
        break;                       // zero-length match: scan() would never advance
    matcher.input(subject);
    for (size_t i = 0; i < kMaxMatches && matcher.find() != 0; ++i)
      if (matcher.size() == 0 && matcher.at_end())
        break;
  }
  catch (const reflex::regex_error &)
  {
    // invalid regex — rejected by the parser, not a defect
  }
  return 0;
}
