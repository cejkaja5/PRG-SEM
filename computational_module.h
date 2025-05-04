
#ifndef __COMPUTATIONAL_MODULE_H__
#define __COMPUTATIONAL_MODULE_H__

#include "common_lib.h"

#ifdef thread_shared_data_t
#undef thread_shared_data_t
#endif

typedef struct {
    _Bool quit;
    data_t module_to_app;
    data_t app_to_module;
} thread_shared_data_t;

#endif
