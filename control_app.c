
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
static void handle_message_compute_data_burst(message msg);
static void queue_clear(void* queue);
static void* queue_pop(void *queue);
static void queue_push(void *queue, void *entry);
static void close_window_safe(void);
static void redraw_window_safe(void);
static void open_window_safe(void);
static void print_help(void);
static void open_parameters_settings(thread_shared_data_t *data);
static void print_settings_menu(void);
static void clear_settings_menu(int lines);
static void set_chunk_size(void);
static void set_chunks_in_row_col(void);
static void set_num_iterations(void);
static void set_lower_left_corner(void);
static void set_upper_right_corner(void);
static void set_recurzive_constant(void);
static void calculate_window_parameters(void);

static uint8_t chunk_width = 64;
static uint8_t chunk_height = 48;
static uint8_t chunks_in_row = 16;
static uint8_t chunks_in_col = 16; 
static int width;
static int heigth;
static uint8_t *bitmap; 
static uint8_t num_of_iterations = 100;
static complex double lower_left_corner =  -1.6 - 1.1 * I;
static complex double upper_right_corner = 1.6 + 1.1 * I;
static complex double pixel_size;
static complex double recurzive_eq_constant = -0.4 + 0.6 * I; 
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
    uint8_t c;
    int r;
    message msg;
    while (!data->quit){
        if ((r = io_getc_timeout(STDIN_FILENO, DELAY_MS, &c)) == -1){
            fprintf(stderr, "ERROR: io_getc_timeout() from stdin failed: %s\n", strerror(errno));
            continue;
        } else if (r == 0){
            continue; // no character read
        }
        switch (c)
        {
        case 'q': 
            if (data->app_to_module.fd != -1){
                fprintf(stderr, "INFO: Quiting control application.\n");
                msg.type = MSG_QUIT;
                send_message(&data->app_to_module.fd, msg, &data->app_to_module.lock);   
            }
            fprintf(stderr, "INFO: Quiting module.\n");
            atomic_store(&data->quit, true);
            close_window_safe();          
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
            open_window_safe();
            break;
        case 'r':
            redraw_window_safe();
            break;
        case 'c':
            close_window_safe();
            break;
        case 'e':
            fprintf(stderr, "INFO: Cleared bitmap buffer.\n");
            memset(bitmap, 0x00, width * heigth * 3);
            if (window_state == WINDOW_ACTIVE) xwin_redraw(width, heigth, bitmap);
            break;  
        case 'h':
            print_help();
            break;
        case 'p':
            open_parameters_settings(data);
            break;
        default:
            break;
        }
    }
    return NULL;
}


