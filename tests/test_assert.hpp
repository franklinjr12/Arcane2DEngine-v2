#pragma once

#include <cstdio>
#include <cstdlib>

// Intentionally not a framework: a single macro that prints context and
// exits with a nonzero status, which is all CTest needs.
#define ARCANE_ASSERT_TRUE(expr)                                                        \
  do {                                                                                  \
    if (!(expr)) {                                                                      \
      std::fprintf(stderr, "ASSERT FAILED: %s (%s:%d)\n", #expr, __FILE__, __LINE__);   \
      std::exit(1);                                                                     \
    }                                                                                    \
  } while (0)
