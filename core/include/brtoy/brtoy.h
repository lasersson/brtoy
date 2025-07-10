#pragma once
#include <stdint.h>

#ifdef BRTOY_DLL
#ifdef BRTOY_DLL_EXPORT
#define BRTOY_API __declspec(dllexport)
#else
#define BRTOY_API __declspec(dllimport)
#endif
#else
#define BRTOY_API
#endif

#define BRTOY_ASSERT(x)                                                                            \
    do {                                                                                           \
        if (!(x))                                                                                  \
            brtoy::debugBreak();                                                                   \
    } while (0)

namespace brtoy {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using OsHandle = u64;

void debugBreak();

} // namespace brtoy
