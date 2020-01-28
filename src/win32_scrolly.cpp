// @todo improve input system

// @todo mouse position is still not 100% right, when going further away from the center I think ?


#include "iml_general.h"
#include "scrolly_platform.h"
#include "scrolly_math.h"
#include "iml_types.h"


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h> // @note excluded in lean and mean, used for timeBeginPeriod to set scheduler granularity

#include <xinput.h>

#include <stdio.h>

#include "win32_scrolly.h"

#define HEIGHT (288)
#define WIDTH (HEIGHT / 3 * 4)

#define WINDOW_HEIGHT (HEIGHT * 4)
#define WINDOW_WIDTH  (WIDTH * 4)


global b32 global_running;
#if BUILD_INTERNAL
global b32 global_pause;
#endif
global Win32_Offscreen_Buffer global_backbuffer;
global s64 global_performance_count_frequency;
global WINDOWPLACEMENT global_window_position = { sizeof(global_window_position) };


internal void
win32_build_exe_path_filename(Win32_State *state, char *filename,
                              char *output, int output_length) {
    concat_strings(state->exe_filename, state->exe_filename_one_past_last_slash - state->exe_filename,
                   filename, string_length(filename),
                   output, output_length);
}

PLATFORM_FREE_FILE_MEMORY_SIG(platform_free_file_memory) {
    if (!memory)  return;
    VirtualFree(memory, null, MEM_RELEASE);
}

PLATFORM_READ_ENTIRE_FILE_SIG(platform_read_entire_file) {
    Read_File_Result result = {};
    
    HANDLE file_handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, null, OPEN_EXISTING, null, null);
    defer { CloseHandle(file_handle); };
    if (file_handle == INVALID_HANDLE_VALUE)  return {};
    
    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file_handle, &file_size))  return {};
    result.memory_size = safe_truncate_u64_to_u32(file_size.QuadPart);
    
    result.memory = VirtualAlloc(null, result.memory_size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!result.memory)  return {};
    
    DWORD bytes_read;
    if (!ReadFile(file_handle, result.memory, result.memory_size, &bytes_read, null) ||
        (result.memory_size != bytes_read)) {
        platform_free_file_memory(result.memory);
        return {};
    }
    
    return result;
}

// @note xinput_get_state
#define XINPUT_GET_STATE_SIG(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef XINPUT_GET_STATE_SIG(XInput_Get_State_Sig);
XINPUT_GET_STATE_SIG(xinput_get_state_stub) {
    return ERROR_DEVICE_NOT_CONNECTED;
}
global XInput_Get_State_Sig *xinput_get_state_ = xinput_get_state_stub;
#define xinput_get_state xinput_get_state_

// @note xinput_set_state
#define XINPUT_SET_STATE_SIG(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef XINPUT_SET_STATE_SIG(XInput_Set_State_Sig);
XINPUT_SET_STATE_SIG(xinput_set_state_stub) {
    return ERROR_DEVICE_NOT_CONNECTED;
}
global XInput_Set_State_Sig *xinput_set_state_ = xinput_set_state_stub;
#define xinput_set_state xinput_set_state_

internal void
win32_load_xinput() {
    HMODULE xinput_library = LoadLibraryA("xinput1_4.dll");
    if(!xinput_library) {
        xinput_library = LoadLibraryA("xinput9_1_0.dll");
    }
    if(!xinput_library) {
        xinput_library = LoadLibraryA("xinput1_3.dll");
    }
    
    if (!xinput_library)  return;
    
    xinput_get_state = (XInput_Get_State_Sig *)GetProcAddress(xinput_library, "XInputGetState");
    if (!xinput_get_state) {
        xinput_get_state = xinput_get_state_stub;
    }
    
    xinput_set_state = (XInput_Set_State_Sig *)GetProcAddress(xinput_library, "XInputSetState");
    if (!xinput_set_state)  {
        xinput_set_state = xinput_set_state_stub;
    }
}

