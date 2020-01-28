#if !defined(SDL2_SCROLLY_H)


struct SDL2_Offscreen_Buffer {
    SDL_Texture *texture;
    void *memory;
    int width;
    int height;
    int pitch;
    int bytes_per_pixel;
};

struct SDL2_Window_Dimension {
    int width;
    int height;
};

struct SDL2_Game_Code {
    void *game_code_dll;
    
    // @important Either of the callbacks can be null!
    // You must check before calling.
    Game_Update_And_Render_Function *update_and_render;
    
    b32 is_valid;
};

#define SDL2_STATE_FILENAME_LENGTH 255 // @todo

struct SDL2_State {
    u64 game_memory_total_size;
    void *game_memory_block;
    
    char *executable_path;
};


#define SDL2_SCROLLY_H
#endif
