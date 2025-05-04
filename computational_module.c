
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "computational_module.h"
#include "common_lib.h"
#include "prg_io_nonblock.h"

static void *read_from_pipe(void *arg);

int main(int argc, char *argv[]) {
    data_t data[2] = {{.quit = false, .fd = -1}, {.quit = false, .fd = -1}};
    data_t *app_to_module = &data[0];
    data_t *module_to_app = &data[1];
    pthread_t FIFO_reader_thread;
    pthread_mutex_init(&app_to_module->lock, NULL);
    pthread_mutex_init(&module_to_app->lock, NULL);

    // call_termios(SET_TERMINAL_TO_RAW);

    if (pthread_create(&FIFO_reader_thread, NULL, read_from_pipe, data) != 0){
        fprintf(stderr, "ERROR: Creating pipe reader thread failed.\n");
        return(ERROR_CREATING_THREADS);
    } else {
        fprintf(stderr, "INFO: Succesfully created pipe reader thread.\n");
    }

    const char *app_to_module_pipe_name = argc >= 3 ? argv[1] : "/tmp/computational_module.in";
    const char *module_to_app_pipe_name = argc >= 3 ? argv[2] : "/tmp/computational_module.out";

    open_pipes(app_to_module, module_to_app, app_to_module_pipe_name, module_to_app_pipe_name);
    
    message msg;
    int types[] = {MSG_STARTUP, MSG_OK, MSG_ERROR, MSG_COMPUTE_DATA, MSG_DONE, MSG_ABORT, MSG_VERSION, MSG_COMPUTE};
    for (int i = 0; i < 8; i++){
        msg.type = types[i];
        send_message(module_to_app->fd, msg);
    }

    pthread_join(FIFO_reader_thread, NULL);

    call_termios(SET_TERMINAL_TO_DEFAULT);
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


