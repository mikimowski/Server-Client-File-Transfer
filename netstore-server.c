#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include "err.h"

#define DEF_PORT_NUM 6543
#define QUEUE_LENGTH 5
#define BUFFER_SIZE 512000
#define NUMBER_OF_MSG_TYPES 2
#define FILES_NAMES_REQUEST 1
#define FILE_FRAGMENT_REQUEST 2
#define FILE_FRAGMENT_SENDING 3

#define DEBUG


struct __attribute__((__packed__)) file_fragment_request {
    uint32_t start_addr;
    uint32_t bytes_to_send;
    uint16_t file_name_len;
};

struct __attribute__((__packed__)) msg_server {
    uint16_t msg_type;
    uint32_t param;
};


size_t min(size_t a, size_t b) {
    return a < b ? a : b;
}

void check_argc(int argc, char *argv[]) {
    if (argc != 2 && argc != 3)
        fatal("Usage %s <directory-with-files> [server-port-number]", argv[0]);
}

void open_directory(const char *dir_name, DIR **dir_stream) {
#ifdef DEBUG_DETAILED
    printf("opening directory: %s\n", dir_name);
#endif
    *dir_stream = opendir(dir_name);
    if (*dir_stream == NULL)
        syserr("opening directory");
#ifdef DEBUG_DETAILED
    printf("directory opened\n");
#endif

}

void close_directory(DIR *dir_stream) {
    if (closedir(dir_stream) < 0)
        syserr("closing directory");
#ifdef DEBUG_DETAILED
    printf("directory closed\n");
#endif
}

void create_socket(int *sockfd) {
    *sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (*sockfd < 0)
        syserr("socket");
#ifdef DEBUG_DETAILED
    printf("socket created\n");
#endif
}

void set_server_address(struct sockaddr_in *server_address, int port_num) {
    memset(server_address, 0, sizeof(struct sockaddr_in));
    server_address->sin_family = AF_INET;
    server_address->sin_addr.s_addr = htonl(INADDR_ANY);
    server_address->sin_port = htons(port_num);
#ifdef DEBUG_DETAILED
    printf("server_address set\n");
#endif
}

void bind_socket(int sockfd, struct sockaddr_in *server_address) {
    if (bind(sockfd, (struct sockaddr *) server_address, sizeof(struct sockaddr_in)) < 0)
        syserr("bind");
#ifdef DEBUG_DETAILED
    printf("socket binded\n");
#endif
}

void set_listen(int sockfd) {
    if (listen(sockfd, QUEUE_LENGTH) < 0)
        syserr("listen");
#ifdef DEBUG_DETAILED
    printf("socket set listen\n");
#endif
}

void close_socket(int sockfd) {
    if (close(sockfd) < 0)
        syserr("close");
}

void accept_connection(int server_sockfd, int *msg_sockfd, struct sockaddr_in *client_address) {
    socklen_t client_address_len;
    client_address_len = sizeof(*client_address);
    *msg_sockfd = accept(server_sockfd, (struct sockaddr *) client_address, &client_address_len);
    if (*msg_sockfd < 0)
        syserr("accept");
#ifdef DEBUG
    printf("\nconnection accepted\n");
#endif
}


void create_files_names_list(DIR *dir_stream, char files_names_list[], size_t *list_len) {
    struct dirent *dir_entry;
    struct stat statbuf;

    rewinddir(dir_stream);
    while ((dir_entry = readdir(dir_stream))) {
        if (fstatat(dirfd(dir_stream), dir_entry->d_name, &statbuf, 0) != 0)
            syserr("going through directory failure");

        if (!S_ISDIR(statbuf.st_mode)) {
            for (int i = 0; dir_entry->d_name[i] != '\0'; i++) {
                files_names_list[*list_len] = dir_entry->d_name[i];
                (*list_len)++;
            }
            files_names_list[*list_len] = '|';
            (*list_len)++;
        }
    }

#ifdef DEBUG
    printf("directory list: ");
    for (int i = sizeof(struct msg_server); i < *list_len; i++)
        printf("%c", files_names_list[i]);
    printf("\n");
#endif
}

