
#include "common_lib.h"
#include "control_app.h"

static void *read_user_input(void* arg);
static void *read_from_pipe(void *arg);
static void cleanup(void);
static thread_shared_data_t *thread_shared_data_init(void);
static void destroy_shared_data(thread_shared_data_t *data);

int main(int argc, char *argv[]) {
    atexit(cleanup);

    int N = 2, ret = ERROR_OK;
    thread_shared_data_t *data = thread_shared_data_init();

    thread_t threads[] = {
        {.thread_name = "Keyboard", .thread_function = read_user_input}, 
        {.thread_name = "Pipe", .thread_function = read_from_pipe}
    };

    if ((ret = create_all_threads(N, threads, data)) != ERROR_OK) return ret;    
    
    const char *app_to_module_pipe_name = argc >= 3 ? argv[1] : "/tmp/computational_module.in";
    const char *module_to_app_pipe_name = argc >= 3 ? argv[2] : "/tmp/computational_module.out";
    
    open_pipes(&data->module_to_app, &data->app_to_module, &data->quit, 
        module_to_app_pipe_name, app_to_module_pipe_name);
    
    join_all_threads(N, threads);
    destroy_shared_data(data);

    return ERROR_OK;
}

static void *read_user_input(void* arg){
    call_termios(SET_TERMINAL_TO_RAW);

    thread_shared_data_t *data = (thread_shared_data_t *)arg;
    int c;
    message msg;
    while (!data->quit){
        c = getchar();
        switch (c)
        {
        case 'q':
            fprintf(stderr, "INFO: Quiting control application.\n");
            pthread_mutex_lock(&data->app_to_module.lock);
            data->quit = true; 
            pthread_mutex_unlock(&data->app_to_module.lock);
            break;
        case 'g':
            fprintf(stderr, "INFO: Requesting module version.\n");
            msg.type = MSG_GET_VERSION;
            send_message(data->app_to_module.fd, msg, &data->app_to_module.lock);
            break;
        case 's':
            fprintf(stderr, "INFO: Setting module computation data.\n");
            msg.type = MSG_SET_COMPUTE;
            msg.data.set_compute.c_re = -0.4;
            msg.data.set_compute.c_im = 0.6;
            msg.data.set_compute.d_re = 0.001;
            msg.data.set_compute.d_im = 0.001;
            msg.data.set_compute.n = 50;
            send_message(data->app_to_module.fd, msg, &data->app_to_module.lock);
            break;
        case '1':
            fprintf(stderr, "INFO: Requesting module computation.\n");
            msg.type = MSG_COMPUTE;
            msg.data.compute.cid = 0;
            msg.data.compute.re = -1.6;
            msg.data.compute.im = -1.1;
            msg.data.compute.n_re = 1.0;
            msg.data.compute.n_im = 1.0;
            send_message(data->app_to_module.fd, msg, &data->app_to_module.lock);
            break;
        default:
            break;
        }
    }
    return NULL;
}

static void *read_from_pipe(void *arg){
    thread_shared_data_t *data = (thread_shared_data_t *)arg;
    
    while (data->module_to_app.fd == -1 && data->quit) {
        usleep(DELAY_MS); // waiting for pipe to be joined
    }    
    message msg;

    while(!data->quit){
        if (recieve_message(data->module_to_app.fd, &msg, DELAY_MS)){
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
                fprintf(stderr, "INFO: Modul version is %d.%d.%d\n", msg.data.version.major,
                msg.data.version.minor, msg.data.version.patch);
                break;
            default:
                fprintf(stderr, "WARN: modul returned message of unexpected (but defined) type.\n");
                break;
            }
        }
    }
   
    return NULL; 
} 

static thread_shared_data_t *thread_shared_data_init(void){
    thread_shared_data_t *data = malloc(sizeof(thread_shared_data_t));
    if (data == NULL){
        fprintf(stderr, "ERROR: Allocation failed.\n");
        exit(ERROR_ALLOCATION);
    }
    data->quit = false;
    data->app_to_module.fd = -1;
    data->module_to_app.fd = -1;
    pthread_mutex_init(&data->app_to_module.lock, NULL);
    pthread_mutex_init(&data->module_to_app.lock, NULL);
    return data;
}

static void destroy_shared_data(thread_shared_data_t *data){
    pthread_mutex_destroy(&data->app_to_module.lock);
    pthread_mutex_destroy(&data->module_to_app.lock);
    free(data);
}

static void cleanup(void){
    call_termios(SET_TERMINAL_TO_DEFAULT);
}