
#ifndef __COMPUTATIONAL_MODULE_H__
#define __COMPUTATIONAL_MODULE_H__

#include "common_lib.h"

#ifdef thread_shared_data_t
#undef thread_shared_data_t
#endif

#define DEFAULT_NUM_OF_WORKERS 2

typedef struct {
    data_t module_to_app;
    data_t app_to_module;
    queue_t *queue_of_work;
    atomic_bool abort;
} thread_shared_data_t;

typedef struct {
    atomic_bool is_ready; // thread has been created
    atomic_bool abort;
    atomic_bool is_busy;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    message work;
    data_t *module_to_app;
} data_compute_worker_t;

typedef struct {
    atomic_bool *abort;
    queue_t *queue_of_work;
    uint8_t num_of_workers;
    data_compute_worker_t **array_of_ptrs_to_worker_data;
} data_compute_boss_t;


#endif
