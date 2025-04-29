
#include "common_lib.h"
#include "control_app.h"

static void *read_user_input(void* arg);
static void *read_from_pipe(void *arg);

int main(int argc, char *argv[]) {
    data_t data[2] = {{.quit = false, .fd = -1}, {.quit = false, .fd = -1}};
    data_t *app_to_module = &data[0];
    data_t *module_to_app = &data[1];
    pthread_t user_input_thread, FIFO_reader_thread;
    pthread_mutex_init(&app_to_module->lock, NULL);
    pthread_mutex_init(&module_to_app->lock, NULL);
    
    call_termios(SET_TERMINAL_TO_RAW);
    

    if (pthread_create(&user_input_thread, NULL, read_user_input, data) != 0){
        fprintf(stderr, "ERROR: Creating user input reader thread failed.\n");
        return(ERROR_CREATING_THREADS);
    } else {
        fprintf(stderr, "INFO: Succesfully created user input reader thread.\n");
    }

    if (pthread_create(&FIFO_reader_thread, NULL, read_from_pipe, data) != 0){
        fprintf(stderr, "ERROR: Creating pipe reader thread failed.\n");
        return(ERROR_CREATING_THREADS);
    } else {
        fprintf(stderr, "INFO: Succesfully created pipe reader thread.\n");
    }

    const char *app_to_module_pipe_name = argc >= 3 ? argv[1] : "/tmp/computational_module.in";
    const char *module_to_app_pipe_name = argc >= 3 ? argv[2] : "/tmp/computational_module.out";
    
    open_pipes(module_to_app, app_to_module, module_to_app_pipe_name, app_to_module_pipe_name);
       
    pthread_join(user_input_thread, NULL);
    pthread_join(FIFO_reader_thread, NULL);
    
    call_termios(SET_TERMINAL_TO_DEFAULT);
    return ERROR_OK;
}

static void *read_user_input(void* arg){
    data_t *app_to_module = (data_t *)arg;
    data_t *module_to_app = app_to_module + 1;
    int c;
    while (!module_to_app->quit && !app_to_module->quit){
        c = getchar();
        switch (c)
        {
        case 'q':
            pthread_mutex_lock(&app_to_module->lock);
            app_to_module->quit = true; 
            pthread_mutex_unlock(&app_to_module->lock);
            break;
        
        default:
            break;
        }
    }
    return NULL;
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
            case MSG_STARTUP:
                fprintf(stderr, "INFO: Modul startup was successfull\n");
                break;
            case MSG_OK:
                fprintf(stderr, "INFO: Modul responded OK.\n");
                break;
            case MSG_ERROR:
                fprintf(stderr, "WARN: Modul responded ERROR.\n");
                break;
            case MSG_COMPUTE_DATA:
                fprintf(stderr, "INFO: Modul returned computed data.\n");
                break;
            case MSG_DONE:
                fprintf(stderr, "INFO: Modul is done with computing.\n");
                break;
            case MSG_ABORT:
                fprintf(stderr, "INFO: Modul has aborted computation.\n");
                break;
            case MSG_VERSION:
                fprintf(stderr, "INFO: Modul version is TODO.\n");
                break;
            default:
                fprintf(stderr, "WARN: modul returned message of unexpected (but defined) type.\n");
                break;
            }
        }
    }
   
    return NULL; 
} 