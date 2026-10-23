#pragma once

#ifdef ARCANE2D_COMPILE
#define A2D_API __declspec(dllexport)
#else
#ifdef ARCANE2D_COMPILE_EXE
#define A2D_API
#else
#define A2D_API __declspec(dllimport)
#endif
#endif