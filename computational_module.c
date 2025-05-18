
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>


#include "computational_module.h"
#include "common_lib.h"
#include "prg_io_nonblock.h"

static void *read_from_pipe(void *arg);
static void *read_user_input(void *arg);
static void *compute_boss(void *arg);
static void *compute_worker(void *arg);
static void cleanup(void);
static void send_version_message(int *fd, pthread_mutex_t *fd_lock);
static void send_ok_message(int *fd, pthread_mutex_t *fd_lock);
static void send_error_message(int *fd, pthread_mutex_t *fd_lock);
static void send_abort_message(int *fd, pthread_mutex_t *fd_lock);
static void send_done_message(int *fd, pthread_mutex_t *fd_lock);
static thread_shared_data_t *thread_shared_data_init(void);
static data_compute_boss_t *data_compute_boss_init(atomic_bool *abort, queue_t *queue_of_work, 
    uint8_t num_of_workers, data_t *module_to_app);
static data_compute_worker_t *data_compute_worker_init(data_t *module_to_app);
static void destroy_shared_data(thread_shared_data_t *data, data_compute_boss_t *boss_data);
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
static atomic_bool quit;

int main(int argc, char *argv[]) {
    computational_module_init();

    int ret = ERROR_OK, tmp;
    uint8_t num_of_non_workers = 3, num_of_workers = (argc >= 2 && (tmp = atoi(argv[1])) > 0 && tmp <= 8) ? 
        tmp : DEFAULT_NUM_OF_WORKERS;
    thread_shared_data_t *data = thread_shared_data_init();
    data_compute_boss_t *data_boss = data_compute_boss_init(&data->abort, data->queue_of_work, 
        num_of_workers, &data->module_to_app);

    thread_t threads[num_of_non_workers + num_of_workers];        
    threads[0].thread_name = "Pipe",  threads[0].thread_function = read_from_pipe,  threads[0].data = data; 
    threads[1].thread_name = "Keyboard", threads[1].thread_function = read_user_input, threads[1].data = data;
    threads[2].thread_name = "Compute boss", threads[2].thread_function = compute_boss, threads[2].data = data_boss;
    for (int i = 0; i < num_of_workers; i++){
        char *worker_name = malloc(sizeof("Compute worker ") + 3);// three digits for number
        if (worker_name == NULL){
            fprintf(stderr, "FATAL ERROR: allocation failed.\n");
            exit(ERROR_ALLOCATION);
        }
        snprintf(worker_name, sizeof("Compute worker ") + 3, "Compute worker %d", i);
        threads[num_of_non_workers + i].thread_name = worker_name;
        threads[num_of_non_workers + i].thread_function = compute_worker;
        threads[num_of_non_workers + i].data = data_boss->array_of_ptrs_to_worker_data[i];
    }
    
    if ((ret = create_all_threads(num_of_non_workers + num_of_workers, threads)) != ERROR_OK) return ret;

    const char *app_to_module_pipe_name = argc >= 4 ? argv[2] : "/tmp/computational_module.in";
    const char *module_to_app_pipe_name = argc >= 4 ? argv[3] : "/tmp/computational_module.out";

    if (open_pipes(&data->app_to_module, &data->module_to_app, &quit, 
        app_to_module_pipe_name, module_to_app_pipe_name) && sizeof(startup_message) + 1 <= STARTUP_MSG_LEN){
        message msg = {.type = MSG_STARTUP};
        memcpy(msg.data.startup.message, startup_message, sizeof(startup_message));  
        msg.data.startup.message[sizeof(startup_message)] = num_of_workers;
        send_message(&data->module_to_app.fd, msg, &data->module_to_app.lock);
    }

    join_all_threads(num_of_non_workers + num_of_workers, threads);
    for (int i = 0; i < num_of_workers; i++) free(threads[i + num_of_non_workers].thread_name);

    destroy_shared_data(data, data_boss);

    return ERROR_OK;
}

