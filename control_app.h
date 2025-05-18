
#ifndef __CONTROL_APP_H__
#define __CONTROL_APP_H__

#include "common_lib.h"
#include "xwin_sdl.h"

#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif

#include "stb_image_write.h"

#define MAX_IMAGE_NAME_LENGHT 30

#ifdef thread_shared_data_t
#undef thread_shared_data_t
#endif

typedef struct {
    atomic_bool quit;   
    data_t module_to_app;
    data_t app_to_module;
} thread_shared_data_t;

enum {
    WINDOW_NOT_INITIATED,
    WINDOW_ACTIVE,
    WINDOW_CLOSED,
} window_status_enum;

enum {
    DIRECTION_UP = 'A',
    DIRECTION_DOWN = 'B',
    DIRECTION_RIGHT = 'C',
    DIRECTION_LEFT = 'D',
} directions_enum;

#endif