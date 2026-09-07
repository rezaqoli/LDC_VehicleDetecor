// time.h polyfill for native tests on Windows
#pragma once
#include <time.h>
#include <cstring>

// Windows MSVC and MinGW may not have localtime_r / gmtime_r. Provide one.
#if !defined(_WIN32) || defined(__GNUC__)
  // POSIX systems already have them.
#else
static inline struct tm *localtime_r(const time_t *t, struct tm *out) {
  if (!out) return nullptr;
  struct tm *p = localtime(t);
  if (p) { *out = *p; return out; }
  return nullptr;
}
static inline struct tm *gmtime_r(const time_t *t, struct tm *out) {
  if (!out) return nullptr;
  struct tm *p = gmtime(t);
  if (p) { *out = *p; return out; }
  return nullptr;
}
#endif