static void *read_from_pipe(void *arg){
    thread_shared_data_t *data = (thread_shared_data_t *)arg;
    
    while (data->module_to_app.fd == -1 && !atomic_load(&quit)){
        usleep(DELAY_MS);
    } ; // waiting for pipe to be joined
            
    message msg;

    while(!atomic_load(&quit)){
        if (recieve_message(data->app_to_module.fd, &msg, DELAY_MS, &data->app_to_module.lock)){
            switch (msg.type)
            {
            case MSG_GET_VERSION:
                if (data->app_to_module.fd == -1) break;
                fprintf(stderr, "INFO: App requested version.\n");
                send_version_message(&data->module_to_app.fd, &data->module_to_app.lock);
                break;
            case MSG_SET_COMPUTE:
                atomic_store(&data->abort, true); // abort ongoing calculation with old values. Boss thread will clear queue.
                c = msg.data.set_compute.c_re + msg.data.set_compute.c_im * I; 
                d = msg.data.set_compute.d_re + msg.data.set_compute.d_im * I;
                n = msg.data.set_compute.n;
                fprintf(stderr, "INFO: App set new computation data. c = %.4f %+.4fi, d = %.4f %+.4fi, n = %d\n", 
                    creal(c), cimag(c), creal(d), cimag(d), n);
                if (data->app_to_module.fd == -1) break;
                send_ok_message(&data->module_to_app.fd, &data->module_to_app.lock);
                break;
            case MSG_COMPUTE:
                if (n <= 0 || (creal(c) == 0.0 && cimag(c) == 0.0) || creal(d) == 0.0 || cimag(d) == 0.0){
                    fprintf(stderr, "WARN: Computation data has not been set properly.\n");
                    if (data->app_to_module.fd == -1) break;
                    send_error_message(&data->module_to_app.fd, &data->module_to_app.lock);
                    break;
                }
                message *msg_copy = malloc(sizeof(message));
                if (msg_copy == NULL){
                    fprintf(stderr, "FATAL ERROR: Allocation failed.\n");
                    exit(ERROR_ALLOCATION);
                } 
                while (atomic_load(&data->abort)) {
                    usleep(DELAY_MS); // Boss thread is aborting
                }
                *msg_copy = msg;
                queue_push(data->queue_of_work, msg_copy);
                send_ok_message(&data->module_to_app.fd, &data->module_to_app.lock);
                break;
            case MSG_ABORT:
                if (data->app_to_module.fd == -1) break;
                fprintf(stderr, "INFO: App requested abortion.\n");
                atomic_store(&data->abort, true);
                send_abort_message(&data->module_to_app.fd, &data->module_to_app.lock);
                break;
            case MSG_QUIT:
                fprintf(stderr, "INFO: Quiting module.\n");
                atomic_store(&quit, true);
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
    while (!atomic_load(&quit)){
        if ((r = io_getc_timeout(STDIN_FILENO, DELAY_MS, &c)) == -1){
            fprintf(stderr, "ERROR: io_getc_timeout() from stdin failed: %s\n", strerror(errno));
            continue;
        } else if (r == 0){
            continue; // no character read
        }
        switch (c)
        {
        case 'q':
            atomic_store(&quit, true);
            fprintf(stderr, "INFO: Quiting module.\n");
            break;
        case 'a':
            fprintf(stderr, "INFO: Aborting.\n");
            atomic_store(&data->abort, true);        
            if (data->app_to_module.fd == -1) break;
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

static void *compute_boss(void *arg){
    data_compute_boss_t *data = (data_compute_boss_t*)arg;
    bool workers_are_ready = false;

    while (!workers_are_ready){ // wait until all worker threads are ready
        workers_are_ready = true;
        for (int i = 0; i < data->num_of_workers; i++){
            if (data->array_of_ptrs_to_worker_data[i]->is_ready == false){
                workers_are_ready = false;
                break;
            }
        }
        usleep(DELAY_MS);
    }

    while (!atomic_load(&quit)){
        if (atomic_load(data->abort)){ // abort everyone, who is working
            queue_clear(data->queue_of_work);
            for (int i = 0; i < data->num_of_workers; i++){
                data_compute_worker_t *worker_data = data->array_of_ptrs_to_worker_data[i];
                if (atomic_load(&worker_data->is_busy)) {
                    atomic_store(&worker_data->abort, true);
                }
            }
            atomic_store(data->abort, false); // everyone has been aborted
        }

        message *msg = queue_pop(data->queue_of_work);        
        if (msg == NULL){  // no work
            usleep(DELAY_MS);
            continue;
        }

        bool found_worker = false;

        while (!found_worker && !atomic_load(data->abort)){
            for (int i = 0; i < data->num_of_workers && !atomic_load(data->abort); i++){
                data_compute_worker_t *worker_data = data->array_of_ptrs_to_worker_data[i];
                if (atomic_load(&worker_data->is_busy)) continue;
#if DEBUG_MULTITHREADING                
                fprintf(stderr, "DEBUG: Giving chunk %d to worker thread %d.\n", msg->data.compute.cid,i);
#endif                                
                pthread_mutex_lock(&worker_data->lock);
                worker_data->work = *msg;
                pthread_cond_signal(&worker_data->cond);
                pthread_mutex_unlock(&worker_data->lock);
                found_worker = true;
                break;
            }
            usleep(DELAY_MS); // no free worker was found, try again later
        }
        free(msg);
    }    
    for (int i = 0; i < data->num_of_workers && !atomic_load(data->abort); i++){  // wake up sleeping workers
        data_compute_worker_t *worker_data = data->array_of_ptrs_to_worker_data[i];
        pthread_mutex_lock(&worker_data->lock);
        pthread_cond_signal(&worker_data->cond);
        pthread_mutex_unlock(&worker_data->lock);
    }
    return NULL;
}

static void *compute_worker(void *arg){
    data_compute_worker_t *data = (data_compute_worker_t *)arg;
    atomic_store(&data->is_ready, true);

    while (!atomic_load(&quit)){
        pthread_mutex_lock(&data->lock);
        while (!atomic_load(&quit) && data->work.type != MSG_COMPUTE){
#if DEBUG_MULTITHREADING            
            fprintf(stderr, "DEBUG: Worker waiting for work.\n");
#endif            
            pthread_cond_wait(&data->cond, &data->lock);
        }

#if DEBUG_MULTITHREADING            
            fprintf(stderr, "DEBUG: Worker has exited the waiting loop.\n");
#endif 

        message msg = data->work;  // copy the work assignment
        data->work.type = MSG_NBR; // to prevent unwanted calculations 
        pthread_mutex_unlock(&data->lock);
        if (atomic_load(&quit)) break;
        atomic_store(&data->is_busy, true);        

        uint8_t iters[msg.data.compute.n_re * msg.data.compute.n_im];
        complex double lower_left_corner = msg.data.compute.re + msg.data.compute.im * I, z;

        for (int row = 0, i = 0; row < msg.data.compute.n_im && !atomic_load(&data->abort) 
            && !atomic_load(&quit); row++){
            for (int col = 0; col < msg.data.compute.n_re && !atomic_load(&data->abort) 
                && !atomic_load(&quit); col++, i++){
                z = lower_left_corner + row*cimag(d)*I + col*creal(d);
                iters[i] = compute_one_pixel(z);
            }
        }

        if (atomic_load(&data->abort)){

#if DEBUG_MULTITHREADING
            fprintf(stderr, "DEBUG: Worker has aborted computation.\n");
#endif            
            atomic_store(&data->abort, false);
            atomic_store(&data->is_busy, false);
            continue;
        }

        message output = {.type = MSG_COMPUTE_DATA_BURST, .data.compute_data_burst = {
            .length = msg.data.compute.n_re * msg.data.compute.n_im, .chunk_id = msg.data.compute.cid, 
            .iters = iters}};

        send_message(&data->module_to_app->fd, output, &data->module_to_app->lock);

        send_done_message(&data->module_to_app->fd, &data->module_to_app->lock);

#if DEBUG_MULTITHREADING
            fprintf(stderr, "DEBUG: Worker has sent burst message and done message.\n");
#endif 

        atomic_store(&data->is_busy, false);
    }
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
        fprintf(stderr, "FATAL ERROR: Allocation failed.\n");
        exit(ERROR_ALLOCATION);
    }
    atomic_store(&quit, false);
    atomic_store(&data->abort, false);
    data->app_to_module.fd = -1;
    data->module_to_app.fd = -1;
    queue_t *queue= malloc(sizeof(queue_t));
    if (queue == NULL){
        fprintf(stderr, "FATAL ERROR: Allocation failed.\n");
        exit(ERROR_ALLOCATION);
    }
    queue_create(queue);
    data->queue_of_work = queue;
    pthread_mutex_init(&data->app_to_module.lock, NULL);
    pthread_mutex_init(&data->module_to_app.lock, NULL);
    return data;
}

// also initiates data for workers
static data_compute_boss_t *data_compute_boss_init(atomic_bool *abort, queue_t *queue_of_work, 
    uint8_t num_of_workers, data_t *module_to_app){
    data_compute_boss_t *data = malloc(sizeof(data_compute_boss_t));
    if (data == NULL){
        fprintf(stderr, "FATAL ERROR: Allocation failed.\n");
        exit(ERROR_ALLOCATION);
    }    
    data->abort = abort;
    data->queue_of_work = queue_of_work;
    data->num_of_workers = num_of_workers;
    data_compute_worker_t **workers_data = malloc(sizeof(data_compute_worker_t *) * num_of_workers);
    if (workers_data == NULL){
        fprintf(stderr, "FATAL ERROR: Allocation failed.\n");
        exit(ERROR_ALLOCATION);
    }
    for (int i = 0; i < num_of_workers; i++) {
        workers_data[i] = data_compute_worker_init(module_to_app);
    }
    data->array_of_ptrs_to_worker_data = workers_data;
    return data;
}

static data_compute_worker_t *data_compute_worker_init(data_t *module_to_app){
    data_compute_worker_t *data = malloc(sizeof(data_compute_worker_t));
    if (data == NULL){
        fprintf(stderr, "FATAL ERROR: Allocation failed.\n");
        exit(ERROR_ALLOCATION);
    }  
    atomic_store(&data->is_ready, false);
    atomic_store(&data->abort, false);
    atomic_store(&data->is_busy, false);
    data->module_to_app = module_to_app;
    data->work.type = MSG_NBR;
    data->work.data.compute.n_im = 0;
    data->work.data.compute.n_re = 0; 
    pthread_mutex_init(&data->lock, NULL);
    pthread_cond_init(&data->cond, NULL);
    return data;
}

static void destroy_shared_data(thread_shared_data_t *data, data_compute_boss_t *boss_data){
    pthread_mutex_destroy(&data->app_to_module.lock);
    pthread_mutex_destroy(&data->module_to_app.lock);
    queue_clear(data->queue_of_work);
    free(data->queue_of_work->q);
    free(data->queue_of_work);
    free(data);
    for (int i = 0; i < boss_data->num_of_workers; i++){
        pthread_mutex_destroy(&boss_data->array_of_ptrs_to_worker_data[i]->lock);
        pthread_cond_destroy(&boss_data->array_of_ptrs_to_worker_data[i]->cond);
        free(boss_data->array_of_ptrs_to_worker_data[i]);
    }
    free(boss_data->array_of_ptrs_to_worker_data);
    free(boss_data);
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
    fprintf(stderr, "\n============================= ARGUMENTS ============================\n");
    fprintf(stderr, "  argv[1] - Number of worker threads. Must be between 1 and 8 (default %d).\n", 
        DEFAULT_NUM_OF_WORKERS);
    fprintf(stderr, "  argv[2] - App to module named pipe path. Has to be opened beforehand.\n");
    fprintf(stderr, "  argv[3] - Module to app named pipe path. Has to be opened beforehand.\n");
    fprintf(stderr, "============================= COMMANDS =============================\n");
    fprintf(stderr, "  'q' - Quit module.\n"); 
    fprintf(stderr, "  'a' - Abort computation.\n");
    fprintf(stderr, "  'h' - Help message.\n");
    fprintf(stderr, "====================================================================\n\n");
}

static void computational_module_init(void){
    atexit(cleanup);
    call_termios(SET_TERMINAL_TO_RAW);
    fprintf(stderr, "INFO: Press 'h' for help.\n");
    signal(SIGPIPE, SIG_IGN);
    atomic_store(&quit, false);
}





