#pragma once

// Mapper helpers are shared by Pico GCC firmware and the MSVC openMSX build.
// Firmware must inline bus-loop helpers to avoid calls into flash.
#if defined(_MSC_VER)
#define OMNISCC_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define OMNISCC_FORCE_INLINE __attribute__((always_inline)) inline
#else
#define OMNISCC_FORCE_INLINE inline
#endif
