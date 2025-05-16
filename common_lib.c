#include "common_lib.h"

void call_termios(int reset) {
    static struct termios tio, tioOld;
    tcgetattr(STDIN_FILENO, &tio);
    if (reset) {
        tcsetattr(STDIN_FILENO, TCSANOW, &tioOld);
    }
    else {
        tioOld = tio; // backup
        cfmakeraw(&tio);
        tio.c_oflag |= OPOST;
        tcsetattr(STDIN_FILENO, TCSANOW, &tio);
    }
}


// return true on succes, returns false if quit flag is raised before someone joins the other end of pipe,
// exits on opening error
bool open_pipes(data_t *in, data_t *out, bool *quit, const char *in_pipe_name, const char *out_pipe_name){

    pthread_mutex_lock(&in->lock);
    if ((in->fd = io_open_read(in_pipe_name)) == -1){
        pthread_mutex_unlock(&in->lock);
        fprintf(stderr, "ERROR: Cannot open named pipe port '%s'\n", in_pipe_name);
        *quit = true;
        call_termios(SET_TERMINAL_TO_DEFAULT);
        exit(ERROR_OPENING_PIPE);
    } else {
        pthread_mutex_unlock(&in->lock);
        fprintf(stderr, "INFO: Named pipe port '%s' (FD %d) opened succesfully for reading\n", 
            in_pipe_name, in->fd);
    }

    fprintf(stderr, "INFO: Waiting for someone to join pipe port '%s' as a reader\n", out_pipe_name); 
    int tmp = -1;
    while (!*quit){
        tmp = open(out_pipe_name, O_WRONLY | O_NONBLOCK | O_NOCTTY | O_SYNC);
        if (tmp != -1) break;
        if (errno != ENXIO) {
            fprintf(stderr, "ERROR: Cannot open named pipe port '%s': %s\n", 
                out_pipe_name, strerror(errno));
                *quit = true;
                exit(ERROR_OPENING_PIPE);
            }
        usleep(10000);
        }
    if (*quit) {
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
    const size_t buffer_size = sizeof(message);
    uint8_t buffer[buffer_size];
    int msg_size;

    if (!get_message_size(&msg, &msg_size)){
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
    const size_t buffer_size = sizeof(message);
    uint8_t buffer[buffer_size];
    int bytes_read = 0;

    if (fd == -1){
        fprintf(stderr, "ERROR: cannot recieve from fd = -1.\n");
        return false;
    }

    pthread_mutex_lock(fd_lock);
    if (io_getc_timeout(fd, timeout_ms, &buffer[bytes_read++]) == 0){
        pthread_mutex_unlock(fd_lock);
        return false; // no message to be read
    }

    int msg_size;
    message tmp = {.type = buffer[0]}; 
    if (tmp.type == MSG_COMPUTE_DATA_BURST) {
        uint8_t burst_lenght[2];
        if(io_read_timeout(fd, burst_lenght, 2, timeout_ms)){
            fprintf(stderr, "ERROR: Couldnt read the 2 bytes to determine the lenght of "
            "burst message.\n");
            return false;
        }
        bytes_read += 2;
        tmp.data.compute_data_burst.length = *((uint16_t*)burst_lenght);
    }

    if (!get_message_size(&tmp, &msg_size)){
        pthread_mutex_unlock(fd_lock);
        fprintf(stderr, "ERROR: Recieved message of unknown type: %d.\n", buffer[0]);
        return false;
    }

    if (msg_size > buffer_size){
        pthread_mutex_unlock(fd_lock);
        fprintf(stderr, "ERROR: Message size (%d) is larger than buffer size (%ld)\n", msg_size, buffer_size);
        return false;
    }

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

int create_all_threads(int N, thread_t threads[N], void *data){
    int ret = ERROR_OK;
    for (int i = 0; i < N; i++){
        if ((ret = pthread_mutex_init(&threads[i].lock, NULL)) != ERROR_OK) {
            fprintf(stderr, "ERROR: Initialization of mutex in '%s' thread failed.\n", threads[i].thread_name);
            return ERROR_CREATING_THREADS;
        };
        if ((ret = pthread_create(&threads[i].thread, NULL, threads[i].thread_function, data)) != ERROR_OK){
            fprintf(stderr, "ERROR: Creating thread '%s' failed.\n", threads[i].thread_name);
            return ERROR_CREATING_THREADS;
        } else {
            fprintf(stderr, "INFO: Succesfully created '%s' thread.\n", threads[i].thread_name);
        }
    }
    return ret;
}



