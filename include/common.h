#ifndef __COMMON_H__
#define __COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)   (sizeof(x)/sizeof((x)[0]))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a): (b))
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif 

#ifndef container_of
#define container_of(ptr, type, member) ((type *)((long)(ptr) - offsetof(type, member)))
#endif

#if defined(__GNUC__)
#define STRUCT_MEMBER(name, var)   .name = var
#else // ARMCC
#define STRUCT_MEMBER(name, var)   var
#endif

#if !defined(DEBUG_VER)
#undef  RELEASE_VER
#endif

#ifndef UNUSE
#define UNUSE(_x) (void)(_x)
#endif

/* example: CFG_TEST ==> "CFG_TEST" */
#ifndef MACRO_TO_STRING
#define TO_STRING(x)                #x
#define MACRO_TO_STRING(x)          TO_STRING(x) 
#endif

#ifndef SET_BIT
#define SET_BIT(x, bit)             (x | (1<<bit))
#endif
#ifndef RESET_BIT
#define RESET_BIT(x, bit)           (x & (~(1<<bit)))
#endif

#ifndef IS_BIT_SET
#define IS_BIT_SET(x, bit)          (x & (1<<bit))
#endif

#ifndef SAFE_FREE
#define SAFE_FREE(x)    if (x) {free(x); (x) = NULL;}
#endif
/*
 * Check at compile time that something is of a particular type.
 * Always evaluates to 1 so you may use it easily in comparisons.
 */
#define typecheck(type,x) \
    ({  type __dummy; \
        typeof(x) __dummy2; \
        (void)(&__dummy == &__dummy2); \
        1; \
    })

/*
 * Check at compile time that 'function' is a certain type, or is a pointer
 * to that type (needs to use typedef for the function type.)
 */
#define typecheck_fn(type,function) \
({  typeof(type) __tmp = function; \
    (void)__tmp; \
})

/*
  * min()/max() macros that also do
  * strict type-checking.. See the
  * "unnecessary" pointer comparison.
  */ 
#ifndef     min
#define min(X, Y)                   \
    ({ typeof (X) __x = (X);        \
        typeof (Y) __y = (Y);       \
        (__x < __y) ? __x : __y; })
#endif
    
#ifndef     max
#define max(X, Y)                   \
    ({ typeof (X) __x = (X);        \
        typeof (Y) __y = (Y);       \
        (__x > __y) ? __x : __y; })
#endif

#ifndef __INLINE
#define __INLINE    inline
#endif

#define BUG()   \
    do {        \
        printf("BUG: failure at %s:%d in %s()!\n", __FILE__, __LINE__, __FUNCTION__); \
    } while (0)

        
#define BUG_ON(condition)                   do { if (condition) BUG(); } while(0)

#ifndef __attribute_used__
#define __attribute_used__                  __attribute__((__used__))
#endif

#ifndef __attribute_maybe_unused__
#define __attribute_maybe_unused__          __attribute__((__unused__))
#endif

#ifndef __attribute_section__
# define __attribute_section__(S)           __attribute__((section(#S)))
#endif
	
#ifdef __cplusplus
}
#endif

#endif