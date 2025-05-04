
#ifndef __COMMON_FUNCTIONS_LIB_H__
#define __COMMON_FUNCTIONS_LIB_H__

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "prg_io_nonblock.h"
#include "messages.h"

#define SET_TERMINAL_TO_RAW 0
#define SET_TERMINAL_TO_DEFAULT 1
#define DELAY_MS 10

enum {
    ERROR_OK = 0,
    ERROR_OPENING_PIPE = 100,
    ERROR_CREATING_THREADS = 101,
};

typedef void *(*thread_fnc_ptr)(void *);

typedef struct {
    bool quit;
    pthread_mutex_t lock;
    pthread_t thread;
    thread_fnc_ptr thread_function;
    char *thread_name;
    int fd; // file descriptor
} data_t;

void call_termios(int reset);
void open_pipes(data_t *in, data_t *out, const char *in_pipe_name, const char *out_pipe_name);
bool send_message(int fd, message msg);
bool recieve_message(int fd, message *out_msg, int timeout_ms);
void join_all_threads(int N, data_t data[N]);
int create_all_threads(int N, data_t data[N]);

#endif
