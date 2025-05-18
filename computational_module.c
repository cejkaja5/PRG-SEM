
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>


#include "computational_module.h"
#include "common_lib.h"
#include "prg_io_nonblock.h"

static void *read_from_pipe(void *arg);
static void *read_user_input(void *arg);
static void *compute(void *arg);
static void cleanup(void);
static void send_version_message(int *fd, pthread_mutex_t *fd_lock);
static void send_ok_message(int *fd, pthread_mutex_t *fd_lock);
static void send_error_message(int *fd, pthread_mutex_t *fd_lock);
static void send_abort_message(int *fd, pthread_mutex_t *fd_lock);
static void send_done_message(int *fd, pthread_mutex_t *fd_lock);
static thread_shared_data_t *thread_shared_data_init(void);
static void destroy_shared_data(thread_shared_data_t *data);
static uint8_t compute_one_pixel(complex double z);
static void print_help(void);
static void computational_module_init(void);

static const uint8_t major = 1;
static const uint8_t minor = 2;
static const uint8_t patch = 3;
static const uint8_t startup_message[] = {'c','e','j','k','a','\0'};

static double complex c = 0.0 + 0.0 * I; // constant for calculation
static double complex d = 0.0 + 0.0 * I; // increment
static uint8_t n = -1;

int main(int argc, char *argv[]) {
    computational_module_init();

    int N = 3, ret = ERROR_OK;
    thread_shared_data_t *data = thread_shared_data_init();
    thread_t threads[] = {
        { .thread_name = "Pipe", .thread_function = read_from_pipe}, 
        { .thread_name = "Keyboard", .thread_function = read_user_input},
        { .thread_name = "Compute", .thread_function = compute},
    };
        

    if ((ret = create_all_threads(N, threads, data)) != ERROR_OK) return ret;

    const char *app_to_module_pipe_name = argc >= 3 ? argv[1] : "/tmp/computational_module.in";
    const char *module_to_app_pipe_name = argc >= 3 ? argv[2] : "/tmp/computational_module.out";

    if (open_pipes(&data->app_to_module, &data->module_to_app, &data->quit, 
        app_to_module_pipe_name, module_to_app_pipe_name)){
        message msg = {.type = MSG_STARTUP};
        memcpy(msg.data.startup.message, startup_message, sizeof(startup_message));    
        send_message(&data->module_to_app.fd, msg, &data->module_to_app.lock);
    }

    join_all_threads(N, threads);
    destroy_shared_data(data);

    return ERROR_OK;
}