internal Win32_Game_Code
win32_load_game_code(char *source_dll_name) {
    Win32_Game_Code game = {};
    
    game.game_code_dll = LoadLibraryA(source_dll_name);
    if (game.game_code_dll) {
        game.update_and_render = (Game_Update_And_Render_Function *)GetProcAddress(game.game_code_dll, "game_update_and_render");
        
        game.is_valid = (game.update_and_render != nullptr);
    }
    
    if (!game.is_valid) {
        game.update_and_render = null;
    }
    
    return game;
}

internal void
win32_unload_game_code(Win32_Game_Code *game) {
    if (game->game_code_dll) {
        FreeLibrary(game->game_code_dll);
        game->game_code_dll = null;
    }
    game->is_valid = false;
    game->update_and_render = null;
}

internal void
win32_resize_dib_section(Win32_Offscreen_Buffer *buffer, int width, int height) {
    if (buffer->memory) {
        VirtualFree(buffer->memory, 0, MEM_RELEASE);
    }
    
    buffer->width  = width;
    buffer->height = height;
    
    buffer->info.bmiHeader.biSize = sizeof(buffer->info.bmiHeader);
    buffer->info.bmiHeader.biWidth = buffer->width;
    buffer->info.bmiHeader.biHeight = -buffer->height;
    buffer->info.bmiHeader.biPlanes = 1;
    buffer->info.bmiHeader.biBitCount = 32;
    buffer->info.bmiHeader.biCompression = BI_RGB;
    
    int bytes_per_pixel = 4;
    int bitmap_memory_size = (buffer->width * buffer->height) * bytes_per_pixel;
    buffer->memory = VirtualAlloc(0, bitmap_memory_size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    buffer->pitch = buffer->width * bytes_per_pixel;
    buffer->bytes_per_pixel = bytes_per_pixel;
}

internal Win32_Window_Dimension
win32_get_window_dimension(HWND window) {
    Win32_Window_Dimension result;
    
    RECT client_rect;
    GetClientRect(window, &client_rect);
    result.width = client_rect.right - client_rect.left;
    result.height = client_rect.bottom - client_rect.top;
    
    return result;
}

internal void
win32_display_buffer_in_window(Win32_Offscreen_Buffer *buffer, HDC device_context, int window_width, int window_height) {
    // @todo better scaling, centering, black bars, ...
    
#if 0
    StretchDIBits(device_context,
                  0, 0, window_width, window_height,
                  0, 0, buffer->width, buffer->height,
                  buffer->memory, &buffer->info,
                  DIB_RGB_COLORS, SRCCOPY);
    
#else
    
    int buffer_width = WIDTH;
    int buffer_height = HEIGHT;
    
    f32 height_scale = (f32)window_height / (f32)buffer_height;
    int new_width = (int)((f32)buffer_width * height_scale);
    int offset_x = (window_width - new_width) / 2;
    if (new_width <= buffer_width)  {
        offset_x = 0;
        new_width = window_width;
    }
    else {
        PatBlt(device_context, 0, 0, offset_x-2, window_height, BLACKNESS);
        PatBlt(device_context, offset_x-2, 0, offset_x, window_height,  WHITENESS);
        PatBlt(device_context, offset_x+new_width, 0, offset_x+buffer_width+2, window_height, WHITENESS);
        PatBlt(device_context, offset_x+new_width+2, 0, window_width, window_height, BLACKNESS);
    }
    
    StretchDIBits(device_context,
                  offset_x, 0, new_width, window_height,
                  0, 0, buffer->width, buffer->height,
                  buffer->memory, &buffer->info,
                  DIB_RGB_COLORS, SRCCOPY);
#endif
}

internal void
toggle_fullscreen(HWND window) {
    // @note: This follows Raymond Chen's prescription for fullscreen toggling, see:
    //        https://devblogs.microsoft.com/oldnewthing/20100412-00/?p=14353
    
    DWORD style = GetWindowLong(window, GWL_STYLE);
    if (style & WS_OVERLAPPEDWINDOW) {
        MONITORINFO monitor_info = { sizeof(monitor_info) };
        if (GetWindowPlacement(window, &global_window_position) &&
            GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor_info)) {
            SetWindowLong(window, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(window, HWND_TOP,
                         monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
                         monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                         monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    }
    else {
        SetWindowLong(window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(window, &global_window_position);
        SetWindowPos(window, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

LRESULT CALLBACK
win32_main_window_callback(HWND window,
                           UINT message,
                           WPARAM wparam,
                           LPARAM lparam) {
    LRESULT result = 0;
    
    switch (message) {
        case WM_ACTIVATEAPP: {
            OutputDebugStringA("WM_ACTIVATEAPP\n");
        } break;
        
        case WM_DESTROY:
        case WM_CLOSE: {
            global_running = false;
        } break;
        
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP: {
            assert(!"Keyboard input came in through a non-dispatch message!");
        } break;
        
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC device_context = BeginPaint(window, &paint);
            int x = paint.rcPaint.left;
            int y = paint.rcPaint.top;
            int width  = paint.rcPaint.right  - paint.rcPaint.left;
            int height = paint.rcPaint.bottom - paint.rcPaint.top;
            Win32_Window_Dimension dimension = win32_get_window_dimension(window);
            win32_display_buffer_in_window(&global_backbuffer, device_context, dimension.width, dimension.height);
            EndPaint(window, &paint);
        } break;
        
        default: {
            result = DefWindowProcA(window, message, wparam, lparam);
        } break;
    }
    
    return result;
}

internal void
win32_process_keyboard_message(Game_Button_State *new_state, b32 is_down) {
    if (new_state->ended_down == is_down)  return;
    new_state->ended_down = is_down;
    ++new_state->half_transition_count;
}

internal void
win32_process_xinput_digital_button(DWORD xinput_button_state,
                                    Game_Button_State *old_state, DWORD button_bit,
                                    Game_Button_State *new_state) {
    b32 is_down = ((xinput_button_state & button_bit) == button_bit);
    new_state->ended_down = false;
    if (!is_down)  {
        new_state->allow_press = true;
        return;
    }
    else {
        new_state->allow_press = false;
        
        if (old_state->allow_press)  {
            new_state->ended_down = is_down;
            new_state->half_transition_count = (old_state->ended_down != new_state->ended_down) ? 1 : 0;
            return;
        }
    }
}

internal void
win32_process_pending_messages(Game_Controller_Input *keyboard_controller) {
    MSG message;
    while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
        switch (message.message) {
            case WM_QUIT: {
                global_running = false;
            } break;
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_KEYDOWN:
            case WM_KEYUP: {
                u32 vk_code = (u32)message.wParam;
                
                // @note Since we are comparing was_down to is_down,
                // we MUST use == and != to convert these bit tests to actual 0 or 1 values.
                b32 was_down = ((message.lParam & (1 << 30)) != 0);
                b32 is_down = ((message.lParam & (1 << 31)) == 0);
                if (was_down != is_down) {
                    if (vk_code == 'W') {
                        win32_process_keyboard_message(&keyboard_controller->move_up, is_down);
                    }
                    else if (vk_code == 'A') {
                        win32_process_keyboard_message(&keyboard_controller->move_left, is_down);
                    }
                    else if (vk_code == 'S') {
                        win32_process_keyboard_message(&keyboard_controller->move_down, is_down);
                    }
                    else if (vk_code == 'D') {
                        win32_process_keyboard_message(&keyboard_controller->move_right, is_down);
                    }
                    else if (vk_code == 'Q') {
                        win32_process_keyboard_message(&keyboard_controller->left_shoulder, is_down);
                    }
                    else if (vk_code == 'E') {
                        win32_process_keyboard_message(&keyboard_controller->right_shoulder, is_down);
                    }
                    else if (vk_code == VK_UP) {
                        win32_process_keyboard_message(&keyboard_controller->action_up, is_down);
                    }
                    else if (vk_code == VK_LEFT) {
                        win32_process_keyboard_message(&keyboard_controller->action_left, is_down);
                    }
                    else if ((vk_code == VK_DOWN) || (vk_code == 'K')) {
                        win32_process_keyboard_message(&keyboard_controller->action_down, is_down);
                    }
                    else if ((vk_code == VK_RIGHT) || (vk_code == 'J')) {
                        win32_process_keyboard_message(&keyboard_controller->action_right, is_down);
                    }
                    else if (vk_code == VK_RETURN) {
                        win32_process_keyboard_message(&keyboard_controller->start, is_down);
                    }
                    else if (vk_code == VK_BACK) {
                        win32_process_keyboard_message(&keyboard_controller->back, is_down);
                    }
                    if (vk_code == VK_ESCAPE) {
                        global_running = false;
                    }
                    
#if BUILD_INTERNAL
                    else if (vk_code == 'P') {
                        if (is_down) {
                            global_pause = !global_pause;
                        }
                    }
                    
                    if (is_down) {
                        b32 alt_key_was_down = (message.lParam & (1 << 29));
                        if ((vk_code == VK_F4) && alt_key_was_down) {
                            global_running = false;
                        }
                        if ((vk_code == VK_RETURN) && alt_key_was_down)  {
                            if (message.hwnd)  {
                                toggle_fullscreen(message.hwnd);
                            }
                        }
                    }
#endif
                }
            } break;
            
            default: {
                TranslateMessage(&message);
                DispatchMessageA(&message);
            } break;
        }
    }
}

inline LARGE_INTEGER
win32_get_wall_clock() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter;
}

inline f32
win32_get_seconds_elapsed(LARGE_INTEGER start, LARGE_INTEGER end) {
    f32 result = ((f32)(end.QuadPart - start.QuadPart) / (f32)global_performance_count_frequency);
    return result;
}

internal void
win32_get_exe_filename(Win32_State *state) {
    DWORD size_of_filename = GetModuleFileNameA(0, state->exe_filename, sizeof(state->exe_filename));
    state->exe_filename_one_past_last_slash = state->exe_filename;
    for (char *scan = state->exe_filename; *scan; ++scan) {
        if (*scan == '\\') {
            state->exe_filename_one_past_last_slash = scan + 1;
        }
    }
}

int CALLBACK
WinMain(HINSTANCE instance,
        HINSTANCE prev_instance,
        LPSTR     cmd_line,
        int       show_cmd) {
    Win32_State win32_state = {};
    win32_get_exe_filename(&win32_state);
    
    char source_game_dll_full_path[WIN32_STATE_FILENAME_LENGTH];
    win32_build_exe_path_filename(&win32_state, "scrolly.dll",
                                  source_game_dll_full_path, sizeof(source_game_dll_full_path));
    
    LARGE_INTEGER performance_count_frequency_result;
    QueryPerformanceFrequency(&performance_count_frequency_result);
    global_performance_count_frequency = performance_count_frequency_result.QuadPart;
    
    UINT desired_scheduler_ms = 1;
    b32 sleep_is_granular = (timeBeginPeriod(desired_scheduler_ms) == TIMERR_NOERROR);
    
    win32_load_xinput();
    
    win32_resize_dib_section(&global_backbuffer, WIDTH, HEIGHT);
    
    WNDCLASSA window_class = {};
    window_class.style = CS_HREDRAW|CS_VREDRAW|CS_OWNDC;
    window_class.lpfnWndProc = win32_main_window_callback;
    window_class.hInstance = instance;
    window_class.lpszClassName = "scrollyWindowClass";
    
    if (!RegisterClassA(&window_class))  {
        // @todo
    }
    HWND window = CreateWindowExA(0, window_class.lpszClassName,
                                  "scrolly",
                                  WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT,
                                  WINDOW_WIDTH, WINDOW_HEIGHT,
                                  0, 0, instance, 0);
    if (!window)  {
        // @todo
    }
    
    int monitor_refresh_hz = 60;
    HDC refresh_dc = GetDC(window);
    int win32_refresh_rate = GetDeviceCaps(refresh_dc, VREFRESH);
    ReleaseDC(window, refresh_dc);
    if (win32_refresh_rate > 1)  {
        monitor_refresh_hz = win32_refresh_rate;
    }
    f32 game_update_hz = (monitor_refresh_hz / 2.0f);
    f32 target_seconds_per_frame =  1.0f / (f32)game_update_hz;
    f32 dt = target_seconds_per_frame;
    
    
    // @note game memory
#if BUILD_INTERNAL
    LPVOID base_address = (LPVOID)terabytes_to_bytes(2);
#else
    LPVOID base_address = 0;
#endif
    Game_Memory game_memory = {};
    game_memory.platform_free_file_memory = platform_free_file_memory;
    game_memory.platform_read_entire_file = platform_read_entire_file;
    
    game_memory.permanent_storage_size = megabytes_to_bytes(64);
    
    win32_state.game_memory_total_size = game_memory.permanent_storage_size;
    win32_state.game_memory_block = VirtualAlloc(base_address, (size_t)win32_state.game_memory_total_size,
                                                 MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    game_memory.permanent_storage = win32_state.game_memory_block;
    
    if (!game_memory.permanent_storage) {
        return -1;
    }
    
    
    Game_Input input[2] = {};
    Game_Input *new_input = &input[0];
    Game_Input *old_input = &input[1];
    
    Win32_Game_Code game = win32_load_game_code(source_game_dll_full_path);
    if (!game.is_valid) {
        return -1;
    }
    
    global_running = true;
    
    LARGE_INTEGER last_counter = win32_get_wall_clock();
    QueryPerformanceCounter(&last_counter);
    u64 last_cycle_count = __rdtsc();
    while (global_running) {
        new_input->dt = target_seconds_per_frame;
        
        Win32_Window_Dimension dimension = win32_get_window_dimension(window);
        Rectangle_s32 draw_region = aspect_ratio_fit(WIDTH,
                                                     HEIGHT,
                                                     dimension.width, dimension.height);
        
        //
        // @note handle input
        //
        
        Game_Controller_Input *old_keyboard_controller = get_controller(old_input, 0);
        Game_Controller_Input *new_keyboard_controller = get_controller(new_input, 0);
        *new_keyboard_controller = {};
        new_keyboard_controller->is_connected = true;
        for (int button_index = 0; button_index < array_count(new_keyboard_controller->buttons); ++button_index) {
            new_keyboard_controller->buttons[button_index].ended_down = old_keyboard_controller->buttons[button_index].ended_down;
        }
        
        win32_process_pending_messages(new_keyboard_controller);
        
        if (global_pause) {
        	continue;
        }
        
        // @note handle mouse
        POINT mouse_point;
        GetCursorPos(&mouse_point);
        ScreenToClient(window, &mouse_point);
        f32 mouse_x = (f32)mouse_point.x;
        f32 mouse_y = (f32)mouse_point.y; // (f32)(dimension.height - 1) - mouse_point.y);
        
        new_input->mouse_x = clamp_01_map_to_range((f32)draw_region.min_x, mouse_x, (f32)draw_region.max_x) * WIDTH;
        new_input->mouse_y = clamp_01_map_to_range((f32)draw_region.min_y, mouse_y, (f32)draw_region.max_y) * HEIGHT;
        new_input->mouse_z = 0; // @todo Support mousewheel?
        
        win32_process_keyboard_message(&new_input->mouse_buttons[0],
                                       GetKeyState(VK_LBUTTON) & (1 << 15));
        win32_process_keyboard_message(&new_input->mouse_buttons[1],
                                       GetKeyState(VK_MBUTTON) & (1 << 15));
        win32_process_keyboard_message(&new_input->mouse_buttons[2],
                                       GetKeyState(VK_RBUTTON) & (1 << 15));
        win32_process_keyboard_message(&new_input->mouse_buttons[3],
                                       GetKeyState(VK_XBUTTON1) & (1 << 15));
        win32_process_keyboard_message(&new_input->mouse_buttons[4],
                                       GetKeyState(VK_XBUTTON2) & (1 << 15));
        
        // @note handle controllers
        DWORD max_controller_count = XUSER_MAX_COUNT;
        DWORD input_controller_count = array_count(input->controllers) - 1;
        if (max_controller_count > input_controller_count) {
            max_controller_count = input_controller_count;
        }
        for (DWORD controller_index = 0; controller_index < max_controller_count; ++controller_index) {
            DWORD our_controller_index = controller_index + 1;
            Game_Controller_Input *old_controller = get_controller(old_input, our_controller_index);
            Game_Controller_Input *new_controller = get_controller(new_input, our_controller_index);
            *new_controller = {};
            
            XINPUT_STATE controller_state;
            ZeroMemory(&controller_state, sizeof(XINPUT_STATE));
            if (xinput_get_state(controller_index, &controller_state) != ERROR_SUCCESS) {
                new_controller->is_connected = false;
                continue;
            }
            
            new_controller->is_connected = true;
            new_controller->is_analog = old_controller->is_analog;
            XINPUT_GAMEPAD *pad = &controller_state.Gamepad;
            
            // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE 7689
            // XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689
            auto process_xinput_stick_value = [](SHORT value, SHORT deadzone_threshold) {
                f32 result = 0;
                if (value < -deadzone_threshold) {
                    result = (f32)((value + deadzone_threshold) / (32768.0f - deadzone_threshold));
                }
                else if (value > deadzone_threshold){
                    result = (f32)((value - deadzone_threshold) / (32767.0f - deadzone_threshold));
                }
                return result;
            };
            
            new_controller->stick_average_x = process_xinput_stick_value(pad->sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            new_controller->stick_average_y = process_xinput_stick_value(pad->sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            if ((new_controller->stick_average_x != 0.0f) ||
                (new_controller->stick_average_y != 0.0f)) {
                new_controller->is_analog = true;
            }
            
            if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) {
                new_controller->stick_average_y = 1.0f;
                new_controller->is_analog = false;
            }
            if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) {
                new_controller->stick_average_y = -1.0f;
                new_controller->is_analog = false;
            }
            if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) {
                new_controller->stick_average_x = -1.0f;
                new_controller->is_analog = false;
            }
            if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) {
                new_controller->stick_average_x = 1.0f;
                new_controller->is_analog = false;
            }
            
            f32 threshold = 0.5f;
            win32_process_xinput_digital_button((new_controller->stick_average_x < -threshold) ? 1 : 0,
                                                &old_controller->move_left, 1,
                                                &new_controller->move_left);
            win32_process_xinput_digital_button((new_controller->stick_average_x > threshold) ? 1 : 0,
                                                &old_controller->move_right, 1,
                                                &new_controller->move_right);
            win32_process_xinput_digital_button((new_controller->stick_average_y < -threshold) ? 1 : 0,
                                                &old_controller->move_down, 1,
                                                &new_controller->move_down);
            win32_process_xinput_digital_button((new_controller->stick_average_y > threshold) ? 1 : 0,
                                                &old_controller->move_up, 1,
                                                &new_controller->move_up);
            
            win32_process_xinput_digital_button(pad->wButtons,
                                                &old_controller->action_down, XINPUT_GAMEPAD_A,
                                                &new_controller->action_down);
            win32_process_xinput_digital_button(pad->wButtons,
                                                &old_controller->action_right, XINPUT_GAMEPAD_B,
                                                &new_controller->action_right);
            win32_process_xinput_digital_button(pad->wButtons,
                                                &old_controller->action_left, XINPUT_GAMEPAD_X,
                                                &new_controller->action_left);
            win32_process_xinput_digital_button(pad->wButtons,
                                                &old_controller->action_up, XINPUT_GAMEPAD_Y,
                                                &new_controller->action_up);
            
            win32_process_xinput_digital_button(pad->wButtons,
                                                &old_controller->left_shoulder, XINPUT_GAMEPAD_LEFT_SHOULDER,
                                                &new_controller->left_shoulder);
            win32_process_xinput_digital_button(pad->wButtons,
                                                &old_controller->right_shoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                                                &new_controller->right_shoulder);
            
            win32_process_xinput_digital_button(pad->wButtons,
                                                &old_controller->start, XINPUT_GAMEPAD_START,
                                                &new_controller->start);
            win32_process_xinput_digital_button(pad->wButtons,
                                                &old_controller->back, XINPUT_GAMEPAD_BACK,
                                                &new_controller->back);
        }
        
        //
        // @note game_update_and_render
        //
        Game_Offscreen_Buffer buffer = {};
        buffer.memory = global_backbuffer.memory;
        buffer.width  = global_backbuffer.width;
        buffer.height = global_backbuffer.height;
        buffer.pitch  = global_backbuffer.pitch;
        buffer.bytes_per_pixel  = global_backbuffer.bytes_per_pixel;
        
        if (game.update_and_render)  game.update_and_render(&game_memory, new_input, &buffer);
        if (new_input->request_quit) {
            global_running = false;
        }
        
        //
        // @note frame rate
        //
        LARGE_INTEGER work_counter = win32_get_wall_clock();
        f32 seconds_elapsed_for_work = win32_get_seconds_elapsed(last_counter, work_counter);
        
        f32 seconds_elapsed_for_frame = seconds_elapsed_for_work;
        if (seconds_elapsed_for_frame < target_seconds_per_frame) {
            DWORD sleep_ms;
            if (sleep_is_granular) {
                sleep_ms = (DWORD)(1000 * (DWORD)(target_seconds_per_frame - seconds_elapsed_for_frame));
                if (sleep_ms > 0) {
                    Sleep(sleep_ms);
                }
            }
            
            f32 test_seconds_elapsed_for_frame = win32_get_seconds_elapsed(last_counter, win32_get_wall_clock());
            if (test_seconds_elapsed_for_frame > target_seconds_per_frame) {
                // @todo LOG MISSED SLEEP HERE
            }
            
            while (seconds_elapsed_for_frame < target_seconds_per_frame) {
                seconds_elapsed_for_frame = win32_get_seconds_elapsed(last_counter, win32_get_wall_clock());
            }
        }
        else {
            // @todo MISSED FRAME RATE!
            // @todo logging
        }
        
        LARGE_INTEGER end_counter = win32_get_wall_clock();
        f64 ms_per_frame = 1000.0f * win32_get_seconds_elapsed(last_counter, end_counter);
        last_counter = end_counter;
        
        //
        // @note display buffer
        //
        
        HDC device_context = GetDC(window);
        win32_display_buffer_in_window(&global_backbuffer, device_context,
                                       dimension.width, dimension.height);
        ReleaseDC(window, device_context);
        
        
        Game_Input *temp_input = new_input;
        new_input = old_input;
        old_input = temp_input;
        
        u64 end_cycle_count = __rdtsc();
        u64 cycles_elapsed = end_cycle_count - last_cycle_count;
        last_cycle_count = end_cycle_count;
        
        f64 fps = 0; // @note not a relevant measurement (f64)global_performance_count_frequency / (f64)counter_elapsed;
        f64 mcpf = (f64)cycles_elapsed / (1000.0f * 1000.0f);
        
        char fps_buffer[256];
        _snprintf_s(fps_buffer, sizeof(fps_buffer), "%.02fms/work, %.02fms/f, %.02ffps, %.02fmc/f\n", seconds_elapsed_for_work*1000, ms_per_frame, fps, mcpf);
        OutputDebugStringA(fps_buffer);
    }
    
    return 0;
}