// fuzz_watchdog.h — per-input CPU-time watchdog shared by the RE/flex fuzz harnesses.
//
// RE/flex's Pattern compiler can spend unbounded time building the DFA for a tiny adversarial
// regex (e.g. the 4-byte `d$*[` spins in Pattern::analyze_dfa, lib/pattern.cpp) — a real but
// slow/DoS-class finding rather than a memory-safety bug. Under libFuzzer's 1200s default
// per-unit timeout, the first such unit stalls the whole campaign AND the local fuzz-smoke gate.
//
// Arming a watchdog around every LLVMFuzzerTestOneInput turns that into a recorded timeout
// instead of an indefinite hang. It uses ITIMER_VIRTUAL / SIGVTALRM on purpose: libFuzzer's own
// -timeout machinery owns ITIMER_REAL / SIGALRM, so taking that signal would disable libFuzzer's
// timeout and RSS checks. Counting CPU time is also the right measure for a spin loop.
//
// The handler uses libFuzzer's kTimeoutExitCode (70) so the unit is classified as a timeout.
#ifndef MAYHEM_FUZZ_WATCHDOG_H
#define MAYHEM_FUZZ_WATCHDOG_H

#include <csignal>
#include <sys/time.h>
#include <unistd.h>

#ifndef MAYHEM_FUZZ_UNIT_TIMEOUT
#define MAYHEM_FUZZ_UNIT_TIMEOUT 5   /* CPU seconds per input before we declare a timeout */
#endif

namespace mayhem_fuzz {

extern "C" inline void watchdog_handler(int)
{
  _exit(70);   // libFuzzer's kTimeoutExitCode — recorded as a timeout, not a clean exit
}

inline void watchdog_set(long seconds)
{
  struct itimerval it;
  it.it_value.tv_sec = seconds;
  it.it_value.tv_usec = 0;
  it.it_interval.tv_sec = 0;
  it.it_interval.tv_usec = 0;
  setitimer(ITIMER_VIRTUAL, &it, NULL);
}

// RAII: arm on entry to LLVMFuzzerTestOneInput, disarm on ANY exit path (early return or throw).
class Watchdog {
 public:
  Watchdog()
  {
    static bool installed = false;
    if (!installed)
    {
      struct sigaction sa;
      sa.sa_handler = watchdog_handler;
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;
      sigaction(SIGVTALRM, &sa, NULL);
      installed = true;
    }
    watchdog_set(MAYHEM_FUZZ_UNIT_TIMEOUT);
  }
  ~Watchdog() { watchdog_set(0); }
 private:
  Watchdog(const Watchdog &);
  Watchdog &operator=(const Watchdog &);
};

} // namespace mayhem_fuzz

#endif // MAYHEM_FUZZ_WATCHDOG_H
