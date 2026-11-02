
#ifndef __INIT_H__
#define __INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef struct {
    const char  *name;
    int         (*init)(void);
} initcall_t;

#if defined(__GNUC__)
#define APP_EARLY_INITCALL_START        __start_app_early_initcall
#define APP_EARLY_INITCALL_END          __stop_app_early_initcall
#define APP_LATE_INITCALL_START         __start_app_late_initcall
#define APP_LATE_INITCALL_END           __stop_app_late_initcall
#else
#define APP_EARLY_INITCALL_START        Image$$APPEARLYINIT$$Base
#define APP_EARLY_INITCALL_END          Image$$APPEARLYINIT$$Limit
#define APP_LATE_INITCALL_START         Image$$APPLATEINIT$$Base
#define APP_LATE_INITCALL_END           Image$$APPLATEINIT$$Limit
#endif

extern const initcall_t APP_EARLY_INITCALL_START[], APP_EARLY_INITCALL_END[];
extern const initcall_t APP_LATE_INITCALL_START[], APP_LATE_INITCALL_END[];

/* 定义不同级别的initcall */
#define __define_initcall(level,fn) \
        static const initcall_t __initcall_##fn __attribute_used__ \
        __attribute__((section(".app.initcall" #level ".init"))) = \
        { #fn, fn, };

#define app_frist_initcall(fn)      __define_initcall(1,fn)
#define app_early_initcall(fn)      __define_initcall(2,fn)
#define app_late_initcall(fn)       __define_initcall(3,fn)

#ifdef __cplusplus
}
#endif

#endif
