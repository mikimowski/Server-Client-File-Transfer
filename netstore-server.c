#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "err.h"

#define DEBUG
#define DEF_PORT_NUM 6543
#define QUEUE_LENGTH 5
#define BUFFER_SIZE 512000
#define CLIENT_MSG_BUFFER_SIZE 256
#define NUMBER_OF_MSG_TYPE 2


struct __attribute__((__packed__)) msg_client {
    uint16_t msg_type;
    uint32_t start_addr;
    uint32_t bytes_to_send;
    uint16_t file_name_len;
};

struct __attribute__((__packed__)) msg_server {
    uint16_t msg_type;
    uint32_t param;
};



void check_argc(int argc, char *argv[]) {
    if (argc != 2 && argc != 3)
        fatal("Usage %s <directory-with-files> [server-port-number]", argv[0]);
}

void open_directory(const char *dir_name, DIR **dir_stream) {
    #ifdef DEBUG
    printf("opening directory: %s\n", dir_name);
    #endif
    *dir_stream = opendir(dir_name);
    if (*dir_stream == NULL)
        syserr("opening directory");

}

void create_socket(int *sockfd) {
    *sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (*sockfd < 0)
        syserr("socket");
}

void set_server_address(struct sockaddr_in *server_address, int *port_num) {
    memset(server_address, 0, sizeof(struct sockaddr_in));
    server_address->sin_family = AF_INET;
    server_address->sin_addr.s_addr = htonl(INADDR_ANY);
    server_address->sin_port = htons(DEF_PORT_NUM);
//    server_address->sin_port = htons(port_num == NULL ? DEF_PORT_NUM : *port_num);
}

void bind_socket(int sockfd, struct sockaddr_in *server_address) {
    if (bind(sockfd, (struct sockaddr *) server_address, sizeof(struct sockaddr_in)) < 0)
        syserr("bind");
}

void set_listen(int sockfd) {
    if (listen(sockfd, QUEUE_LENGTH) < 0)
        syserr("listen");
}

void close_socket(int sockfd) {
    if (close(sockfd) < 0)
        syserr("close");
}

void accept_connection(int server_sockfd, struct sockaddr_in *server_address, int *msg_sockfd, struct sockaddr_in *client_address) {
    socklen_t client_address_len;
    client_address_len = sizeof(*client_address);
    *msg_sockfd = accept(server_sockfd, (struct sockaddr *) client_address, &client_address_len);
    if (*msg_sockfd < 0)
        syserr("accept");
}


void send_msg(int msg_sockfd, int msg_type) {

}

void create_files_names_list(DIR *dir_stream, char files_names_list[], uint32_t *list_len) {
    struct dirent *dir_entry;
    struct stat statbuf;

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
    printf("Directory list:\n");
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

    msg.msg_type = 1;
    msg.param = data_len - sizeof(struct msg_server);
    memcpy(buffer, &msg, sizeof(struct msg_server));

    if (write(msg_sockfd, buffer, data_len) != data_len)
        syserr("partial / failed write");
#ifdef DEBUG
    printf("files name list sent\n");
#endif
}

bool is_expected(uint16_t msg_type, const uint16_t expected[], size_t len) {
    for (size_t i = 0; i < len; i++)
        if (msg_type == expected[i])
            return true;
    return false;
}

uint16_t read_msg_type(int msg_sockfd, const uint16_t expected[], size_t len) {
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

    if (!is_expected(msg_type, expected, len))
        syserr("wrong msg_type");
#ifdef DEBUG
    printf("msg type read: %d\n", msg_type);
#endif
    return msg_type;
}


void receive_file_fragment_request_info(int msg_sockfd, struct msg_client *msg) {
#ifdef DEBUG
    printf("receive_file_fragment_request_info\n");
#endif
    ssize_t read_curr;
    size_t remains, read_all = 0; // TODO msg_type already read chcielibyśmy,,, więc sizeof(uint16_t)...

    do {
        remains = sizeof(struct msg_client) - read_all;
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
    printf("enf of receive_file_fragment_request_info\n");
#endif
}

void receive_file_fragment_request_file_name(int msg_sockfd, uint16_t file_name_len, char buffer[]) {
#ifdef DEBUG
    printf("receive_file_fragment_request_file_name\n");
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
    printf("end of receive_file_fragment_request_file_name\n");
    printf("file name: ");
    for (int i = 0; i < file_name_len; i++)
        printf("%c", buffer[i]);
    printf("\n");
#endif
}

void send_file_fragment(int msg_sockfd, struct msg_client *msg, char buffer[], uint16_t file_name_len) {
#ifdef DEBUG
    printf("send_file_fragment\n");
#endif

    // TODO

#ifdef DEBUG
    printf("end of send_file_fragment\n");
#endif
}


void run_server(int server_sockfd, DIR *dir_stream, struct sockaddr_in *server_address) {
    int msg_sockfd;
    struct sockaddr_in client_address;
    struct msg_client msg;
    char buffer[BUFFER_SIZE];
    uint16_t expected[NUMBER_OF_MSG_TYPE];

    while (true) {
        accept_connection(server_sockfd, server_address, &msg_sockfd, &client_address);
#ifdef DEBUG
    printf("client connected\n");
#endif
        expected[0] = 1, expected[1] = 2;
        switch (read_msg_type(msg_sockfd, expected, 2)) {
            case 1:
                send_files_names(msg_sockfd, dir_stream, buffer);
                //  expected[0] = 2;
                //    read_msg_type(msg_sockfd, expected, 1);
            case 2:
                receive_file_fragment_request_info(msg_sockfd, &msg);
                receive_file_fragment_request_file_name(msg_sockfd, msg.file_name_len, buffer);
                send_file_fragment(msg_sockfd, buffer, msg.file_name_len);
                break;
            default:
                syserr("unknown message type");
        }

        break;
    }
}

/**** DEBUGGIN ****/
void display_directory(DIR *dir_stream) {
    struct dirent *dir_entry;

    while ((dir_entry = readdir(dir_stream))) {
        printf("%s\n", dir_entry->d_name);
    }
}



int main(int argc, char *argv[]) {
    int32_t server_sockfd;
    int32_t *port_num = NULL;
    DIR *dir_stream;
    struct sockaddr_in server_address;

    check_argc(argc, argv);
    char const *dir_path = argv[1];

    open_directory(dir_path, &dir_stream);

    char buffer[BUFFER_SIZE];

#ifdef DEBUG
        printf("directory opened\n");
#endif
    create_socket(&server_sockfd);
    #ifdef DEBUG
            printf("socket created\n");
    #endif
    set_server_address(&server_address, port_num);
    #ifdef DEBUG
            printf("server_address set\n");
    #endif
    bind_socket(server_sockfd, &server_address);
    #ifdef DEBUG
            printf("socket binded\n");
    #endif
    set_listen(server_sockfd);
    #ifdef DEBUG
            printf("socket set listen\n");
    #endif
    #ifdef DEBUG
            printf("run_server starting...\n");
    #endif
    run_server(server_sockfd, dir_stream, &server_address);
    #ifdef DEBUG
            printf("run_server ended\n");
    #endif
 //   close_directory();
    close_socket(server_sockfd);

    return 0;
}
