// @todo Bitmap argb is wrong?
// @todo Support controllers

#include <cstddef>
#include "iml_general.h"
#include "iml_types.h"
#include "scrolly_platform.h"
#include "scrolly_math.h"

#include <SDL.h>
#include <sys/mman.h>
#include <x86intrin.h>

#include "sdl2_scrolly.h"

#define TILE_SIZE 32
#define WIDTH (21  * TILE_SIZE)
#define HEIGHT (27 * TILE_SIZE)

#define WINDOW_WIDTH  (WIDTH) // 800
#define WINDOW_HEIGHT (HEIGHT) // 600


global b32 global_running;
global SDL2_Offscreen_Buffer global_backbuffer;
global u64 global_performance_count_frequency;


PLATFORM_FREE_FILE_MEMORY_SIG(platform_free_file_memory) {
    if (!memory)  return;
    free(memory);
}

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
PLATFORM_READ_ENTIRE_FILE_SIG(platform_read_entire_file) {
    Read_File_Result result = {};
    int file_handle = open(filename, O_RDONLY);
    if (file_handle == -1)  return {};
    defer { close(file_handle); };
    
    struct stat file_status;
    if (fstat(file_handle, &file_status) == -1)  return {};
    result.memory_size = safe_truncate_u64_to_u32(file_status.st_size);
    
    result.memory = malloc(result.memory_size);
    if (!result.memory)  return {};
    
    u32 bytes_to_read = result.memory_size;
    u8 *next_byte_location = (u8 *)result.memory;
    while (bytes_to_read) {
        u32 bytes_read = read(file_handle, next_byte_location, bytes_to_read);
        if (bytes_read == -1) {
            free(result.memory);
            return {};
        }
        bytes_to_read -= bytes_read;
        next_byte_location += bytes_read;
    }
    
    return result;
}

internal SDL2_Game_Code
sdl2_load_game_code(char *source_dll_name) {
    SDL2_Game_Code game = {};
    
    game.game_code_dll = SDL_LoadObject(source_dll_name); // dlopen()
    if (game.game_code_dll) {
        game.update_and_render = (Game_Update_And_Render_Function *)
            SDL_LoadFunction(game.game_code_dll, "game_update_and_render"); // dlsym()
        
        game.is_valid = (game.update_and_render != nullptr);
    }
    
    if (!game.is_valid) {
        game.update_and_render = null;
    }
    
    return game;
}

internal void
sdl2_unload_game_code(SDL2_Game_Code *game) {
    if (game->game_code_dll) {
        SDL_UnloadObject(game->game_code_dll); // dlclose()
        game->game_code_dll = null;
    }
    game->is_valid = false;
    game->update_and_render = null;
}

internal SDL2_Window_Dimension
sdl2_get_window_dimension(SDL_Window *window) {
    SDL2_Window_Dimension result;
    SDL_GetWindowSize(window, &result.width, &result.height);
    return result;
}

internal void
sdl2_display_buffer_in_window(SDL2_Offscreen_Buffer *buffer, SDL_Renderer *renderer,
                              int window_width, int window_height) {
#if 0
    SDL_UpdateTexture(buffer->texture, 0,
                      buffer->memory,
                      buffer->pitch);
    SDL_RenderCopy(renderer, buffer->texture, 0, 0);
    SDL_RenderPresent(renderer);
#else
    SDL_Rect target_rect = {};
    
    Rectangle_s32 draw_region = aspect_ratio_fit(WIDTH, HEIGHT, window_width, window_height);
    
    target_rect.x = draw_region.min_x;
    target_rect.y = draw_region.min_y;
    target_rect.w = draw_region.max_x - draw_region.min_x;
    target_rect.h = draw_region.max_y - draw_region.min_y;
    
    SDL_UpdateTexture(buffer->texture, 0,
                      buffer->memory,
                      buffer->pitch);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, buffer->texture, 0, &target_rect);
    SDL_RenderPresent(renderer);
#endif
}

internal void
sdl2_resize_dib_section(SDL_Renderer *renderer,
                        SDL2_Offscreen_Buffer *buffer, int width, int height) {
    if (buffer->memory)  {
        munmap(buffer->memory,
               buffer->width * buffer->height * buffer->bytes_per_pixel);
    }
    if (buffer->texture)  {
        SDL_DestroyTexture(buffer->texture);
    }
    buffer->texture = SDL_CreateTexture(renderer,
                                        SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        width, height);
    buffer->bytes_per_pixel = sizeof(u32);
    buffer->memory = (u32 *)mmap(0,
                                 width * height * buffer->bytes_per_pixel,
                                 PROT_READ | PROT_WRITE,
                                 MAP_ANONYMOUS | MAP_PRIVATE,
                                 -1, 0);
    buffer->width  = width;
    buffer->height = height;
    buffer->pitch  = width * buffer->bytes_per_pixel;
}

