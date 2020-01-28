#include "scrolly.h"
#include "scrolly_math.h"
#include "scrolly_intrinsics.h"


#include <stdlib.h>
#include <time.h>

internal u32
get_random_number_in_range(int min, int max) {
    srand((unsigned int)time(null));
    u32 random = min + (rand() % ((max + 1) - min));
    return random;
}

internal void
render_rectangle(Game_Offscreen_Buffer *buffer,
                 Vector2 v_min, Vector2 v_max,
                 f32 r, f32 g, f32 b) {
    s32 min_x = round_float_to_s32(v_min.x);
    s32 min_y = round_float_to_s32(v_min.y);
    s32 max_x = round_float_to_s32(v_max.x);
    s32 max_y = round_float_to_s32(v_max.y);
    
    if (min_x < 0)  min_x = 0;
    if (min_y < 0)  min_y = 0;
    if (max_x > buffer->width)   max_x = buffer->width;
    if (max_y > buffer->height)  max_y = buffer->height;
    
    u32 color = ((round_float_to_u32(r * 255.0f) << 16) |
                 (round_float_to_u32(g * 255.0f) << 8)  |
                 (round_float_to_u32(b * 255.0f) << 0));
    
    u8 *row = ((u8 *)buffer->memory +
               min_x * buffer->bytes_per_pixel +
               min_y * buffer->pitch);
    for (int y = min_y; y < max_y; ++y) {
        u32 *pixel = (u32 *)row;
        for (int x = min_x; x < max_x; ++x) {
            *pixel++ = color;
        }
        row += buffer->pitch;
    }
}

// @todo premultiplied alpha
// @note https://docs.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapfileheader
#pragma pack(push, 1)
// @note BITMAPINFO structure
struct Bitmap_Header {
    // @note BITMAPFILEHEADER structure
    u16 file_type;
    u32 file_size;
    u16 reserved_1;
    u16 reserved_2;
    u32 bitmap_offset;
    
    // @note BITMAPINFOHEADER structure
    u32 size;
    s32 width;
    s32 height;
    u16 planes;
    u16 bit_per_pixel;
    
    u32 compression_method;
    u32 size_of_bitmap;
    s32 horizontal_resolution;
    s32 vertical_resolution;
    u32 colors_used;
    u32 colors_important;
    
    // @note RGBQUAD structure
    u32 red_mask;
    u32 green_mask;
    u32 blue_mask;
};
#pragma pack(pop)

internal Loaded_Bitmap
load_bitmap(Platform_Read_Entire_File_Function *platform_read_entire_file, char *filename) {
    Loaded_Bitmap result = {};
    
    Read_File_Result read_result = platform_read_entire_file(filename);
    if (read_result.memory_size == 0)  return result;
    
    Bitmap_Header *header = (Bitmap_Header *)read_result.memory;
    // @todo handle compression
    assert(header->compression_method == 3);
    // @todo handle top-down or bottom-up order. For top-down the height will be negative.
    u32 *pixels = (u32 *)((u8 *)read_result.memory + header->bitmap_offset);
    
    // 0x AA RR GG BB
    
    u32 red_mask   = header->red_mask;
    u32 green_mask = header->green_mask;
    u32 blue_mask  = header->blue_mask;
    u32 alpha_mask = ~(red_mask | green_mask | blue_mask);
    
    Bit_Scan_Result red_scan = find_least_significant_set_bit(red_mask);
    Bit_Scan_Result green_scan = find_least_significant_set_bit(green_mask);
    Bit_Scan_Result blue_scan = find_least_significant_set_bit(blue_mask);
    Bit_Scan_Result alpha_scan = find_least_significant_set_bit(alpha_mask);
    
    assert(red_scan.found);
    assert(green_scan.found);
    assert(blue_scan.found);
    assert(alpha_scan.found);
    
    s32 alpha_shift = 24 - (s32)alpha_scan.index;
    s32 red_shift   = 16 - (s32)red_scan.index;
    s32 green_shift =  8 - (s32)green_scan.index;
    s32 blue_shift  =  0 - (s32)blue_scan.index;
    
    u32 *pixel = pixels;
    for (s32 y = 0; y < header->height; ++y) {
        for (s32 x = 0; x < header->width; ++x) {
            u32 color = *pixel;
#if 1
            *pixel = (rotate_left(color & alpha_mask, alpha_shift) |
                      rotate_left(color & red_mask,   red_shift)   |
                      rotate_left(color & green_mask, green_shift) |
                      rotate_left(color & blue_mask,  blue_shift));
#else
            *pixel = ((((color >> alpha_scan.index) & 0xFF) << 24) |
                      (((color >> red_scan.index)   & 0xFF) << 16) |
                      (((color >> green_scan.index) & 0xFF) <<  8) |
                      (((color >> blue_scan.index)  & 0xFF) <<  0));
#endif
            
            ++pixel;
        }
    }
    
    result.width  = header->width;
    result.height = header->height;
    result.pixels  = pixels;
    
    return result;
}

