#if !defined(IML_GENERAL_H)

#ifdef IML_NAMESPACE
namespace iml {
#if 0
}
#endif
#endif


//
// includes
//

#include "iml_types.h"


//
// defines
//

// #include <assert.h>
#define assert(expression) if (!(expression)) { *(int *)(0) = 0; }
#define array_count(array) (sizeof(array) / sizeof((array)[0]))


//
// defer
//

#define CONCAT_INTERNAL(x,y) x ## y
#define CONCAT(x,y) CONCAT_INTERNAL(x,y)

template<typename T>
struct Exit_Scope {
    T lambda;
    Exit_Scope(T lambda): lambda(lambda) {}
    ~Exit_Scope() {
        lambda();
    }
    Exit_Scope(const Exit_Scope&);
    private:
    Exit_Scope& operator = (const Exit_Scope&);
};

struct Exit_Scope_Help {
    template<typename T>
        Exit_Scope<T> operator+(T t) {
        return t;
    }
};

#define defer const auto& CONCAT(defer__, __LINE__) = Exit_Scope_Help() + [&]()


//
// general for macros
//

#define For(array)               \
for (auto &it = (array).begin(); \
it != (array).end();        \
++it)


#ifdef IML_NAMESPACE
#if 0
{
#endif
}
#endif

#define IML_GENERAL_H
#endif
