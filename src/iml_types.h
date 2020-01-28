#if !defined(IML_TYPES_H)

#ifdef IML_NAMESPACE
namespace iml {
#if 0
}
#endif
#endif


#if 1
typedef bool __check_int____size32__[sizeof(int)    == 4  ? 1  : -1];
typedef bool __check_bool___size8___[sizeof(bool)   == 1  ? 1  : -1]; // unnecessary
typedef bool __check_float__size32__[sizeof(float)  == 4  ? 1  : -1];
typedef bool __check_double_size64__[sizeof(double) == 8  ? 1  : -1];
#else
static_assert(sizeof(size_t) == 8, "Compiling for 32-bit not supported!");
#endif



#if !defined(internal)
#  define internal    static
#endif
#define local_persist static
#define global        static
#if !defined(inline)
#  define inline      static
#endif

#if !defined(null)
#  define null 0
#endif

#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <float.h>

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef uint32_t b32;

typedef float  f32; // @todo j naming float32
typedef double f64; // @todo j naming float64

// @todo Rename to uptr and sptr?
typedef uintptr_t uintptr;
typedef intptr_t  intptr;

typedef uintptr_t umm;
typedef intptr_t  smm;

// @todo just size?
#if 0
typedef size_t size_type;
#else

#if defined(ENV64) // @todo better name?
typedef u64 size_type;
#elif defined(ENV32) // @todo better name?
typedef u32 size_type;
#else
typedef size_t size_type;
#endif

#endif

typedef size_type memory_index; // @todo short name?

#define flag8(type)  u8
#define flag16(type) u16
#define flag32(type) u32
#define flag64(type) u64

#define enum8(type)  u8
#define enum16(type) u16
#define enum32(type) u32
#define enum64(type) u64
// @todo Signed enums?


#ifdef IML_NAMESPACE
#if 0
{
#endif
}
#endif

#define IML_TYPES_H
#endif
