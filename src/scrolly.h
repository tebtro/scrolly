#if !defined(SCROLLY_H)


#include "scrolly_platform.h"
#include "iml_general.h"


//
// @note: memory management
//

struct Memory_Arena {
    memory_index size;
    u8 *base;
    memory_index used;
};

internal void
initialize_memory_arena(Memory_Arena *arena,
                        memory_index size,
                        u8 *base) {
    arena->size = size;
    arena->base = base;
    arena->used = 0;
}

#define push_struct(arena, type) (type *)_push_size_(arena, sizeof(type))
#define push_array(arena, count, type) (type *)_push_size_(arena, (count)*sizeof(type))
internal void *
_push_size_(Memory_Arena *arena, memory_index size) {
    assert((arena->used + size) <= arena->size);
    void *memory = arena->base + arena->used;
    arena->used += size;
    
    return memory;
}


//
// @note: game related
//

struct Loaded_Bitmap {
    s32 width, height;
    u32 *pixels;
};

struct Tilemap {
    u32 width;
    u32 height;
    u32 *tiles;
};

struct Player {
    f32 pos_x;
    f32 pos_y;
    
    Loaded_Bitmap bmp;
};

struct Game {
    Loaded_Bitmap bmp_background;
    Tilemap *current_tilemap;
    Player player;
};

struct Game_State {
    Memory_Arena game_arena;
    Game *game;
    
    b32 show_titlescreen = true;
    u32 active_controller_index;
    
    f32 tile_size;
    
    Loaded_Bitmap bmp_font;
};

#define SCROLLY_H
#endif