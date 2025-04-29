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

void open_pipes(data_t *in, data_t *out, const char *in_pipe_name, const char *out_pipe_name){

    pthread_mutex_lock(&in->lock);
    if ((in->fd = io_open_read(in_pipe_name)) == -1){
        pthread_mutex_unlock(&in->lock);
        fprintf(stderr, "ERROR: Cannot open named pipe port %s\n", in_pipe_name);
        in->quit = true;
        call_termios(SET_TERMINAL_TO_DEFAULT);
        exit(ERROR_OPENING_PIPE);
    } else {
        pthread_mutex_unlock(&in->lock);
        fprintf(stderr, "INFO: Named pipe port %s (ID %d) opened succesfully for reading\n", 
            in_pipe_name, in->fd);
    }

    fprintf(stderr, "INFO: Waiting for someone to join pipe port %s as a reader\n", out_pipe_name); 
    int tmp = io_open_write(out_pipe_name);
    pthread_mutex_lock(&out->lock);
    if ((out->fd = tmp) == -1){
        pthread_mutex_unlock(&out->lock);
        fprintf(stderr, "ERROR: Cannot open named pipe port %s: %s\n", 
            out_pipe_name, strerror(errno));
        out->quit = true;
        call_termios(SET_TERMINAL_TO_DEFAULT);
        exit(ERROR_OPENING_PIPE);
    } else {
        pthread_mutex_unlock(&out->lock);
        fprintf(stderr, "INFO: Named pipe port %s (ID %d) opened succesfully for writing\n", 
            out_pipe_name, out->fd);
    }
}

bool send_message(int fd, message msg){
    const size_t buffer_size = sizeof(message);
    uint8_t buffer[buffer_size];
    int msg_size;

    if (!get_message_size(msg.type, &msg_size)){
        fprintf(stderr, "ERROR: Sending message of type %d failed, "
            "because message type is invalid.\n", msg.type);
        return false;
    }

    if (!fill_message_buf(&msg, buffer, buffer_size, &msg_size)){
        fprintf(stderr, "ERROR: Serializing message of type %d failed.\n", msg.type);
        return false;
    }

    for (int i = 0; i < msg_size; i++){
        if (io_putc(fd, buffer[i]) != 1){
            fprintf(stderr, "ERROR: io_putc() failed.\n");
            return false;
        }
    }

    fprintf(stderr, "INFO: Message of type %d successfully sent in %d bytes.\n", msg.type, msg_size);
    return true;
}

bool recieve_message(int fd, message *out_msg, int timeout_ms){
    const size_t buffer_size = sizeof(message);
    uint8_t buffer[buffer_size];
    int bytes_read = 0;

    if (io_getc_timeout(fd, timeout_ms, &buffer[bytes_read++]) == 0){
        return false; // no message to be read
    }

    int msg_size;
    if (!get_message_size(buffer[0], &msg_size)){
        fprintf(stderr, "ERROR: Recieved message of unknown type: %d.\n", buffer[0]);
        return false;
    }

    if (msg_size > buffer_size){
        fprintf(stderr, "ERROR: Message size (%d) is larger than buffer size (%ld)\n", msg_size, buffer_size);
        return false;
    }

    while (bytes_read < msg_size){
        if (io_getc_timeout(fd, timeout_ms, &buffer[bytes_read++]) == 0){
            fprintf(stderr, "ERROR: Couldnt read all %d bytes of message, %d bytes were succesfully read.\n", 
                msg_size, bytes_read - 1);
            return false; 
        }
    }

    if (!parse_message_buf(buffer, msg_size, out_msg)){
        fprintf(stderr, "ERROR: Parsing message of type %d failed.\n", buffer[0]);
        return false;
    }

    fprintf(stderr, "INFO: Message of type %d succesfully recieved in %d bytes.\n", out_msg->type, msg_size);
    return true;
}

