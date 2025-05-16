
#include "common_lib.h"
#include "control_app.h"
#include "queue.h"


static void *read_user_input(void* arg);
static void *read_from_pipe(void *arg);
static void cleanup(void);
static thread_shared_data_t *thread_shared_data_init(void);
static void destroy_shared_data(thread_shared_data_t *data);
static void control_app_init(void);
static void send_compute_message(thread_shared_data_t *data);
static void handle_message_compute_data(message msg);
static void queue_clear(void* queue);
static void* queue_pop(void *queue);
static void queue_push(void *queue, void *entry);

static uint8_t chunk_width = 64;
static uint8_t chunk_height = 48;
static uint8_t chunks_in_row = 10;
static uint8_t chunks_in_col = 10; 
static int width;
static int heigth;
static uint8_t *bitmap; 
static uint8_t num_of_iterations = 100;
static complex double lower_left_corner =  -1.6 - 1.1 * I;
static complex double upper_right_corner = 1.6 + 1.1 * I;
static complex double pixel_size;
static const complex double recurzive_eq_constant = -0.4 + 0.6 * I; 
static int window_state = WINDOW_NOT_INITIATED;
static void *queue_of_CIDs_to_be_computed;
static pthread_mutex_t queue_mtx;

int main(int argc, char *argv[]) {
    control_app_init();

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
    thread_shared_data_t *data = (thread_shared_data_t *)arg;
    int c;
    message msg;
    while (!data->quit){
        c = getchar();
        switch (c)
        {
        case 'q': 
            fprintf(stderr, "INFO: Quiting control application.\n");
            data->quit = true; 
            if (window_state == WINDOW_ACTIVE) {
                fprintf(stderr, "INFO: Closing window.\n");
                xwin_close();
            }            
            break;
        case 'g':
            if (data->app_to_module.fd == -1) break;
            fprintf(stderr, "INFO: Requesting module version.\n");
            msg.type = MSG_GET_VERSION;
            send_message(&data->app_to_module.fd, msg, &data->app_to_module.lock);
            break;
        case 's':
            if (data->app_to_module.fd == -1) break;
            fprintf(stderr, "INFO: Setting module computation data.\n");
            queue_clear(queue_of_CIDs_to_be_computed);
            msg.type = MSG_SET_COMPUTE;
            msg.data.set_compute.c_re = creal(recurzive_eq_constant);
            msg.data.set_compute.c_im = cimag(recurzive_eq_constant);
            msg.data.set_compute.d_re = creal(pixel_size);
            msg.data.set_compute.d_im = cimag(pixel_size);
            msg.data.set_compute.n = num_of_iterations;
            send_message(&data->app_to_module.fd, msg, &data->app_to_module.lock);
            break;
        case '1':
            if (data->app_to_module.fd == -1) break;
            send_compute_message(data);
            break;

#if DEBUG_GUI            
        case '2':
            fprintf(stderr, "INFO: Requesting singe chunk computation - debuging function.\n");
            msg.type = MSG_COMPUTE;
            msg.data.compute.cid = 5;
            msg.data.compute.re = -0.6;
            msg.data.compute.im = 0.0;
            msg.data.compute.n_re = chunk_width;
            msg.data.compute.n_im = chunk_height;
            send_message(&data->app_to_module.fd, msg, &data->app_to_module.lock);
            break;
#endif        
        case 'a':
            if (data->app_to_module.fd == -1) break;
            fprintf(stderr, "INFO: Requesting abortion.\n");
            queue_clear(queue_of_CIDs_to_be_computed);
            msg.type = MSG_ABORT;
            send_message(&data->app_to_module.fd, msg, &data->app_to_module.lock); 
            break;
        case 'w':
            if (window_state != WINDOW_NOT_INITIATED) {
                fprintf(stderr, "WARN: Window has already been initialized in this session.\n");
                break;
            } 
            fprintf(stderr, "INFO: Initializing window.\n");
            int r;
            if ((r = xwin_init(width, heigth))) {
                fprintf(stderr, "ERROR: Window inicialization failed with exit code %d.\n", r);
            } else {
                fprintf(stderr, "INFO: Window inicialization OK.\n");
                window_state = WINDOW_ACTIVE;
                xwin_redraw(width, heigth, bitmap);
            }
            break;
        case 'd':
            if (window_state != WINDOW_ACTIVE){
                fprintf(stderr, "WARN: Window is not active.\n");
                break;
            }
            fprintf(stderr, "INFO: Drawing window.\n");
            xwin_redraw(width, heigth, bitmap);
            break;
        case 'c':
            if (window_state != WINDOW_ACTIVE) {
                fprintf(stderr, "WARN: Window is not active.\n");
            }
            fprintf(stderr, "INFO: Closing window.\n");
            window_state = WINDOW_CLOSED;
            xwin_close();
            break;
        case 'l':
            fprintf(stderr, "INFO: Cleared bitmap buffer.\n");
            memset(bitmap, 0x00, width * heigth * 3);
            if (window_state == WINDOW_ACTIVE) xwin_redraw(width, heigth, bitmap);
            break;  
        default:
            break;
        }
    }
    return NULL;
}