internal void
render_bitmap(Game_Offscreen_Buffer *buffer, Loaded_Bitmap *bitmap,
              f32 float_x, f32 float_y, s32 align_x = 0, s32 align_y = 0) {
    float_x -= (f32)align_x;
    float_y -= (f32)align_y;
    
    s32 min_x = round_float_to_s32(float_x);
    s32 min_y = round_float_to_s32(float_y);
    s32 max_x = round_float_to_s32(float_x + (f32)bitmap->width);
    s32 max_y = round_float_to_s32(float_y + (f32)bitmap->height);
    
    s32 source_offset_x = 0;
    if (min_x < 0) {
        source_offset_x = -min_x;
        min_x = 0;
    }
    s32 source_offset_y = 0;
    if (min_y < 0) {
        source_offset_y = -min_y;
        min_y = 0;
    }
    if (max_x > buffer->width)   max_x = buffer->width;
    if (max_y > buffer->height)  max_y = buffer->height;
    
    u32 *source_row = bitmap->pixels + bitmap->width * (bitmap->height - 1);
    source_row += bitmap->width * -source_offset_y + source_offset_x;
    u8 *dest_row = ((u8 *)buffer->memory +
                    min_x * buffer->bytes_per_pixel +
                    min_y * buffer->pitch);
    for (s32 y = min_y; y < max_y; ++y) {
        u32 *source = source_row;
        u32 *dest = (u32 *)dest_row;
        
        for (s32 x = min_x; x < max_x; ++x) {
            f32 alpha = (f32)((u8)(*source >> 24) & 0xFF) / 255.0f;
            
            f32 source_red    = (f32)((u8)(*source >> 16) & 0xFF);
            f32 source_green  = (f32)((u8)(*source >>  8) & 0xFF);
            f32 source_blue   = (f32)((u8)(*source >>  0) & 0xFF);
            
            f32 dest_alpha = (f32)((u8)(*dest >> 24) & 0xFF);
            f32 dest_red   = (f32)((u8)(*dest >> 16) & 0xFF);
            f32 dest_green = (f32)((u8)(*dest >>  8) & 0xFF);
            f32 dest_blue  = (f32)((u8)(*dest >>  0) & 0xFF);
            
            // @note linear blending
            f32 red   = (1.0f - alpha) * dest_red   + alpha * source_red;
            f32 green = (1.0f - alpha) * dest_green + alpha * source_green;
            f32 blue  = (1.0f - alpha) * dest_blue  + alpha * source_blue;
            
            *dest = (((u32)(dest_alpha)   << 24) |
                     ((u32)(red   + 0.5f) << 16) |
                     ((u32)(green + 0.5f) << 8)  |
                     ((u32)(blue  + 0.5f) << 0));
            
            ++dest;
            ++source;
        }
        
        source_row -= bitmap->width;
        dest_row += buffer->pitch;
    }
}

internal void
render_background(Game_Offscreen_Buffer *buffer, Loaded_Bitmap *bitmap, s32 back_offset_x, s32 back_offset_y) {
    render_bitmap(buffer, bitmap,
                  (f32)-back_offset_x, (f32)-back_offset_y);
    
    assert(back_offset_y == 0);
    render_bitmap(buffer, bitmap,
                  (f32)bitmap->width - (f32)back_offset_x, 
                  (f32)back_offset_y);
}