void send_files_names(int msg_sockfd, DIR *dir_stream, char buffer[]) {
#ifdef DEBUG
    printf("sending files names list\n");
#endif
    struct msg_server msg;
    size_t data_len = sizeof(struct msg_server);

    create_files_names_list(dir_stream, buffer, &data_len);

    msg.msg_type = htons(1);
    msg.param = htonl(data_len - sizeof(struct msg_server));
    memcpy(buffer, &msg, sizeof(struct msg_server));

    if (write(msg_sockfd, buffer, data_len) != data_len)
        syserr("partial / failed write");
#ifdef DEBUG
    printf("files name list sent\n");
#endif
}

/**
 *
 * @param msg_type
 * @param expected
 * @param len
 * @return True if msg_type was one of accepted msg_types in current state
 */
bool is_expected(uint16_t msg_type, const uint16_t accepted[], size_t len) {
    for (size_t i = 0; i < len; i++)
        if (msg_type == accepted[i])
            return true;
    return false;
}

uint16_t read_msg_type(int msg_sockfd, const uint16_t accepted[], size_t len) {
#ifdef DEBUG
    printf("reading msg type\n");
#endif
    ssize_t read_curr;
    size_t read_all = 0, remains;
    uint16_t msg_type;

    do {
        remains = sizeof(uint16_t) - read_all;
        read_curr = read(msg_sockfd, &msg_type + read_all, remains);
        if (read_curr < 0)
            syserr("reading message type");

        read_all += read_curr;
    } while (read_curr > 0);

    msg_type = ntohs(msg_type);

    if (!is_expected(msg_type, accepted, len))
        syserr("wrong msg_type");
#ifdef DEBUG
    printf("msg type read: %d\n", msg_type);
#endif
    return msg_type;
}


void receive_file_fragment_request_header(int msg_sockfd, struct file_fragment_request *msg) {
#ifdef DEBUG
    printf("receiving file fragment request header\n");
#endif
    ssize_t read_curr;
    size_t remains, read_all = 0;

    do {
        remains = sizeof(struct file_fragment_request) - read_all;
        read_curr = read(msg_sockfd, msg + read_all, remains);
        if (read_curr < 0)
            syserr("reading message type");

        read_all += read_curr;
    } while (read_curr > 0);

    msg->start_addr = ntohl(msg->start_addr);
    msg->bytes_to_send = ntohl(msg->bytes_to_send);
    msg->file_name_len = ntohs(msg->file_name_len);
#ifdef DEBUG
    printf("msg_info: ");
    printf("%d %d %d\n", msg->start_addr, msg->bytes_to_send, msg->file_name_len);
    printf("file fragment request header received\n");
#endif
}

void receive_file_fragment_request_file_name(int msg_sockfd, uint16_t file_name_len, char buffer[]) {
#ifdef DEBUG
    printf("receiving file_name for requested fragment\n");
#endif
    ssize_t read_curr;
    size_t read_all = 0, remains;

    do {
        remains = file_name_len - read_all;
        read_curr = read(msg_sockfd, buffer + read_all, remains);
        if (read_curr < 0)
            syserr("reading message type");

        read_all += read_curr;
    } while (read_curr > 0);
#ifdef DEBUG
    printf("file name: ");
    for (int i = 0; i < file_name_len; i++)
        printf("%c", buffer[i]);
    printf("\n");
    printf("file_name for given fragment received\n");
#endif
}


