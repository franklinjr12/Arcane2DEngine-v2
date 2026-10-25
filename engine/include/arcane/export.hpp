#pragma once

#if defined(_WIN32)
  #if defined(ARCANE_BUILD_RUNTIME)
    #define ARCANE_API __declspec(dllexport)
  #else
    #define ARCANE_API __declspec(dllimport)
  #endif
#else
  #define ARCANE_API __attribute__((visibility("default")))
#endif

#if defined(_WIN32)
  #define ARCANE_GAME_API extern "C" __declspec(dllexport)
#else
  #define ARCANE_GAME_API extern "C" __attribute__((visibility("default")))
#endif
