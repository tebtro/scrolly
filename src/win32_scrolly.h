#if !defined(WIN32_SCROLLY_H)


struct Win32_Offscreen_Buffer {
    BITMAPINFO info;
    void *memory;
    int width;
    int height;
    int pitch;
    int bytes_per_pixel;
};

struct Win32_Window_Dimension {
    int width;
    int height;
};

struct Win32_Game_Code {
    HMODULE game_code_dll;
    
    // @important Either of the callbacks can be null!
    // You must check before calling.
    Game_Update_And_Render_Function *update_and_render;
    
    b32 is_valid;
};

#define WIN32_STATE_FILENAME_LENGTH MAX_PATH

struct Win32_State {
    u64 game_memory_total_size;
    void *game_memory_block;
    
    char exe_filename[WIN32_STATE_FILENAME_LENGTH];
    char *exe_filename_one_past_last_slash;
};


#define WIN32_SCROLLY_H
#endif