static void *read_from_pipe(void *arg){
    thread_shared_data_t *data = (thread_shared_data_t *)arg;
    queue_t queue;
    
    while (data->module_to_app.fd == -1 && !atomic_load(&data->quit)){
        usleep(DELAY_MS);
    } ; // waiting for pipe to be joined
            
    message msg;

    while(!atomic_load(&data->quit)){
        if (recieve_message(data->app_to_module.fd, &msg, DELAY_MS, &data->app_to_module.lock)){
            switch (msg.type)
            {
            case MSG_GET_VERSION:
                if (data->app_to_module.fd == -1) break;
                fprintf(stderr, "INFO: App requested version.\n");
                send_version_message(&data->module_to_app.fd, &data->module_to_app.lock);
                break;
            case MSG_SET_COMPUTE:
                c = msg.data.set_compute.c_re + msg.data.set_compute.c_im * I; 
                d = msg.data.set_compute.d_re + msg.data.set_compute.d_im * I;
                n = msg.data.set_compute.n;
                fprintf(stderr, "INFO: App set computation data. c = %.4f %+.4fi, d = %.4f %+.4fi, n = %d\n", 
                    creal(c), cimag(c), creal(d), cimag(d), n);
                if (data->app_to_module.fd == -1) break;
                send_ok_message(&data->module_to_app.fd, &data->module_to_app.lock);
                break;
            case MSG_COMPUTE:
                fprintf(stderr, "INFO: App requested computation (cid %d).\n", msg.data.compute.cid);
                if (n <= 0 || (creal(c) == 0.0 && cimag(c) == 0.0) || creal(d) == 0.0 || cimag(d) == 0.0){
                    fprintf(stderr, "WARN: Computation data has not been set properly.\n");
                    if (data->app_to_module.fd == -1) break;
                    send_error_message(&data->module_to_app.fd, &data->module_to_app.lock);
                    if (data->computer_thread_has_work && !data->abort_computation){
                        data->abort_computation = true;
                    }
                    break;
                } else if (data->computer_thread_has_work){
                    if (data->app_to_module.fd == -1) break;
                    fprintf(stderr, "WARN: Computer thread is busy.\n");
                    send_error_message(&data->module_to_app.fd, &data->module_to_app.lock);
                    break;
                }
                pthread_mutex_lock(&data->computer_lock);
                data->computer_thread_has_work = true;
                data->re = msg.data.compute.re;
                data->im = msg.data.compute.im;
                data->n_re = msg.data.compute.n_re;
                data->n_im = msg.data.compute.n_im;
                data->cid = msg.data.compute.cid;
#if DEBUG_COMPUTATIONS                
                fprintf(stderr, "DEBUG: Parameters: lower left corner %.4f %+.4fi, n_re = %d, n_im = %d, cid = %d\n",
                    data->re, data->im, data->n_re, data->n_im, data->cid);
#endif
                pthread_cond_signal(&data->computer_cond);
                pthread_mutex_unlock(&data->computer_lock);
                break;
            case MSG_ABORT:
                if (data->app_to_module.fd == -1) break;
                fprintf(stderr, "INFO: App requested abortion.\n");
                send_abort_message(&data->module_to_app.fd, &data->module_to_app.lock);
                break;
            case MSG_QUIT:
                fprintf(stderr, "INFO: Quiting module.\n");
                pthread_mutex_lock(&data->computer_lock);
                atomic_store(&data->quit, true);
                pthread_cond_signal(&data->computer_cond);
                pthread_mutex_unlock(&data->computer_lock);  
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
    thread_shared_data_t *data = (thread_shared_data_t *)arg;    
    uint8_t c;
    int r;
    while (!atomic_load(&data->quit)){
        if ((r = io_getc_timeout(STDIN_FILENO, DELAY_MS, &c)) == -1){
            fprintf(stderr, "ERROR: io_getc_timeout() from stdin failed: %s\n", strerror(errno));
            continue;
        } else if (r == 0){
            continue; // no character read
        }
        switch (c)
        {
        case 'q':
            pthread_mutex_lock(&data->computer_lock);
            atomic_store(&data->quit, true);
            pthread_cond_signal(&data->computer_cond);
            pthread_mutex_unlock(&data->computer_lock);
            fprintf(stderr, "INFO: Quiting module.\n");
            break;
        case 'a':
            if (data->app_to_module.fd == -1) break;
            fprintf(stderr, "INFO: Aborting.\n");
            data->abort_computation = true;            
            send_abort_message(&data->module_to_app.fd, &data->module_to_app.lock);
            break;
        case 'h':
            print_help();
            break;
        default:
            break;
        }
    }
    return NULL;
}

static void *compute(void *arg){
    thread_shared_data_t *data = (thread_shared_data_t *)arg;
    
    pthread_mutex_lock(&data->computer_lock);
    while (!atomic_load(&data->quit)){
        while (!data->computer_thread_has_work && !atomic_load(&data->quit)){
            pthread_cond_wait(&data->computer_cond, &data->computer_lock);
        }
        if (atomic_load(&data->quit)) break;

        uint8_t *chunk_buffer = malloc(data->n_im * data->n_re);
        if (chunk_buffer == NULL){
            fprintf(stderr, "ERROR: allocation of chunk buffer failed.\n");
            data->computer_thread_has_work = false;
            break;
        }
        message msg = {.type = MSG_COMPUTE_DATA_BURST, .data.compute_data_burst = {.chunk_id = data->cid, 
            .iters = chunk_buffer, .length = data->n_im * data->n_re}};       


        complex double z, z0 = data->re + data->im * I;
        for (int row = 0, i = 0; row < data->n_im && !atomic_load(&data->quit) && !data->abort_computation; row++){
            for (int col = 0; col < data->n_re && !atomic_load(&data->quit) && !data->abort_computation; col++, i++){
                z = z0 + (col * creal(d) + row * cimag(d) * I);
                chunk_buffer[i] = compute_one_pixel(z);
            } 
        }
        send_message(&data->module_to_app.fd, msg, &data->module_to_app.lock);
        free(chunk_buffer);
        data->abort_computation = false;
        data->computer_thread_has_work = false;
        send_done_message(&data->module_to_app.fd, &data->module_to_app.lock);
    }

    pthread_mutex_unlock(&data->computer_lock);
    
    return NULL;
}

static uint8_t compute_one_pixel(complex double z){
    int i = 0;
    for (; i < n; i++){
        if (cabs(z) > 2){
            break;
        }
        z = z * z + c;
    }
    return i;
}

static thread_shared_data_t *thread_shared_data_init(void){
    thread_shared_data_t *data = malloc(sizeof(thread_shared_data_t));
    if (data == NULL){
        fprintf(stderr, "ERROR: Allocation failed.\n");
        exit(ERROR_ALLOCATION);
    }
    atomic_store(&data->quit, false);
    data->computer_thread_has_work = false;
    data->abort_computation = false;
    data->app_to_module.fd = -1;
    data->module_to_app.fd = -1;
    pthread_mutex_init(&data->app_to_module.lock, NULL);
    pthread_mutex_init(&data->module_to_app.lock, NULL);
    pthread_mutex_init(&data->computer_lock, NULL);
    pthread_cond_init(&data->computer_cond, NULL);
    return data;
}

static void destroy_shared_data(thread_shared_data_t *data){
    pthread_mutex_destroy(&data->app_to_module.lock);
    pthread_mutex_destroy(&data->module_to_app.lock);
    pthread_mutex_destroy(&data->computer_lock);
    pthread_cond_destroy(&data->computer_cond);
    free(data);
}

static void cleanup(void){
    call_termios(SET_TERMINAL_TO_DEFAULT);
}

static void send_version_message(int *fd, pthread_mutex_t *fd_lock){
    message msg = {.type = MSG_VERSION, .data.version.major = major, 
        .data.version.minor = minor, .data.version.patch = patch};
    send_message(fd, msg, fd_lock);
}
    
static void send_ok_message(int *fd, pthread_mutex_t *fd_lock){
    message msg = {.type = MSG_OK};
    send_message(fd, msg, fd_lock);
}

static void send_error_message(int *fd, pthread_mutex_t *fd_lock){
    message msg = {.type = MSG_ERROR};
    send_message(fd, msg, fd_lock);
}

static void send_abort_message(int *fd, pthread_mutex_t *fd_lock){
    message msg = {.type = MSG_ABORT};
    send_message(fd, msg, fd_lock);
}

static void send_done_message(int *fd, pthread_mutex_t *fd_lock){
    message msg = {.type = MSG_DONE};
    send_message(fd, msg, fd_lock);
}

static void print_help(void){
    fprintf(stderr, "\n============================= COMMANDS =============================\n");
    fprintf(stderr, "\t'q' - Quit module.\n"); 
    fprintf(stderr, "\t'a' - Abort computation.\n");
    fprintf(stderr, "\t'h' - Help message.\n");
    fprintf(stderr, "====================================================================\n\n");
}

static void computational_module_init(void){
    atexit(cleanup);
    call_termios(SET_TERMINAL_TO_RAW);
    fprintf(stderr, "INFO: Press 'h' for help.\n");
    signal(SIGPIPE, SIG_IGN);
}

