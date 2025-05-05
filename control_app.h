
#ifndef __CONTROL_APP_H__
#define __CONTROL_APP_H__

#include "common_lib.h"
#include "xwin_sdl.h"

#ifdef thread_shared_data_t
#undef thread_shared_data_t
#endif

typedef struct {
    _Bool quit;
    pthread_mutex_t WR_pipe_lock; // locks named pipe, so that only one thread at time writes into it
    data_t module_to_app;
    data_t app_to_module;
} thread_shared_data_t;

enum {
    WINDOW_NOT_INITIATED,
    WINDOW_ACTIVE,
    WINDOW_CLOSED,
} window_status_enum;

#endif