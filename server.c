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
#include <errno.h>
#include <inttypes.h>

#include "err.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x2000
#endif

#define DEF_PORT_NUM 6543
#define QUEUE_LENGTH 5
#define BUFFER_SIZE 512000

#define NUMBER_OF_MSG_TYPES 2

/// Files names request and its messages
#define FILES_NAMES_REQUEST 1
#define FILE_FRAGMENT_REQUEST 2
#define FILE_FRAGMENT_SENDING 3
#define FILES_NAMES_LIST 1

/// Server refusal and its messages
#define SERVER_REFUSAL 2
#define FILE_ERROR 1
#define WRONG_START_ADDRESS 2
#define WRONG_FRAGMENT_SIZE 3

#define ANALYSIS


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

void open_directory(const char *dir_name, DIR **dir_stream, int *dir_fd) {
#ifdef ANALYSIS_DETAILED
    printf("opening directory: %s\n", dir_name);
#endif
    *dir_stream = opendir(dir_name);
    if (*dir_stream == NULL)
        syserr("opening directory");

    if ((*dir_fd = dirfd(*dir_stream)) < 0)
        syserr("opening directory");
#ifdef ANALYSIS_DETAILED
    printf("directory opened\n");
#endif
}

void close_directory(DIR *dir_stream) {
    if (closedir(dir_stream) < 0)
        syserr("closing directory");
#ifdef ANALYSIS_DETAILED
    printf("directory closed\n");
#endif
}

/******************************************** CONNECTION ******************************************/

void create_socket(int *sockfd) {
    *sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (*sockfd < 0)
        syserr("socket");
#ifdef ANALYSIS_DETAILED
    printf("socket created\n");
#endif
}

void set_server_address(struct sockaddr_in *server_address, int port_num) {
    memset(server_address, 0, sizeof(struct sockaddr_in));
    server_address->sin_family = AF_INET;
    server_address->sin_addr.s_addr = htonl(INADDR_ANY);
    server_address->sin_port = htons(port_num);
#ifdef ANALYSIS_DETAILED
    printf("server_address set\n");
#endif
}

void bind_socket(int sockfd, struct sockaddr_in *server_address) {
    if (bind(sockfd, (struct sockaddr *) server_address, sizeof(struct sockaddr_in)) < 0)
        syserr("bind");
#ifdef ANALYSIS_DETAILED
    printf("socket binded\n");
#endif
}

void set_listen(int sockfd) {
    if (listen(sockfd, QUEUE_LENGTH) < 0)
        syserr("listen");
#ifdef ANALYSIS_DETAILED
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
#ifdef ANALYSIS
    printf("\n************************************ CONNECTION ACCEPTED *******************************************\n");
#endif
}

void end_connection_msg() {
    printf("\n************************************* CONNECTION ENDED *********************************************\n\n");
}

/******************************************** MESSAGES ********************************************/

int send_server_msg(int msg_sockfd, uint16_t msg_type, uint32_t param) {
    struct msg_server msg;
    msg.msg_type = htons(msg_type);
    msg.param = htonl(param);

    if (send(msg_sockfd, &msg, sizeof(struct msg_server), MSG_NOSIGNAL) != sizeof(struct msg_server) && errno != EPIPE)
        return -1;
    return 0;
}

void end_connection_early(int msg_sockfd, const char *msg) {
    printf("%s: ending connection...\n", msg);
    if (close(msg_sockfd) < 0)
        syserr("close socket");
}

void create_files_names_list(DIR *dir_stream, int dir_fd, char files_names_list[], uint32_t *list_len) {
    struct dirent *dir_entry;
    struct stat statbuf;
    bool first_file = true;

    rewinddir(dir_stream);
    while ((dir_entry = readdir(dir_stream))) {
        if (fstatat(dir_fd, dir_entry->d_name, &statbuf, 0) != 0)
            syserr("fstatat");

        if (S_ISREG(statbuf.st_mode)) {
            if (!first_file)
                files_names_list[(*list_len)++] = '|';
            first_file = false;

            for (int i = 0; dir_entry->d_name[i] != '\0'; i++) {
                files_names_list[*list_len] = dir_entry->d_name[i];
                (*list_len)++;
            }
        }
    }

#ifdef ANALYSIS_DETAILED
    printf("directory list: ");
    for (int i = sizeof(struct msg_server); i < *list_len; i++)
        printf("%c", files_names_list[i]);
    printf("\n");
#endif
}