internal void
render_weird_gradient(SDL2_Offscreen_Buffer *buffer, int blue_offset = 0, int green_offset = 0) {
    u8 *row = (u8 *)buffer->memory;
    for (int y = 0; y < buffer->height; ++y) {
        u32 *pixel = (u32 *)row;
        for (int x = 0; x < buffer->width; ++x) {
            u8 alpha = 0;
            u8 red = 0;
            u8 green = (y + green_offset);
            u8 blue = (x + blue_offset);
            
            u32 color = ((alpha << 24) |
                         (red   << 16) |
                         (green << 8)  |
                         (blue  << 0));
            
            *pixel++ = color;
        }
        
        row += buffer->pitch;
    }
}

internal void
sdl2_process_key_press(Game_Button_State *new_state, b32 is_down) {
    // assert(new_state->ended_down != is_down);
    
    if (new_state->ended_down == is_down)  return;
    new_state->ended_down = is_down;
    ++new_state->half_transition_count;
}

internal void
sdl2_process_pending_events(Game_Input *new_input, Rectangle_s32 draw_region) {
    Game_Controller_Input *new_keyboard_controller = &new_input->controllers[0];
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT: {
                global_running = false;
            } break;
            
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                SDL_Keycode key_code = event.key.keysym.sym;
                b32 is_down = (event.key.state == SDL_PRESSED);
                b32 was_down = false;
                if (event.key.state == SDL_RELEASED)  was_down = true;
                else if (event.key.repeat != 0)  was_down = true;
                
                if (event.key.repeat == 0) {
                    if (key_code == SDLK_w) {
                        sdl2_process_key_press(&new_keyboard_controller->move_up, is_down);
                    }
                    else if (key_code == SDLK_a) {
                        sdl2_process_key_press(&new_keyboard_controller->move_left, is_down);
                    }
                    else if (key_code == SDLK_s) {
                        sdl2_process_key_press(&new_keyboard_controller->move_down, is_down);
                    }
                    else if (key_code == SDLK_d) {
                        sdl2_process_key_press(&new_keyboard_controller->move_right, is_down);
                    }
                    else if (key_code == SDLK_q) {
                        sdl2_process_key_press(&new_keyboard_controller->left_shoulder, is_down);
                    }
                    else if (key_code == SDLK_e) {
                        sdl2_process_key_press(&new_keyboard_controller->right_shoulder, is_down);
                    }
                    else if (key_code == SDLK_UP) {
                        sdl2_process_key_press(&new_keyboard_controller->move_up, is_down);
                    }
                    else if (key_code == SDLK_LEFT) {
                        sdl2_process_key_press(&new_keyboard_controller->move_left, is_down);
                    }
                    else if (key_code == SDLK_DOWN) {
                        sdl2_process_key_press(&new_keyboard_controller->move_down, is_down);
                    }
                    else if (key_code == SDLK_RIGHT) {
                        sdl2_process_key_press(&new_keyboard_controller->move_right, is_down);
                    }
                    else if (key_code == 'K')  {
                        sdl2_process_key_press(&new_keyboard_controller->action_down, is_down);
                    }
                    else if (key_code == 'J')  {
                        sdl2_process_key_press(&new_keyboard_controller->action_right, is_down);
                    }
                    else if (key_code == SDLK_RETURN) {
                        sdl2_process_key_press(&new_keyboard_controller->start, is_down);
                    }
                    else if (key_code == SDLK_BACKSPACE) {
                        sdl2_process_key_press(&new_keyboard_controller->back, is_down);
                    }
                    else if (key_code == SDLK_ESCAPE) {
                        if (is_down) {
                            global_running = false;
                        }
                    }
                }
            } break;
            
#if 0
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                b32 is_down = (event.key.state == SDL_PRESSED);
                b32 was_down = false;
                if (event.key.state == SDL_RELEASED)  was_down = true;
                else if (event.key.repeat != 0)  was_down = true;
                
                if (event.key.repeat == 0) {
                    switch (event.button.button) {
                        case SDL_BUTTON_LEFT: {
                            sdl2_process_key_press(&new_input->mouse_buttons[0], is_down);
                        } break;
                        case SDL_BUTTON_MIDDLE: {
                            sdl2_process_key_press(&new_input->mouse_buttons[1], is_down);
                        } break;
                        case SDL_BUTTON_RIGHT: {
                            sdl2_process_key_press(&new_input->mouse_buttons[2], is_down);
                        } break;
                        case SDL_BUTTON_X1: {
                            sdl2_process_key_press(&new_input->mouse_buttons[3], is_down);
                        } break;
                        case SDL_BUTTON_X2: {
                            sdl2_process_key_press(&new_input->mouse_buttons[4], is_down);
                        } break;
                    }
                }
            } break;
            case SDL_MOUSEMOTION: {
                f32 mouse_x = (f32)event.motion.x;
                f32 mouse_y = (f32)event.motion.y;
                
                new_input->mouse_x = clamp_01_map_to_range((f32)draw_region.min_x, mouse_x, (f32)draw_region.max_x) * WIDTH;
                new_input->mouse_y = clamp_01_map_to_range((f32)draw_region.min_y, mouse_y, (f32)draw_region.max_y) * HEIGHT;
                new_input->mouse_z = 0; // @todo Support mousewheel?
            } break;
