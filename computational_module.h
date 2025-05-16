
#ifndef __COMPUTATIONAL_MODULE_H__
#define __COMPUTATIONAL_MODULE_H__

#include "common_lib.h"

#ifdef thread_shared_data_t
#undef thread_shared_data_t
#endif

typedef struct {
    _Bool quit;
    _Bool computer_thread_has_work;
    _Bool abort_computation;
    pthread_mutex_t computer_lock;
    pthread_cond_t computer_cond;
    double re;
    double im;
    uint8_t n_re;
    uint8_t n_im;
    uint8_t cid;
    data_t module_to_app;
    data_t app_to_module;
} thread_shared_data_t;

#endif