// @cleanup @copynpaste
internal void
render_char(Game_Offscreen_Buffer *buffer, Game_State *game_state, f32 float_x, f32 float_y, char _char) {
    Loaded_Bitmap *bitmap = &game_state->bmp_font;
    int char_value = _char;
    int char_index = 0;
    if (char_value == 32) {
        Vector2 v_min = {
            float_x,
            float_y
        };
        Vector2 v_max = {
            v_min.x + 11.0f,
            v_min.y + 17.0f
        };
        render_rectangle(buffer, v_min, v_max, 1.0f, 1.0f, 1.0f);
        return;
    }
    else if (char_value >= 48 && char_value <= 57) {
        char_index = char_value - 48;
    }
    else if (char_value >= 65 && char_value <= 90) {
        char_index = char_value - 65 + (36);
    }
    else if (char_value >= 97 && char_value <= 122) {
        char_index = char_value - 97 + (10);
    }
    
    
    s32 min_x = round_float_to_s32(float_x);
    s32 min_y = round_float_to_s32(float_y);
    s32 max_x = round_float_to_s32(float_x + (f32)11);
    s32 max_y = round_float_to_s32(float_y + (f32)17);
    
    s32 source_offset_x = 0;
    if (min_x < 0) {
        source_offset_x = -min_x;
        min_x = 0;
    }
    s32 source_offset_y = 0;
    if (min_y < 0) {
        source_offset_y = -min_y;
        min_y = 0;
    }
    if (max_x > buffer->width)   max_x = buffer->width;
    if (max_y > buffer->height)  max_y = buffer->height;
    
    u32 *source_row = bitmap->pixels + bitmap->width * (bitmap->height - 1);
    source_row += bitmap->width * -source_offset_y + source_offset_x;
    source_row += bitmap->width * -0 + 11 * char_index;
    u8 *dest_row = ((u8 *)buffer->memory +
                    min_x * buffer->bytes_per_pixel +
                    min_y * buffer->pitch);
    for (s32 y = min_y; y < max_y; ++y) {
        u32 *source = source_row;
        u32 *dest = (u32 *)dest_row;
        
        for (s32 x = min_x; x < max_x; ++x) {
            f32 alpha = (f32)((u8)(*source >> 24) & 0xFF) / 255.0f;
            
            f32 source_red    = (f32)((u8)(*source >> 16) & 0xFF);
            f32 source_green  = (f32)((u8)(*source >>  8) & 0xFF);
            f32 source_blue   = (f32)((u8)(*source >>  0) & 0xFF);
            
            f32 dest_alpha = (f32)((u8)(*dest >> 24) & 0xFF);
            f32 dest_red   = (f32)((u8)(*dest >> 16) & 0xFF);
            f32 dest_green = (f32)((u8)(*dest >>  8) & 0xFF);
            f32 dest_blue  = (f32)((u8)(*dest >>  0) & 0xFF);
            
            // @note linear blending
            f32 red   = (1.0f - alpha) * dest_red   + alpha * source_red;
            f32 green = (1.0f - alpha) * dest_green + alpha * source_green;
            f32 blue  = (1.0f - alpha) * dest_blue  + alpha * source_blue;
            
            *dest = (((u32)(dest_alpha)   << 24) |
                     ((u32)(red   + 0.5f) << 16) |
                     ((u32)(green + 0.5f) << 8)  |
                     ((u32)(blue  + 0.5f) << 0));
            
            ++dest;
            ++source;
        }
        
        source_row -= bitmap->width;
        dest_row += buffer->pitch;
    }
}

internal void
render_string(Game_Offscreen_Buffer *buffer, Game_State *game_state, f32 float_x, f32 float_y, char *str) {
    for (int i = 0; i < string_length(str); ++i) {
        render_char(buffer, game_state, float_x + (f32)(i * 11), float_y, str[i]);
    }
}

struct Button {
    Vector2 v_min;
    Vector2 v_max;
};

internal b32
is_button_pressed(Game_Input *input, Button *button) {
    b32 is_pressed = false;
    
    if (!input->mouse_buttons[0].ended_down)  {
        return false;
    }
    
    b32 is_inside_x = ((f32)input->mouse_x >= button->v_min.x && (f32)input->mouse_x <= button->v_max.x);
    b32 is_inside_y = ((f32)input->mouse_y >= button->v_min.y && (f32)input->mouse_y <= button->v_max.y);
    if (is_inside_x && is_inside_y) {
        is_pressed = true;
    }
    
    return is_pressed;
}

// @todo Ability to set width/height of the button
internal Button
render_button(Game_Offscreen_Buffer *buffer, Game_Input *input, Game_State *game_state, f32 float_x, f32 float_y, char *str, b32 center = true) {
    f32 string_width  = (f32)string_length(str) * 11.0f;
    f32 string_height = 17.0f;
    f32 padding = 5.0f;
    
    Vector2 v_min = {
        float_x,
        float_y
    };
    if (center) {
        v_min.x -= (string_width /2.0f) + padding;
        v_min.y -= (string_height/2.0f) + padding;
    }
    Vector2 v_max = {
        v_min.x + string_width  + 2.0f*padding,
        v_min.y + string_height + 2.0f*padding
    };
    
    Button button = {
        v_min,
        v_max
    };
    
    // @copynpaste @refactor is_inside_rect function or something
    b32 is_inside_x = ((f32)input->mouse_x >= button.v_min.x && (f32)input->mouse_x <= button.v_max.x);
    b32 is_inside_y = ((f32)input->mouse_y >= button.v_min.y && (f32)input->mouse_y <= button.v_max.y);
    if (is_inside_x && is_inside_y) {
        render_rectangle(buffer, v_min, v_max, 0.5f, 0.5f, 0.5f);
        f32 pad = padding - 2.0f;
        render_rectangle(buffer,
                         { v_min.x + pad, v_min.y + pad },
                         { v_max.x - pad, v_max.y - pad },
                         1.0f, 1.0f, 1.0f);
    }
    else {
        render_rectangle(buffer, v_min, v_max, 1.0f, 1.0f, 1.0f);
    }
    
    render_string(buffer, game_state, v_min.x + padding, v_min.y + padding, str);
    
    return button;
}

