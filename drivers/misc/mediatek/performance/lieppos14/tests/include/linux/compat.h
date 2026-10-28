#include <stdint.h>
typedef uint32_t compat_uptr_t;
typedef int32_t compat_long_t;
#define compat_ptr(x) ((void *)(uintptr_t)(uint32_t)(x))
