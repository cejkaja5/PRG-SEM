
#ifndef __COMMON_FUNCTIONS_LIB_H__
#define __COMMON_FUNCTIONS_LIB_H__

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <complex.h>
#include <signal.h>

#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>

#include "prg_io_nonblock.h"
#include "messages.h"

#define SET_TERMINAL_TO_RAW 0
#define SET_TERMINAL_TO_DEFAULT 1
#define DELAY_MS 10
#define GARBAGE_BUFFER_SIZE 256

#define DEBUG_MESSAGES 0
#define DEBUG_PIPES 0
#define DEBUG_MUTEX 0
#define DEBUG_COMPUTATIONS 0
#define DEBUG_GUI 0
#define DEBUG_MEMORY 1

enum {
    ERROR_OK = 0,
    ERROR_OPENING_PIPE = 100,
    ERROR_CREATING_THREADS = 101,
    ERROR_ALLOCATION = 102,
};

typedef void *(*thread_fnc_ptr)(void *);

typedef struct {
    pthread_mutex_t lock;
    int fd; // file descriptor
} data_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_t thread;
    thread_fnc_ptr thread_function;
    char *thread_name;
} thread_t;

void call_termios(int reset);
bool open_pipes(data_t *in, data_t *out, atomic_bool *quit, const char *in_pipe_name, const char *out_pipe_name);
bool send_message(int *fd, message msg, pthread_mutex_t *fd_lock);
bool recieve_message(int fd, message *out_msg, int timeout_ms, pthread_mutex_t *fd_lock);
void join_all_threads(int N, thread_t threads[N]);
int create_all_threads(int N, thread_t threads[N], void *data);

#endif 
