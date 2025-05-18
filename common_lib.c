
#include "common_lib.h"

static void clear_pipe(int fd);

void call_termios(int reset) {
    static struct termios tio, tioOld;
    static int stdin_flags = -1;
    static bool has_made_default_backup = false;

    if (reset) {
        if (has_made_default_backup){
            tcsetattr(STDIN_FILENO, TCSANOW, &tioOld);
            if (stdin_flags != -1){ // unset nonblock
                fcntl(STDIN_FILENO, F_SETFL, stdin_flags);
            }
        }
    }
    else {
        if (!has_made_default_backup){ // make backup
            tcgetattr(STDIN_FILENO, &tioOld);
            stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0); 
            has_made_default_backup = true;
        }
        tcgetattr(STDIN_FILENO, &tio);
        cfmakeraw(&tio);
        tio.c_oflag |= OPOST;
        tcsetattr(STDIN_FILENO, TCSANOW, &tio);
        stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK); //set nonblock
    }
}


// return true on succes, returns false if quit flag is raised before someone joins the other end of pipe,
// exits on opening error
bool open_pipes(data_t *in, data_t *out, atomic_bool *quit, const char *in_pipe_name, const char *out_pipe_name){

    pthread_mutex_lock(&in->lock);
    if ((in->fd = io_open_read(in_pipe_name)) == -1){
        pthread_mutex_unlock(&in->lock);
        fprintf(stderr, "ERROR: Cannot open named pipe port '%s'\n", in_pipe_name);
        atomic_store(quit, true);
        call_termios(SET_TERMINAL_TO_DEFAULT);
        exit(ERROR_OPENING_PIPE);
    } else {
        clear_pipe(in->fd);
        pthread_mutex_unlock(&in->lock);
        fprintf(stderr, "INFO: Named pipe port '%s' (FD %d) opened succesfully for reading\n", 
            in_pipe_name, in->fd);
    }

    fprintf(stderr, "INFO: Waiting for someone to join pipe port '%s' as a reader\n", out_pipe_name); 
    int tmp = -1;
    while (!atomic_load(quit)){
        tmp = open(out_pipe_name, O_WRONLY | O_NONBLOCK | O_NOCTTY | O_SYNC);
        if (tmp != -1) break;
        if (errno != ENXIO) {
            fprintf(stderr, "ERROR: Cannot open named pipe port '%s': %s\n", 
                out_pipe_name, strerror(errno));
                atomic_store(quit, true);
                exit(ERROR_OPENING_PIPE);
            }
        usleep(10000);
        }
    if (atomic_load(quit)) {
#if DEBUG_PIPES
        fprintf(stderr, "DEBUG: open_pipes() is returning early, because of quit flags.\n");
#endif        
        return false;
    }
    pthread_mutex_lock(&out->lock);
    out->fd = tmp;
    pthread_mutex_unlock(&out->lock);
    fprintf(stderr, "INFO: Named pipe port '%s' (FD %d) opened succesfully for writing\n", 
        out_pipe_name, out->fd);
    return true;  
}

bool send_message(int *fd, message msg, pthread_mutex_t *fd_lock){
    size_t buffer_size = msg.type != MSG_COMPUTE_DATA_BURST ? sizeof(message) : 
        msg.data.compute_data_burst.length + 5;
    uint8_t buffer[buffer_size];
    int msg_size;

    if (!get_message_size(&msg, &msg_size)){ // Only to check validity of message type
        return false;
    }

    if (!fill_message_buf(&msg, buffer, buffer_size, &msg_size)){
        fprintf(stderr, "ERROR: Serializing message of type %d failed.\n", msg.type);
        return false;
    }

    if (*fd < 0){
        fprintf(stderr, "WARN: File descriptor is (%d).\n", *fd);
    }

    pthread_mutex_lock(fd_lock);

#if DEBUG_MUTEX 
    fprintf(stderr, "DEBUG: Locked mutex of FD %d at %p.\n", fd, (void *) fd_lock);
#endif
    size_t total_written = 0, written;
    int retries = 1000; 

    while (total_written < (size_t)msg_size && retries-- > 0){
        written = write(*fd, buffer + total_written, msg_size - total_written);
        if (written > 0){
            total_written += written;
            continue;
        }
        else if (written == -1 && errno == EAGAIN) {
            usleep(10); // pipe is full, wait and try again
            continue;    
        }
        else if (written == -1 && errno == EPIPE) {
            fprintf(stderr, "WARN: Reader disconected. \n");
            *fd = -1;
            pthread_mutex_unlock(fd_lock);
#if DEBUG_MUTEX 
            fprintf(stderr, "DEBUG: Unlocked mutex of FD %d at %p.\n", fd, (void *) fd_lock);
#endif
            return false;
        }
        else if (written == -1){
            fprintf(stderr, "ERROR: write() failed. : %s.\n", strerror(errno));
            pthread_mutex_unlock(fd_lock);
#if DEBUG_MUTEX 
            fprintf(stderr, "DEBUG: Unlocked mutex of FD %d at %p.\n", fd, (void *) fd_lock);
#endif
            return false;
        }        
    }

    pthread_mutex_unlock(fd_lock);
#if DEBUG_MUTEX 
    fprintf(stderr, "DEBUG: Unlocked mutex of FD %d at %p.\n", fd, (void *) fd_lock);
#endif

    if (total_written < (size_t)msg_size){
        fprintf(stderr, "ERROR: write() wrote only %d/%d after retries.\n", 
            (int)total_written, msg_size);
        return false;
    }

#if DEBUG_MESSAGES 
    fprintf(stderr, "DEBUG: Message of type %d successfully sent in %d bytes.\n", 
        msg.type, msg_size);
#endif

    return true;
}