int send_files_names(int msg_sockfd, DIR *dir_stream, int dir_fd, char buffer[]) {
#ifdef ANALYSIS
    printf("sending files names list...\n");
#endif
    uint32_t data_len = 0;

    create_files_names_list(dir_stream, dir_fd, buffer, &data_len);
    if (send_server_msg(msg_sockfd, FILES_NAMES_LIST, data_len) < 0)
        return -1;

    if (send(msg_sockfd, buffer, data_len, MSG_NOSIGNAL) != data_len && errno != EPIPE)
        return -1;
#ifdef ANALYSIS
    printf("files name list sent\n");
#endif
    return 0;
}

/**
 * @return true if msg_type was one of accepted msg_types in current state
 */
bool is_accepted(uint16_t msg_type, const uint16_t *acceptable, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (msg_type == acceptable[i])
            return true;
    return false;
}

/**
 *
 * @param msg_sockfd
 * @param acceptable - array integers representing acceptable msg_types in current state of the server
 * @param len - length of the acceptable array
 * @return true if received msg_type is accepted in current state of the server
 *         false otherwise
 */
int32_t read_msg_type(int msg_sockfd, const uint16_t acceptable[], size_t len) {
#ifdef ANALYSIS
    printf("\nreading msg type\n");
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

#ifdef ANALYSIS
    printf("msg type read: %d\n", msg_type);
#endif
    if (!is_accepted(msg_type, acceptable, len))
        return -1;
    return msg_type;
}


void receive_file_fragment_request_header(int msg_sockfd, struct file_fragment_request *msg) {
#ifdef ANALYSIS
    printf("receiving file fragment request header...\n");
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
#ifdef ANALYSIS
    printf("header info: ");
    printf("%" PRIu32 " ", msg->start_addr);
    printf("%" PRIu32 " ", msg->bytes_to_send);
    printf("%" PRIu32 " ", msg->file_name_len);
    printf("\nfile fragment request header received\n");
#endif
}

void receive_file_fragment_request_file_name(int msg_sockfd, uint16_t file_name_len, char buffer[]) {
#ifdef ANALYSIS
    printf("receiving file name for requested fragment...\n");
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

    buffer[file_name_len] = '\0'; // so that we can use it in open()
#ifdef ANALYSIS
    printf("requested file: ");
    for (int i = 0; i < file_name_len; i++)
        printf("%c", buffer[i]);
    printf("\n");
#endif
}

bool is_valid_file_name(const char buffer[], uint16_t file_name_len) {
    for (int i = 0; i < file_name_len; i++)
        if (buffer[i] == '/')
            return false;

    return true;
}

/**
 * @return file size in bytes on success
 *         -1 if any error occurred
 */
ssize_t prepare_file(int *fd, int dir_fd, char buffer[], uint16_t file_name_len, uint32_t start_addr) {
    struct stat fstat_buff;

    if (!is_valid_file_name(buffer, file_name_len) || (*fd = openat(dir_fd, buffer, O_RDONLY)) < 0) {
        msgerr("file opening failed");
        return -1;
    }

    if (fstat(*fd, &fstat_buff) < 0) {
        msgerr("lstat error");
        return -1;
    }

    if (!S_ISREG(fstat_buff.st_mode)) {
        if (close(*fd) < 0)
            syserr("file closing");
        return -1;
    }

    if (lseek(*fd, start_addr, SEEK_SET) < 0) {
        msgerr("lseek error");
        return -1;
    }

    return fstat_buff.st_size;
}


int check_start_addr(uint32_t start_addr, size_t file_size) {
    if (start_addr >= file_size)
        return -1;
    return 0;
}

int check_fragment_length(uint32_t fragment_length) {
    if (fragment_length == 0)
        return -1;
    return 0;
}