#endif
            
            case SDL_WINDOWEVENT: {
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_SIZE_CHANGED: {
#if 0
                        SDL_Window *window = SDL_GetWindowFromID(event.window.windowID);
                        SDL_Renderer *renderer = SDL_GetRenderer(window);
                        sdl2_resize_dib_section(renderer, &global_backbuffer, event.window.data1, event.window.data2);
#endif
                    } break;
                    case SDL_WINDOWEVENT_RESIZED: {
                    } break;
                    
                    case SDL_WINDOWEVENT_FOCUS_GAINED: {
                    } break;
                }
            } break;
        }
    }
}

internal f32
sdl2_get_seconds_elapsed(u64 start, u64 end) {
    f32 result = ((f32)(end - start) / (f32)global_performance_count_frequency);
    return result;
}

internal void
sdl2_get_exe_filename(SDL2_State *state) {
    char *executable_path = SDL_GetBasePath();
    state->executable_path = executable_path;
}

internal void
sdl2_build_executable_path_filename(SDL2_State *state, char *filename,
                                    char *output, int output_length) {
    concat_strings(state->executable_path,
                   string_length(state->executable_path),
                   filename, string_length(filename),
                   output, output_length);
}

int main(int argc, char *argv[]) {
    SDL2_State sdl2_state = {};
    sdl2_get_exe_filename(&sdl2_state);
    
    char source_game_dll_full_path[SDL2_STATE_FILENAME_LENGTH];
    sdl2_build_executable_path_filename(&sdl2_state, "scrolly.dll",
                                        source_game_dll_full_path,
                                        sizeof(source_game_dll_full_path));
    
    global_performance_count_frequency = SDL_GetPerformanceFrequency();
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)  {
        return -1;
    }
    
    SDL_Window *window = SDL_CreateWindow("scrolly",
                                          SDL_WINDOWPOS_UNDEFINED,  SDL_WINDOWPOS_UNDEFINED,
                                          WINDOW_WIDTH, WINDOW_HEIGHT,
                                          SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    
    sdl2_resize_dib_section(renderer,
                            &global_backbuffer, WIDTH, HEIGHT);
    
    
    int monitor_refresh_hz = 60;
    SDL_DisplayMode display_mode;
    int display_index = SDL_GetWindowDisplayIndex(window);
    if (SDL_GetDesktopDisplayMode(display_index, &display_mode) == 0 &&
        display_mode.refresh_rate != 0) {
        monitor_refresh_hz = display_mode.refresh_rate;
    }
    f32 game_update_hz = (monitor_refresh_hz / 2.0f);
    f32 target_seconds_per_frame = 1.0f / (f32)game_update_hz;
    f32 dt = target_seconds_per_frame;
    
#if BUILD_INTERNAL
    void *base_address = (void *)terabytes_to_bytes(2);
#else
    void *base_address = (void *)(0);
#endif
    
    Game_Memory game_memory = {};
    game_memory.platform_free_file_memory = platform_free_file_memory;
    game_memory.platform_read_entire_file = platform_read_entire_file;
    
    game_memory.permanent_storage_size = megabytes_to_bytes(64);
    sdl2_state.game_memory_total_size = game_memory.permanent_storage_size;
    sdl2_state.game_memory_block = mmap(base_address, game_memory.permanent_storage_size,
                                        PROT_READ | PROT_WRITE,
                                        MAP_ANON | MAP_PRIVATE,
                                        -1, 0);
    game_memory.permanent_storage = sdl2_state.game_memory_block;
    
    SDL2_Game_Code game = sdl2_load_game_code(source_game_dll_full_path);
    if (!game.is_valid) {
        return -1;
    }
    
    Game_Input input[2] = {};
    Game_Input *new_input = &input[0];
    Game_Input *old_input = &input[1];
    
    u64 last_counter = SDL_GetPerformanceCounter();
    u64 last_cycle_count = _rdtsc();
    global_running = true;
    while (global_running) {
        new_input->dt = target_seconds_per_frame;
        SDL2_Window_Dimension dimension = sdl2_get_window_dimension(window);
        Rectangle_s32 draw_region = aspect_ratio_fit(WIDTH, HEIGHT,
                                                     dimension.width, dimension.height);
        
        //
        // @note handle input
        //
        Game_Controller_Input *old_keyboard_controller = &old_input->controllers[0];
        Game_Controller_Input *new_keyboard_controller = &new_input->controllers[0];
        *new_keyboard_controller = {};
        for (int button_index = 0; button_index < array_count(new_keyboard_controller->buttons); ++button_index) {
            new_keyboard_controller->buttons[button_index].ended_down = old_keyboard_controller->buttons[button_index].ended_down;
        }
        
        sdl2_process_pending_events(new_input, draw_region);
        if (!global_running)  break;
        
        // @note handle mouse
        int int_mouse_x;
        int int_mouse_y;
        SDL_PumpEvents();
        u32 mouse_state = SDL_GetMouseState(&int_mouse_x, &int_mouse_y);
        
        f32 mouse_x = (f32)int_mouse_x;
        f32 mouse_y = (f32)int_mouse_y;
        new_input->mouse_x = clamp_01_map_to_range((f32)draw_region.min_x, mouse_x, (f32)draw_region.max_x) * WIDTH;
        new_input->mouse_y = clamp_01_map_to_range((f32)draw_region.min_y, mouse_y, (f32)draw_region.max_y) * HEIGHT;
        new_input->mouse_z = 0; // @todo Support mousewheel?
        
        sdl2_process_key_press(&new_input->mouse_buttons[0],
                               mouse_state & SDL_BUTTON(SDL_BUTTON_LEFT));
        sdl2_process_key_press(&new_input->mouse_buttons[1],
                               mouse_state & SDL_BUTTON(SDL_BUTTON_MIDDLE));
        sdl2_process_key_press(&new_input->mouse_buttons[2],
                               mouse_state & SDL_BUTTON(SDL_BUTTON_RIGHT));
        sdl2_process_key_press(&new_input->mouse_buttons[3],
                               mouse_state & SDL_BUTTON(SDL_BUTTON_X1));
        sdl2_process_key_press(&new_input->mouse_buttons[4],
                               mouse_state & SDL_BUTTON(SDL_BUTTON_X2));
        
        //
        // @note render
        //
        Game_Offscreen_Buffer buffer = {};
        buffer.memory = global_backbuffer.memory;
        buffer.width  = global_backbuffer.width;
        buffer.height = global_backbuffer.height;
        buffer.pitch  = global_backbuffer.pitch;
        buffer.bytes_per_pixel = global_backbuffer.bytes_per_pixel;
        if (game.update_and_render)  game.update_and_render(&game_memory, new_input, &buffer);
        if (new_input->request_quit)  {
            global_running = false;
        }
        
        //
        // @note frame rate
        //
        if (sdl2_get_seconds_elapsed(last_counter, SDL_GetPerformanceCounter()) < target_seconds_per_frame)  {
            u32 sleep_ms = ((target_seconds_per_frame - sdl2_get_seconds_elapsed(last_counter, SDL_GetPerformanceCounter())) * 1000) - 1;
            SDL_Delay(sleep_ms);
            if (sdl2_get_seconds_elapsed(last_counter, SDL_GetPerformanceCounter()) > target_seconds_per_frame)  {
                // @todo LOG MISSED SLEEP HERE
            }
            while (sdl2_get_seconds_elapsed(last_counter, SDL_GetPerformanceCounter()) < target_seconds_per_frame) {
                // Yeah spin waiting ...
            }
        }
        
        u64 end_counter = SDL_GetPerformanceCounter();
        f64 ms_per_frame = 1000.0f * sdl2_get_seconds_elapsed(last_counter, end_counter);
        
        //
        // @note display buffer
        //
        sdl2_display_buffer_in_window(&global_backbuffer, renderer, dimension.width, dimension.height);
        
        
        Game_Input *temp_input = new_input;
        new_input = old_input;
        old_input = temp_input;
        
        
        // cycles, ... stuff
        
        // f64 fps = (f64)global_performance_count_frequency / (f64)counter_elapsed;
        
        u64 end_cycle_count = _rdtsc();
        u64 cycles_elapsed = end_cycle_count - last_cycle_count;
        f64 mcpf = ((f64)cycles_elapsed / (1000.0f * 1000.0f));
        
        printf("%.02fms/f, %.02fmc/f\n", ms_per_frame, mcpf);
        
        last_counter = end_counter;
        last_cycle_count = end_cycle_count;
    }
    
    return 0;
}