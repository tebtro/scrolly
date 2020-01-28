#if !defined(SCROLLY_MATH_H)

#include "scrolly_intrinsics.h"

//
// Vector2
//

union Vector2 {
    struct {
        f32 x;
        f32 y;
    };
    f32 e[2];
};

struct Rectangle_s32 {
    s32 min_x, min_y;
    s32 max_x, max_y;
};

internal Rectangle_s32
aspect_ratio_fit(u32 render_width, u32 render_height,
                 u32 window_width, u32 window_height) {
    Rectangle_s32 result = {};
    
    if ((render_width <= 0) || (render_height <= 0) ||
        (window_width <= 0) || (window_height <= 0)) {
        return result;
    }
    
    f32 optimal_window_width  = (f32)window_height * ((f32)render_width  / (f32)render_height);
    f32 optimal_window_height = (f32)window_width  * ((f32)render_height / (f32)render_width);
    
    if (optimal_window_width > (f32)window_width) {
        // @note Width-constrained display - top and bottom black bars.
        result.min_x = 0;
        result.max_x = window_width;
        
        f32 empty = (f32)window_height - optimal_window_height;
        s32 half_empty = round_float_to_s32(0.5f*empty);
        s32 use_height = round_float_to_s32(optimal_window_height);
        
        result.min_y = half_empty;
        result.max_y = result.min_y + use_height;
    }
    else {
        // @note Height-constrained display - left and right black bars
        result.min_y = 0;
        result.max_y = window_height;
        
        f32 empty = (f32)window_width - optimal_window_width;
        s32 half_empty = round_float_to_s32(0.5f*empty);
        s32 use_width  = round_float_to_s32(optimal_window_width);
        
        result.min_x = half_empty;
        result.max_x = result.min_x + use_width;
    }
    
    return result;
}


inline f32
clamp(f32 min, f32 value, f32 max) {
    f32 result = value;
    
    if (result < min) {
        result = min;
    }
    else if (result > max) {
        result = max;
    }
    
    return result;
}

inline f32
clamp_01(f32 value) {
    f32 result = clamp(0.0f, value, 1.0f);
    return result;
}

inline f32
clamp_01_map_to_range(f32 min, f32 t, f32 max) {
    f32 result = 0.0f;
    
    f32 range = max - min;
    if (range != 0.0f) {
        result = clamp_01((t - min) / range);
    }
    
    return result;
}


#define SCROLLY_MATH_H
#endif
