#if !defined(SCROLLY_INTRINSICS_H)

#include "math.h"


inline s32
round_float_to_s32(f32 value) {
    s32 result = (s32)roundf(value);
    return result;
}

inline u32
round_float_to_u32(f32 value) {
    u32 result = (u32)roundf(value);
    return result;
}

//
// @note Bit operations
//

inline u32
rotate_left(u32 value, s32 amount) {
    u32 result = _rotl(value, amount);
    return result;
}

inline u32
rotate_right(u32 value, s32 amount) {
    u32 result = _rotr(value, amount);
    return result;
}

// @note bit_scan
struct Bit_Scan_Result {
    b32 found;
    u32 index;
};

inline Bit_Scan_Result
find_least_significant_set_bit(u32 value) {
    Bit_Scan_Result result = {};
    
#if COMPILER_MSVC
    result.found = _BitScanForward((unsigned long *)&result.index, value);
#else
    for (s32 test = 0; test < 32; ++test) {
        if (value & (1 << test)) {
            result.index = test;
            result.found = true;
            break;
        }
    }
#endif
    
    return result;
}

#define SCROLLY_INTRINSICS_H
#endif