static void *read_from_pipe(void *arg){
    thread_shared_data_t *data = (thread_shared_data_t *)arg;
    
    while (data->module_to_app.fd == -1 && !data->quit) {
        usleep(DELAY_MS * 1000); // waiting for pipe to be joined
    }    

    message msg;

    while(!data->quit){
        if (!recieve_message(data->module_to_app.fd, &msg, DELAY_MS, &data->module_to_app.lock)) continue;
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
            handle_message_compute_data(msg);
#if DEBUG_COMPUTATIONS            
            fprintf(stderr, "DEBUG: Modul returned computed data.\n");
            fprintf(stderr, "DEBUG: cid = %d, i_re = %d, i_im = %d, iter = %d.\n",
                msg.data.compute_data.cid, msg.data.compute_data.i_re, msg.data.compute_data.i_im,
                msg.data.compute_data.iter);
#endif            
            break;
            
        case MSG_DONE:
            fprintf(stderr, "INFO: Modul is done with computing.\n");
            if (data->app_to_module.fd == -1) break;
            message *tmp = queue_pop(queue_of_CIDs_to_be_computed);
            if (tmp != NULL) {
                send_message(&data->app_to_module.fd, *tmp, &data->app_to_module.lock);
                free(tmp);
            }
            if (window_state == WINDOW_ACTIVE) xwin_redraw(width, heigth, bitmap);
            break;
        case MSG_ABORT:
            fprintf(stderr, "INFO: Modul has aborted computation.\n");
            queue_clear(queue_of_CIDs_to_be_computed);
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
    free(bitmap);
    queue_clear(queue_of_CIDs_to_be_computed);
    free(queue_of_CIDs_to_be_computed);
}

static void control_app_init(void){
    call_termios(SET_TERMINAL_TO_RAW);
    atexit(cleanup);
    queue_of_CIDs_to_be_computed = create();
    setClear(queue_of_CIDs_to_be_computed, free);
    pthread_mutex_init(&queue_mtx, NULL);
    width = chunk_width * chunks_in_row;
    heigth = chunk_height * chunks_in_col;
    pixel_size = (creal(upper_right_corner) - creal(lower_left_corner)) / width + 
        ((cimag(upper_right_corner) - cimag(lower_left_corner)) / heigth) * I;
    if ((bitmap = malloc(width * heigth * 3 * sizeof(uint8_t))) == NULL) {
        fprintf(stderr, "ERROR: Allocation of bitmap failed.\n");
        exit(ERROR_ALLOCATION);
    };
    signal(SIGPIPE, SIG_IGN);
}

static void send_compute_message(thread_shared_data_t *data){
    fprintf(stderr, "INFO: Requesting module computation.\n");
    queue_clear(queue_of_CIDs_to_be_computed);
    complex double first_chunk_corner = lower_left_corner + 
        ((chunks_in_col - 1) * chunk_height * cimag(pixel_size)) * I;
    for (int c_row = 0; c_row < chunks_in_col; c_row++){
        for (int c_col = 0; c_col < chunks_in_row; c_col++){
            message *msg;
            if ((msg = malloc(sizeof(message))) == NULL){
                fprintf(stderr, "ERROR: Allocation of message to request the computation of chunk %d failed.\n",
                    c_row * chunks_in_row + c_col);
                    continue;
            }
            msg->type = MSG_COMPUTE;
            msg->data.compute.cid = c_row * chunks_in_row + c_col;
            msg->data.compute.re = creal(first_chunk_corner) + c_col * chunk_width * creal(pixel_size);
            msg->data.compute.im = cimag(first_chunk_corner) - c_row * chunk_height * cimag(pixel_size);
            msg->data.compute.n_re = chunk_width;
            msg->data.compute.n_im = chunk_height;
            queue_push(queue_of_CIDs_to_be_computed, msg);
        }
    }
    message *tmp = queue_pop(queue_of_CIDs_to_be_computed);
    if (tmp != NULL) {
        send_message(&data->app_to_module.fd, *tmp, &data->app_to_module.lock);
        free(tmp);
    }
}

static void handle_message_compute_data(message msg){
    int chunk_row = msg.data.compute_data.cid / chunks_in_row;
    int chunk_col = msg.data.compute_data.cid % chunks_in_row;

    int row = chunk_row * chunk_height + (chunk_height - 1) - msg.data.compute_data.i_im;
    int col = chunk_col * chunk_width + msg.data.compute_data.i_re;
    int idx = (row * width + col) * 3;
    double t = (double)msg.data.compute_data.iter / num_of_iterations;
    uint8_t red = (uint8_t) 9 * (1 - t) * t * t * t * 255;
    uint8_t green = (uint8_t) 15 * (1 - t) * (1 - t) * t * t * 255;
    uint8_t blue = (uint8_t) 8.5 * (1 - t) * (1 - t) * (1 - t) * t * 255;
    bitmap[idx] = red;
    bitmap[idx + 1] = green;
    bitmap[idx + 2] = blue;
}

static void queue_clear(void *queue){
    pthread_mutex_lock(&queue_mtx);
    clear(queue);
    pthread_mutex_unlock(&queue_mtx);
}

static void* queue_pop(void *queue){
    pthread_mutex_lock(&queue_mtx);
    void * tmp = pop(queue);
    pthread_mutex_unlock(&queue_mtx);
    return tmp;
}

static void queue_push(void *queue, void *entry){
    pthread_mutex_lock(&queue_mtx);
    push(queue, entry);
    pthread_mutex_unlock(&queue_mtx);
}