bool recieve_message(int fd, message *out_msg, int timeout_ms, pthread_mutex_t *fd_lock){
    uint8_t msg_type;
    int bytes_read = 0;

    if (fd == -1){
        fprintf(stderr, "ERROR: cannot recieve from fd = -1.\n");
        return false;
    }

    pthread_mutex_lock(fd_lock);
    if (io_getc_timeout(fd, timeout_ms, &msg_type) == 0){
        pthread_mutex_unlock(fd_lock);
        return false; // no message to be read
    }
    bytes_read++;

    int msg_size;
    message tmp = {.type = msg_type}; 
    if (tmp.type == MSG_COMPUTE_DATA_BURST) {
        uint8_t burst_lenght[2];
        if(!io_read_timeout(fd, burst_lenght, 2, timeout_ms)){
            pthread_mutex_unlock(fd_lock);
            fprintf(stderr, "ERROR: Couldnt read the 2 bytes to determine the lenght of "
            "burst message.\n");
            return false;
        }
        bytes_read += 2;
        memcpy(&tmp.data.compute_data_burst.length, burst_lenght, 2);
        memcpy(&out_msg->data.compute_data_burst.length,burst_lenght, 2); 
    }


    if (!get_message_size(&tmp, &msg_size)){
        pthread_mutex_unlock(fd_lock);
        fprintf(stderr, "ERROR: Recieved message of unknown type: %d.\n", msg_type);
        return false;
    }
    uint8_t buffer[msg_size];
    buffer[0] = msg_type;
    if (msg_type == MSG_COMPUTE_DATA_BURST) memcpy(&buffer[1], &tmp.data.compute_data_burst.length, 
        sizeof(uint16_t));

    if (io_read_timeout(fd, buffer + bytes_read, msg_size - bytes_read, timeout_ms) != 1){
        return false;
    }

    pthread_mutex_unlock(fd_lock);

    if (!parse_message_buf(buffer, msg_size, out_msg)){
        fprintf(stderr, "ERROR: Parsing message of type %d failed.\n", buffer[0]);
        return false;
    }

#if DEBUG_MESSAGES
    fprintf(stderr, "DEBUG: Message of type %d succesfully recieved in %d bytes.\n", out_msg->type, msg_size);
#endif

    return true;
}

void join_all_threads(int N, thread_t threads[N]){
    int r;
    for (int i = 0; i < N; i++){
        if ((r = pthread_join(threads[i].thread, NULL)) == ERROR_OK){
            fprintf(stderr, "INFO: Succesfully joined '%s' thread.\n", threads[i].thread_name);
        } else {
            fprintf(stderr, "ERROR: Joining thread %s failed : %s\n", threads[i].thread_name, strerror(r));
        }
    }
}

int create_all_threads(int N, thread_t threads[N]){
    int ret = ERROR_OK;
    for (int i = 0; i < N; i++){
        if ((ret = pthread_mutex_init(&threads[i].lock, NULL)) != ERROR_OK) {
            fprintf(stderr, "ERROR: Initialization of mutex in '%s' thread failed.\n", threads[i].thread_name);
            return ERROR_CREATING_THREADS;
        };
        if ((ret = pthread_create(&threads[i].thread, NULL, threads[i].thread_function, threads[i].data)) != ERROR_OK){
            fprintf(stderr, "ERROR: Creating thread '%s' failed.\n", threads[i].thread_name);
            return ERROR_CREATING_THREADS;
        } else {
            fprintf(stderr, "INFO: Succesfully created '%s' thread.\n", threads[i].thread_name);
        }
    }
    return ret;
}

static void clear_pipe(int fd){ // assumes caller function still holds mutex to the pipe. This function
                                // is to be called right after a pipe was opened for reading
    uint8_t garbage[GARBAGE_BUFFER_SIZE];
    int r;

    while ((r = read(fd, garbage, sizeof(garbage))) > 0) ; // clear all leftover data in pipe
}

void queue_create(queue_t *queue){
    queue->q = create();
    if (queue->q == NULL) {
        fprintf(stderr, "FATAL ERROR: Allocation of queue failed.\n");
        exit(ERROR_ALLOCATION);
    }
    setClear(queue->q, free);
    pthread_mutex_init(&queue->lock, NULL);
}

void queue_clear(queue_t *queue){    
    pthread_mutex_lock(&queue->lock);
#if DEBUG_MULTITHREADING
    fprintf(stderr, "DEBUG: Clearing queue.\n");
#endif
    clear(queue->q);
    pthread_mutex_unlock(&queue->lock);
}

void* queue_pop(queue_t *queue){
    pthread_mutex_lock(&queue->lock);
    void *tmp = pop(queue->q);
    pthread_mutex_unlock(&queue->lock);
    return tmp;
}

void queue_push(queue_t *queue, void *entry){
    pthread_mutex_lock(&queue->lock);
    push(queue->q, entry);
    pthread_mutex_unlock(&queue->lock);
}

void queue_destroy(queue_t *queue){
    pthread_mutex_lock(&queue->lock);
    clear(queue->q);
    free(queue->q);
    pthread_mutex_unlock(&queue->lock);
    pthread_mutex_destroy(&queue->lock);
}

int queue_size(queue_t *queue){
    pthread_mutex_lock(&queue->lock);
    int r = size(queue->q);
    pthread_mutex_unlock(&queue->lock);
    return r;
}