int request_setup(int msg_sockfd, int *fd, ssize_t *file_size, const struct file_fragment_request *request,
        int dir_fd, char *buffer, uint16_t file_name_len) {
    if (check_fragment_length(request->bytes_to_send) < 0) {
        send_server_msg(msg_sockfd, SERVER_REFUSAL, WRONG_FRAGMENT_SIZE);
        end_connection_early(msg_sockfd, "wrong fragment length");
        return -1;
    }
    if ((*file_size = prepare_file(fd, dir_fd, buffer, file_name_len, request->start_addr)) < 0) {
        send_server_msg(msg_sockfd, SERVER_REFUSAL, FILE_ERROR);
        end_connection_early(msg_sockfd, "wrong file name");
        return -1;
    }
    if (check_start_addr(request->start_addr, (size_t)*file_size) < 0) {
        send_server_msg(msg_sockfd, SERVER_REFUSAL, WRONG_START_ADDRESS);
        end_connection_early(msg_sockfd, "wrong start address");
        if (close(*fd) < 0)
            syserr("file closing");
        return -1;
    }

    return 0;
}

void send_file_fragment(int msg_sockfd, int dir_fd, const struct file_fragment_request *request, char buffer[]) {
#ifdef ANALYSIS
    printf("\nfile fragment sending...\n");
#endif
    int fd;
    size_t data_len, bytes_to_send, remains_to_send, remains_to_read, already_sent = 0, already_read;
    ssize_t read_curr, sent_curr, file_size;

    if (request_setup(msg_sockfd, &fd, &file_size, request, dir_fd, buffer, request->file_name_len) == 0) {
        bytes_to_send = min(request->bytes_to_send, (uint32_t) (file_size - request->start_addr));
        if (send_server_msg(msg_sockfd, FILE_FRAGMENT_SENDING, (uint32_t) bytes_to_send) < 0) {
            end_connection_early(msg_sockfd, "message sending error");
        } else {
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
                if (send(msg_sockfd, buffer, data_len, MSG_NOSIGNAL) != data_len && errno != EPIPE)
                    syserr("partial / failed write");
                already_sent += sent_curr;
            } while (sent_curr > 0); // while sent less than requested
        }

        if (close(fd) < 0)
            syserr("file closing");
    }
#ifdef ANALYSIS
    printf("file fragment sent\n");
#endif
}

void run_server(int server_sockfd, DIR *dir_stream, int dir_fd) {
#ifdef ANALYSIS
    printf("run_server starting...\n");
#endif
    int msg_sockfd;
    struct sockaddr_in client_address;
    struct file_fragment_request request_msg;
    char buffer[BUFFER_SIZE];
    uint16_t acceptable[NUMBER_OF_MSG_TYPES];

    while (true) {
        printf("\nwaiting for connection...\n");
        accept_connection(server_sockfd, &msg_sockfd, &client_address);

        acceptable[0] = 1, acceptable[1] = 2;
        switch (read_msg_type(msg_sockfd, acceptable, 2)) {
            case FILES_NAMES_REQUEST:
                acceptable[0] = 2;
                if (send_files_names(msg_sockfd, dir_stream, dir_fd, buffer) < 0) {
                    end_connection_early(msg_sockfd, "message sending error");
                    end_connection_msg();
                    continue;
                }
                if (read_msg_type(msg_sockfd, acceptable, 1) < 0) {
                    end_connection_early(msg_sockfd, "wrong message type");
                    end_connection_msg();
                    continue;
                }
            case FILE_FRAGMENT_REQUEST:
                receive_file_fragment_request_header(msg_sockfd, &request_msg);
                receive_file_fragment_request_file_name(msg_sockfd, request_msg.file_name_len, buffer);
                send_file_fragment(msg_sockfd, dir_fd, &request_msg, buffer);
                break;
            default:
                end_connection_early(msg_sockfd, "wrong message type");
        }

        end_connection_msg();
    }
}


int main(int argc, char *argv[]) {
    int32_t server_sockfd;
    DIR *dir_stream;
    struct sockaddr_in server_address;
    int port_num, dir_fd;

    check_argc(argc, argv);
    char const *dir_path = argv[1];
    port_num = argc == 3 ? atoi(argv[2]) : DEF_PORT_NUM;

    open_directory(dir_path, &dir_stream, &dir_fd);

    create_socket(&server_sockfd);
    set_server_address(&server_address, port_num);
    bind_socket(server_sockfd, &server_address);
    set_listen(server_sockfd);

    run_server(server_sockfd, dir_stream, dir_fd);

    close_directory(dir_stream);
    close_socket(server_sockfd);

    return 0;
}