void send_file_fragment(int msg_sockfd, DIR *dir_stream, const struct file_fragment_request *request, char buffer[]) {
#ifdef DEBUG
    printf("send_file_fragment\n");
#endif
    int dir_fd = dirfd(dir_stream);
    int fd;
    struct msg_server msg_to_send;
    struct stat fstat_buff;
    size_t data_len, bytes_to_send, remains_to_send, remains_to_read, already_sent = 0, already_read;
    ssize_t read_curr, sent_curr;

    buffer[request->file_name_len] = '\0'; // So that we can use it in open()
    if ((fd = openat(dir_fd, buffer, O_RDONLY)) < 0)
        syserr("file opening");

    if (fstat(fd, &fstat_buff) < 0)
        syserr("lstat error");
    bytes_to_send = min(request->bytes_to_send, (uint32_t)(fstat_buff.st_size - request->start_addr));

    if (lseek(fd, request->start_addr, SEEK_SET) < 0)
        syserr("lseek error");

    msg_to_send.msg_type = htons(FILE_FRAGMENT_SENDING);
    msg_to_send.param = htonl(bytes_to_send);
    memcpy(buffer, &msg_to_send, sizeof(struct msg_server));
    data_len = sizeof(struct msg_server);
    if (write(msg_sockfd, buffer, data_len) != data_len)
        syserr("partial / failed write");

    do { // send requested number of bytes
        remains_to_send = bytes_to_send - already_sent;

        read_curr = already_read = 0;
        do { // read from file up to BUFF_SIZE bytes
            remains_to_read = min(remains_to_send, BUFFER_SIZE) - already_read;
            if ((read_curr = read(fd, buffer + read_curr, remains_to_read)) < 0)
                syserr("file reading");

            already_read += read_curr;
        } while (read_curr > 0); // While read less than min(left_requested, buff_size)

        // send read bytes
        sent_curr = already_read;
        data_len = already_read;
        if (write(msg_sockfd, buffer, data_len) != data_len)
            syserr("partial / failed write");
        already_sent += sent_curr;
    } while (sent_curr > 0); // while sent less than requested

    if (close(fd) < 0)
        syserr("file closing");
#ifdef DEBUG
    printf("end of send_file_fragment\n");
#endif
}


void run_server(int server_sockfd, DIR *dir_stream) {
#ifdef DEBUG
    printf("run_server starting...\n");
#endif
    int msg_sockfd;
    struct sockaddr_in client_address;
    struct file_fragment_request request_msg;
    char buffer[BUFFER_SIZE];
    uint16_t accepted[NUMBER_OF_MSG_TYPES];

    while (true) {
        accept_connection(server_sockfd, &msg_sockfd, &client_address);

        accepted[0] = 1, accepted[1] = 2;
        switch (read_msg_type(msg_sockfd, accepted, 2)) {
            case FILES_NAMES_REQUEST:
                send_files_names(msg_sockfd, dir_stream, buffer);
                accepted[0] = 2;
                read_msg_type(msg_sockfd, accepted, 1);
            case FILE_FRAGMENT_REQUEST:
                receive_file_fragment_request_header(msg_sockfd, &request_msg);
                receive_file_fragment_request_file_name(msg_sockfd, request_msg.file_name_len, buffer);
                send_file_fragment(msg_sockfd, dir_stream, &request_msg, buffer);
                break;
            default:
                syserr("unknown message type");
        }
    }

#ifdef DEBUG
    printf("run_server ended\n");
#endif
}





int main(int argc, char *argv[]) {
    int32_t server_sockfd;
    DIR *dir_stream;
    struct sockaddr_in server_address;
    int port_num;

    check_argc(argc, argv);
    char const *dir_path = argv[1];
    port_num = argc == 3 ? atoi(argv[2]) : DEF_PORT_NUM;

    open_directory(dir_path, &dir_stream);

    create_socket(&server_sockfd);
    set_server_address(&server_address, port_num);
    bind_socket(server_sockfd, &server_address);
    set_listen(server_sockfd);

    run_server(server_sockfd, dir_stream);

    close_directory(dir_stream);
    close_socket(server_sockfd);

    return 0;
}



// TODO err.c err.h