internal void
clear_buffer(Game_Offscreen_Buffer *buffer) {
    u8 *row = (u8 *)buffer->memory;
    for (int y = 0; y < buffer->height; ++y) {
        u32 *pixel = (u32 *)row;
        for (int x = 0; x < buffer->width; ++x) {
            *pixel++ = 0;
        }
        
        row += buffer->pitch;
    }
}

extern "C" GAME_UPDATE_AND_RENDER_SIG(game_update_and_render) {
    assert((&input->controllers[0]._terminator_ - &input->controllers[0].buttons[0]) == array_count(input->controllers[0].buttons));
    assert(sizeof(Game_State) <= memory->permanent_storage_size);
    
    Game_State *game_state = (Game_State *)memory->permanent_storage;
    if (!memory->is_initialized) {
        defer {
            memory->is_initialized = true;
        };
        
        *game_state = {};
        initialize_memory_arena(&game_state->game_arena,
                                memory->permanent_storage_size - sizeof(Game_State),
                                (u8 *)memory->permanent_storage + sizeof(Game_State));
        
        game_state->game = push_struct(&game_state->game_arena, Game);
        Game *game = game_state->game;
        *game = {};
        
        // @note load bitmaps
        game->bmp_background = load_bitmap(memory->platform_read_entire_file, "../run_tree/data/sprites/background.bmp");
        game->player.bmp = load_bitmap(memory->platform_read_entire_file, "../run_tree/data/sprites/pacman_closed.bmp");
        
        game_state->bmp_font = load_bitmap(memory->platform_read_entire_file, "../run_tree/data/fonts/bmp_font.bmp");
    }
    Game *game = game_state->game;
    
    // @note get active input controller
    for (u32 controller_index = 0; controller_index < array_count(input->controllers); ++controller_index) {
        Game_Controller_Input *controller = get_controller(input, controller_index);
        if (controller->start.ended_down)  {
            game_state->active_controller_index = controller_index;
        }
    }
    
    //
    // @note titlescreen
    //
    if (game_state->show_titlescreen) {
#if 0
        Vector2 v_min = {
            (f32)input->mouse_x,
            (f32)input->mouse_y
        };
        Vector2 v_max = {
            v_min.x + 10.0f,
            v_min.y + 10.0f
        };
        render_rectangle(buffer, v_min, v_max, 1.0f, 0.0f, 0.0f);
#endif
        
        // Start game
        Button button_start_game = render_button(buffer, input, game_state,
                                                 (f32)buffer->width/2.0f, (f32)buffer->height/2.0f - 21.0f,
                                                 "Start Game");
        if (is_button_pressed(input, &button_start_game)) {
            game_state->show_titlescreen = false;
        }
        
        // Exit game
        Button button_exit_game = render_button(buffer, input, game_state,
                                                (f32)buffer->width/2.0f, (f32)buffer->height/2.0f + 21.0f,
                                                "Exit Game");
        if (is_button_pressed(input, &button_exit_game)) {
            input->request_quit = true;
        }
        
        return;
    }
    
    //
    // @note do input
    //
    {
        Game_Controller_Input *controller = get_controller(input, game_state->active_controller_index);
        
        if (controller->move_up.ended_down) {
            // game->scrolly.next_input_direction = Direction::UP;
        }
        else if (controller->move_left.ended_down) {
            game->player.pos_x -= 5.f;
        }
        else if (controller->move_down.ended_down) {
            // game->scrolly.next_input_direction = Direction::DOWN;
        }
        else if (controller->move_right.ended_down) {
            game->player.pos_x += 5.f;
        }
        else if (controller->action_right.ended_down || controller->action_down.ended_down) {
        }
    }
    
    //
    // @note simulate
    //
    
    // @note update player positions
    Player *player = &game->player;
    
    
    //
    // @note render
    //
    clear_buffer(buffer);
    
    s32 back_offset_x = (s32)game->player.pos_x % game->bmp_background.width;
    s32 back_offset_y = 0;
    render_background(buffer, &game->bmp_background, back_offset_x, back_offset_y);
    render_bitmap(buffer, &game->player.bmp,
                  (f32)buffer->width*0.5f, (f32)buffer->height*0.5f);
    
    // @note render ui
    render_string(buffer, game_state, 5, 5, "Hello Sailor");
}