static void *read_from_pipe(void *arg){
    thread_shared_data_t *data = (thread_shared_data_t *)arg;
    
    while (data->module_to_app.fd == -1 && !atomic_load(&data->quit)) {
        usleep(DELAY_MS * 1000); // waiting for pipe to be joined
    }    

    message msg;

    while(!atomic_load(&data->quit)){
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
        case MSG_COMPUTE_DATA_BURST:
            handle_message_compute_data_burst(msg);
#if DEBUG_COMPUTATIONS
            fprintf(stderr, "DEBUG: Modul returned computed data in burst for "
                "chunk %d.\n", msg.data.compute_data_burst.chunk_id);
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
            redraw_window_safe();
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
    atomic_store(&data->quit, false);
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
    calculate_window_parameters();
    if ((bitmap = malloc(width * heigth * 3 * sizeof(uint8_t))) == NULL) {
        fprintf(stderr, "ERROR: Allocation of bitmap failed.\n");
        exit(ERROR_ALLOCATION);
    };
    signal(SIGPIPE, SIG_IGN);
    fprintf(stderr, "INFO: Press 'h' for help.\n");
}

static void calculate_window_parameters(void){
    width = chunk_width * chunks_in_row;
    heigth = chunk_height * chunks_in_col;
    pixel_size = (creal(upper_right_corner) - creal(lower_left_corner)) / width + 
        ((cimag(upper_right_corner) - cimag(lower_left_corner)) / heigth) * I;
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

static void handle_message_compute_data_burst(message msg){
    int chunk_row = msg.data.compute_data.cid / chunks_in_row;
    int chunk_col = msg.data.compute_data.cid % chunks_in_row;
    int lower_left_corner_row = (chunk_row + 1) * chunk_height - 1;
    int lower_left_corner_col = chunk_col * chunk_width;
    int row, col, idx;
    double t;
    uint8_t red, green, blue;
    for (int i = 0; i < msg.data.compute_data_burst.length; i++){
        row = lower_left_corner_row - i / chunk_width;
        col = lower_left_corner_col + i % chunk_width;
        idx = (row * width + col) * 3;
        t = (double)msg.data.compute_data_burst.iters[i] / num_of_iterations;
        red = (uint8_t) 9 * (1 - t) * t * t * t * 255;
        green = (uint8_t) 15 * (1 - t) * (1 - t) * t * t * 255;
        blue = (uint8_t) 8.5 * (1 - t) * (1 - t) * (1 - t) * t * 255;
        bitmap[idx] = red;
        bitmap[idx + 1] = green;
        bitmap[idx + 2] = blue;
    }
    free(msg.data.compute_data_burst.iters);
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


static void close_window_safe(void){
    if (window_state != WINDOW_ACTIVE) {
        return;
    }
    fprintf(stderr, "INFO: Closing window.\n");
    window_state = WINDOW_CLOSED;
    xwin_close();
}
static void redraw_window_safe(void){
    calculate_window_parameters();
    if (window_state != WINDOW_ACTIVE){
        return;
    }
    fprintf(stderr, "INFO: Drawing window.\n");
    xwin_redraw(width, heigth, bitmap);
}

static void open_window_safe(void){
    if (window_state != WINDOW_NOT_INITIATED) {
        fprintf(stderr, "WARN: Window has already been initialized in this session.\n");
        return;
    } 
    calculate_window_parameters();
    fprintf(stderr, "INFO: Initializing window.\n");
    int r;
    if ((r = xwin_init(width, heigth))) {
        fprintf(stderr, "ERROR: Window inicialization failed with exit code %d.\n", r);
    } else {
        fprintf(stderr, "INFO: Window inicialization OK.\n");
        window_state = WINDOW_ACTIVE;
        xwin_redraw(width, heigth, bitmap);
    }
}

static void print_help(void){
    fprintf(stderr, "\n============================= COMMANDS =============================\n");
    fprintf(stderr, "  'q' - Quit application and module.\n");
    fprintf(stderr, "  'g' - Get module version.\n");
    fprintf(stderr, "  'p' - Parameters for computation settings.\n");
    fprintf(stderr, "  's' - Set module computation parameners.\n");
    fprintf(stderr, "  '1' - Run computation. Computation parameters must be set prior.\n");
#if DEBUG_GUI
    fprintf(stderr, " '2' - Compute chunk 5. Debuging function. Computation parameters must be set prior.\n");
#endif    
    fprintf(stderr, "  'a' - Abort computation.\n");
    fprintf(stderr, "  'w' - Initialize window.\n");
    fprintf(stderr, "  'r' - Redraw window with current buffer.\n");
    fprintf(stderr, "  'c' - Close window.\n");
    fprintf(stderr, "  'e' - Erase buffer.\n");
    fprintf(stderr, "  'h' - Help message.\n");
    fprintf(stderr, "====================================================================\n\n");
}

static void open_parameters_settings(thread_shared_data_t *data){
    print_settings_menu();
    fprintf(stderr, "\n\n\033[1A\033[2K\033[1A\033[2K");
    uint8_t c;
    int r;
    bool reprint;
    while (!data->quit){        
        if ((r = io_getc_timeout(STDIN_FILENO, DELAY_MS, &c)) == -1){
            fprintf(stderr, "ERROR: io_getc_timeout() from stdin failed: %s\n", strerror(errno));
            continue;
        } else if (r == 0){
            continue; // no character read
        }
        reprint = false;
        switch (c){
            case 'q':
                clear_settings_menu(11);
                calculate_window_parameters();
                return;
            case '1':
                if (window_state != WINDOW_NOT_INITIATED) break; 
                clear_settings_menu(11);
                set_chunk_size();
                reprint = true;
                break;
            case '2':
                if (window_state != WINDOW_NOT_INITIATED) break;
                clear_settings_menu(11);
                set_chunks_in_row_col();
                reprint = true;
                break;
            case '3':
                clear_settings_menu(11);
                set_num_iterations();
                reprint = true;
                break;
            case '4':
               clear_settings_menu(11);
                set_lower_left_corner();
                reprint = true;
                break;
            case '5':
                clear_settings_menu(11);
                set_upper_right_corner();
                reprint = true;
                break;
            case '6':
                clear_settings_menu(11);
                set_recurzive_constant();
                reprint = true;
                break;    
            default:
                break;
        }
        if (reprint){
            print_settings_menu();
        }
    }
}

static void print_settings_menu(void){
    fprintf(stderr, "\n============================= SETTINGS =============================\n");
    fprintf(stderr, "  'q' - Quit settings.\n");
    if (window_state == WINDOW_NOT_INITIATED){
        fprintf(stderr, "  '1' - Chunk width and height (currently %d x %d).\n", chunk_width, chunk_height);
        fprintf(stderr, "  '2' - Number of chunks in row and in column (currently %d in row and %d in column).\n",
            chunks_in_row, chunks_in_col);
    } else {
        fprintf(stderr, "  '1' - Not available - window has been opened.\n");
        fprintf(stderr, "  '2' - Not available - window has been opened.\n");
    }
    fprintf(stderr, "  '3' - Maximal number of iterations of recurzive eqation (currently %d).\n", num_of_iterations); 
    fprintf(stderr, "  '4' - Complex value of lower left corner (currenty %.4f %+.4fi)\n", 
        creal(lower_left_corner), cimag(lower_left_corner));
    fprintf(stderr, "  '5' - Complex value of upper right corner (currenty %.4f %+.4fi)\n", 
        creal(upper_right_corner), cimag(upper_right_corner));    
    fprintf(stderr, "  '6' - Additive constant in recurzive eqation (currenty %.4f %+.4fi)\n", 
        creal(recurzive_eq_constant), cimag(recurzive_eq_constant)); 
    fprintf(stderr, "====================================================================\n\n");
}

static void clear_settings_menu(int lines){
for (int i = 0; i < lines; ++i) {
    fprintf(stderr, "\033[1A");
    fprintf(stderr, "\033[2K");
    }
}

static void set_chunk_size(){
    fprintf(stderr, "\n============================= SETTINGS =============================\n");
    fprintf(stderr, "Enter chunk width in pixels and chunk height in pixels. Value must be \n");
    fprintf(stderr, "between 1 and 255.\n");
    fprintf(stderr, "\n");    
    fprintf(stderr, "Current chunk width = %d\n", chunk_width);
    fprintf(stderr, "Current chunk height = %d\n", chunk_height);
    fprintf(stderr, "\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "====================================================================\n\n");
    call_termios(SET_TERMINAL_TO_DEFAULT);
    int new_width, new_height;
    if (scanf("%d", &new_width) && new_width > 0 && new_width < 256) {
        chunk_width = new_width;
    }

    if (scanf("%d", &new_height) && new_height > 0 && new_height < 256){
        chunk_height = new_height;
    }
    call_termios(SET_TERMINAL_TO_RAW);
    clear_settings_menu(13);
}

static void set_chunks_in_row_col(void){
    fprintf(stderr, "\n============================= SETTINGS =============================\n");
    fprintf(stderr, "Enter number of chunks in one row and number of chunks in one column.  \n");
    fprintf(stderr, "Value must be between 1 and 16.\n");
    fprintf(stderr, "\n");    
    fprintf(stderr, "Current number of chunks in one row = %d\n", chunks_in_row);
    fprintf(stderr, "Current number of chunks in one colunm = %d\n", chunks_in_col);
    fprintf(stderr, "\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "====================================================================\n\n");
    call_termios(SET_TERMINAL_TO_DEFAULT);
    int new_in_row, new_in_col;
    if (scanf("%d", &new_in_row) && new_in_row > 0 && new_in_row <= 16) {
        chunks_in_row = new_in_row;
    }

    if (scanf("%d", &new_in_col) && new_in_col > 0 && new_in_col <= 16){
        chunks_in_col = new_in_col;
    }
    call_termios(SET_TERMINAL_TO_RAW);
    clear_settings_menu(13);
}

static void set_num_iterations(void){
    fprintf(stderr, "\n============================= SETTINGS =============================\n");
    fprintf(stderr, "Enter maximal number of iterations of recurzive eqation\n");
    fprintf(stderr, "Value must be between 1 and 255.\n");
    fprintf(stderr, "\n");    
    fprintf(stderr, "Current maximal number of iterations = %d\n", num_of_iterations);
    fprintf(stderr, "\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "====================================================================\n\n");
    call_termios(SET_TERMINAL_TO_DEFAULT);
    int new_iters;
    if (scanf("%d", &new_iters) && new_iters > 0 && new_iters < 256) {
        num_of_iterations = new_iters;
    }
    call_termios(SET_TERMINAL_TO_RAW);
    clear_settings_menu(12);
}
static void set_lower_left_corner(void){
    fprintf(stderr, "\n============================= SETTINGS =============================\n");
    fprintf(stderr, "Enter complex value of pixel in lower left corner.\n");
    fprintf(stderr, "First enter real part and than imaginary part\n");
    fprintf(stderr, "Real part must be between %.4f and %.4f.\n", -5., creal(upper_right_corner));
    fprintf(stderr, "Complex part must be between %.4f and %.4f.\n", -5., cimag(upper_right_corner));    
    fprintf(stderr, "\n");
    fprintf(stderr, "Current value of lower left corner is %.4f %+.4fi\n", creal(lower_left_corner), 
        cimag(lower_left_corner));
    fprintf(stderr, "\n");
    fprintf(stderr, "====================================================================\n\n");
    call_termios(SET_TERMINAL_TO_DEFAULT);
    double new_re, new_im;
    if (scanf("%lf", &new_re) && new_re >= -5 && new_re < creal(upper_right_corner)) {
        lower_left_corner = new_re + cimag(lower_left_corner) * I;
    }

    if (scanf("%lf", &new_im) && new_im >= -5 && new_im < cimag(upper_right_corner)) {
        lower_left_corner = new_im * I + creal(lower_left_corner);
    }
    call_termios(SET_TERMINAL_TO_RAW);
    clear_settings_menu(13);
}

static void set_upper_right_corner(void){
    fprintf(stderr, "\n============================= SETTINGS =============================\n");
    fprintf(stderr, "Enter complex value of pixel in upper right corner.\n");
    fprintf(stderr, "First enter real part and than imaginary part\n");
    fprintf(stderr, "Real part must be between %.4f and %.4f.\n", creal(lower_left_corner), 5.);
    fprintf(stderr, "Complex part must be between %.4f and %.4f.\n",cimag(lower_left_corner), 5.);    
    fprintf(stderr, "\n");
    fprintf(stderr, "Current value of upper right corner is %.4f %+.4fi\n", creal(upper_right_corner), 
        cimag(upper_right_corner));
    fprintf(stderr, "\n");
    fprintf(stderr, "====================================================================\n\n");
    call_termios(SET_TERMINAL_TO_DEFAULT);
    double new_re, new_im;
    if (scanf("%lf", &new_re) && new_re > creal(lower_left_corner) && new_re <= 5) {
        upper_right_corner = new_re + cimag(upper_right_corner) * I;
    }

    if (scanf("%lf", &new_im) && new_im > cimag(lower_left_corner) && new_im <= 5) {
        upper_right_corner = new_im * I + creal(upper_right_corner);
    }
    call_termios(SET_TERMINAL_TO_RAW);
    clear_settings_menu(13);
}

static void set_recurzive_constant(void){
    fprintf(stderr, "\n============================= SETTINGS =============================\n");
    fprintf(stderr, "Enter complex value of additive constant in recurzive equation.\n");
    fprintf(stderr, "First enter real part and than imaginary part\n");
    fprintf(stderr, "Real part must be between %.4f and %.4f.\n", -2., 2.);
    fprintf(stderr, "Complex part must be between %.4f and %.4f.\n", -2., 2.);    
    fprintf(stderr, "\n");
    fprintf(stderr, "Current value of additive constant is %.4f %+.4fi\n", creal(recurzive_eq_constant), 
        cimag(recurzive_eq_constant));
    fprintf(stderr, "\n");
    fprintf(stderr, "====================================================================\n\n");
    call_termios(SET_TERMINAL_TO_DEFAULT);
    double new_re, new_im;
    if (scanf("%lf", &new_re) && new_re >= -2 && new_re <= 2) {
        recurzive_eq_constant = new_re + cimag(recurzive_eq_constant) * I;
    }

    if (scanf("%lf", &new_im) && new_im > -2 && new_im <= 2) {
        recurzive_eq_constant = new_im * I + creal(recurzive_eq_constant);
    }
    call_termios(SET_TERMINAL_TO_RAW);
    clear_settings_menu(13);
}


