
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "computational_module.h"
#include "common_lib.h"
#include "prg_io_nonblock.h"

static void *read_from_pipe(void *arg);
static void *read_user_input(void *arg);
static void cleanup(void);

int main(int argc, char *argv[]) {
    atexit(cleanup);

    int N = 2, ret = ERROR_OK;
    data_t data[] = {{.quit = false, .fd = -1, .thread_name = "Pipe", .thread_function = read_from_pipe}, 
        {.quit = false, .fd = -1, .thread_name = "Keyboard", .thread_function = read_user_input},
    };
    data_t *app_to_module = &data[0];
    data_t *module_to_app = &data[1];

    if ((ret = create_all_threads(N, data)) != ERROR_OK) return ret;

    const char *app_to_module_pipe_name = argc >= 3 ? argv[1] : "/tmp/computational_module.in";
    const char *module_to_app_pipe_name = argc >= 3 ? argv[2] : "/tmp/computational_module.out";

    open_pipes(app_to_module, module_to_app, app_to_module_pipe_name, module_to_app_pipe_name);
    
    message msg = {.type = MSG_STARTUP};

    send_message(module_to_app->fd, msg);
  
    data[0].quit = true;

    join_all_threads(N, data);

    return ERROR_OK;
}

static void *read_from_pipe(void *arg){
    data_t *app_to_module = (data_t *)arg;
    data_t *module_to_app = app_to_module + 1;
    
    while (module_to_app->fd == -1 && !module_to_app->quit && 
        !app_to_module->quit) ; // waiting for pipe to be joined
            
    message msg;

    while(!module_to_app->quit && !app_to_module->quit){
        if (recieve_message(module_to_app->fd, &msg, DELAY_MS)){
            switch (msg.type)
            {
            case MSG_GET_VERSION:
                fprintf(stderr, "INFO: App requested version.\n");
                break;
            case MSG_SET_COMPUTE:
                fprintf(stderr, "INFO: App set computation data.\n");
                break;
            case MSG_COMPUTE:
                fprintf(stderr, "INFO: App requested computation.\n");
                break;
            case MSG_ABORT:
                fprintf(stderr, "INFO: App requested abortion.\n");
                break;
            default:
                fprintf(stderr, "WARN: App sent message of unexpected (but defined) type.\n");
                break;
            }
        }
    }
   
    return NULL; 
} 

static void *read_user_input(void *arg){
    call_termios(SET_TERMINAL_TO_RAW);
    return NULL;
}

static void cleanup(void){
    call_termios(SET_TERMINAL_TO_DEFAULT);
}

