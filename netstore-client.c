#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "err.h"

#define DEF_PORT_NUM "6543"
#define BUFFER_SIZE 524288
#define MAX_FILE_NAME_LEN 257
#define MAX_NUMBER_OF_FILES 65536
#define FILES_NAMES_BUFFER_SIZE MAX_NUMBER_OF_FILES * MAX_FILE_NAME_LEN
#define FILES_NAMES_REQUEST 1
#define FILE_FRAGMENT_REQUEST 2
#define SERVER_REFUSAL 2
#define WRONG_FILE_NAME 1
#define WRONG_FRAGMENT_ADDRESS 2
#define NO_FRAGMENT_SIZE 3

#define DEBUG

struct __attribute__((__packed__)) file_fragment_request {
  uint16_t msg_type;
  uint32_t start_addr;
  uint32_t bytes_to_send;
  uint16_t file_name_len;
};

struct __attribute__((__packed__)) msg_server {
    uint16_t msg_type;
    uint32_t param;
};

struct user_command {
    int32_t file_id;
    int32_t start_addr;
    int32_t end_addr;
};

/******************************************** CONNECTION ******************************************/

void check_argc(int argc, char *argv[]) {
    if (argc != 2 && argc != 3)
        fatal("Usage %s <nazwa-lub-adres-IP4-serwera> [<numer-portu-serwera>]", argv[0]);
}

void set_addr_hints(struct addrinfo *addr_hints) {
    memset(addr_hints, 0, sizeof(struct addrinfo));
    addr_hints->ai_family = AF_INET; // IPv4
    addr_hints->ai_socktype = SOCK_STREAM;
    addr_hints->ai_protocol = IPPROTO_TCP;
}

void get_address_info(char const *host, char const *port, struct addrinfo *addr_hints, struct addrinfo **addr_result) {
    int err;

    err = getaddrinfo(host, port, addr_hints, addr_result);
    if (err == EAI_SYSTEM) // system error
      syserr("getaddrinfo: %s", gai_strerror(err));
    else if (err != 0) // other error (host not found, etc.)
      fatal("getaddrinfo: %s", gai_strerror(err));
}

void init_socket(int *sockfd, struct addrinfo *addr_result) {
    *sockfd = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (*sockfd < 0)
        syserr("socket");
}

void connect_socket(int sockfd, struct addrinfo *addr_result) {
    if (connect(sockfd, addr_result->ai_addr, addr_result->ai_addrlen) < 0)
        syserr("connect");
}

void connect_with_server(int *sockfd, char const *host, char const *port) {
    struct addrinfo addr_hints;
    struct addrinfo *addr_result = NULL;

    set_addr_hints(&addr_hints);
    #ifdef DEBUG
        printf("addr_hints set\n");
    #endif
    get_address_info(host, port, &addr_hints, &addr_result);
    #ifdef DEBUG
        printf("address got\n");
    #endif
    init_socket(sockfd, addr_result);
    #ifdef DEBUG
        printf("socket initialized\n");
    #endif
    connect_socket(*sockfd, addr_result);

    freeaddrinfo(addr_result);
}

void close_socket(int sockfd) {
    if (close(sockfd) < 0)
        syserr("close");
}

/********************************************* MESSAGES *******************************************/

void send_files_names_request(int sockfd) {
#ifdef DEBUG
    printf("sending files names request\n");
#endif

    uint16_t msg_code = htons(FILES_NAMES_REQUEST);
    size_t msg_len = sizeof(msg_code);

    if (write(sockfd, &msg_code, msg_len) != msg_len)
        syserr("partial / failed write");

    #ifdef DEBUG
    printf("files names request sent...\n");
    #endif
}

void get_error_type() {

}

/// Read exactly 6 bits ~ 2 + 4
void receive_server_msg(int sockfd, struct msg_server *msg) {
    ssize_t read_curr;
    size_t read_all = 0, remains;

    #ifdef DEBUG
        printf("receiving server msg...\n");
    #endif
    do {
        remains = sizeof(struct msg_server) - read_all;
        read_curr = read(sockfd, msg + read_all, remains);
        if (read_curr < 0)
            syserr("receiving message from server");

        read_all += read_curr;
    } while (read_curr > 0);

    if (msg->msg_type == SERVER_REFUSAL) {
        switch (msg->param) {
            case WRONG_FILE_NAME:
            syserr("file transfer: wrong file name");
            break;

            case WRONG_FRAGMENT_ADDRESS:
            syserr("file transfer: wrong fragment address");
            break;

            case NO_FRAGMENT_SIZE:
            syserr("file transfer: no fragment size");
            break;

            default:
            syserr("server_msg unknown error");
        }
    }
    #ifdef DEBUG
        printf("server msg received\n");
    #endif
}


void receive_files_names(int sockfd, char files_names_buffer[], uint32_t *list_len) {
    ssize_t read_curr;
    int32_t read_all = 0, remains;
    struct msg_server server_msg;

    receive_server_msg(sockfd, &server_msg);
    *list_len = server_msg.param;

    do {
        remains = *list_len - read_all;
        read_curr = read(sockfd, files_names_buffer + read_all, remains);
        if (read_curr < 0)
            syserr("reading files names list");

        read_all += read_curr;
    } while (read_curr > 0);
}

void display_files_names_list(char files_names_buffer[], int32_t len) {
    uint16_t id = 0;
    for (int i = 0; i < len; i++) {
        printf("%d.", id++);
        while (i < len && files_names_buffer[i] != '|')
            printf("%c", files_names_buffer[i++]);
        printf("\n");
    }
}

void read_user_command(struct user_command *comm) {
  comm->file_id = 2;
  comm->start_addr = 1;
  comm->end_addr = 10;
/*    scanf("%d", &comm->file_id);
    scanf("%d", &comm->start_addr);
    scanf("%d", &comm->end_addr);*/
}

/// Returns file's name length
uint16_t fill_buffer_with_file_name(int32_t file_id, const char files_names_buffer[], char buffer[]) {
#ifdef DEBUG
    printf("filling buffer with file name\n");
#endif
    int32_t curr_file_id = 0;
    int32_t file_name_length = 0;
    int32_t i = 0;

    while (curr_file_id < file_id) {
        if (files_names_buffer[i] == '|')
            curr_file_id++;
        i++;
    }

    int32_t buff_index = sizeof(struct file_fragment_request);
    while (files_names_buffer[i] != '|') {
        buffer[buff_index++] = files_names_buffer[i++];
        file_name_length++;
    }

#ifdef DEBUG
    printf("filling buffer with file name finished\n");
    printf("file name: ");
    for (i = sizeof(struct file_fragment_request); i < sizeof(struct file_fragment_request) + file_name_length; i++)
        printf("%c", buffer[i]);
    printf("\n");
#endif
    return file_name_length;
}

/// Returns data length in buffer
size_t fill_buffer_with_fragment_request(const struct user_command* comm, char files_names_buffer[], char buffer[]) {
#ifdef DEBUG
    printf("setting file fragment request\n");
#endif
    struct file_fragment_request msg;
    size_t file_name_length;

    msg.msg_type = htons(FILE_FRAGMENT_REQUEST);
    msg.start_addr = htonl(comm->start_addr);
    msg.bytes_to_send = htonl(comm->end_addr - comm->start_addr + 1);
    file_name_length = fill_buffer_with_file_name(comm->file_id, files_names_buffer, buffer);
    msg.file_name_len = htons(file_name_length);

    memcpy(buffer, &msg, sizeof(struct file_fragment_request));

#ifdef DEBUG
    printf("msg: %d %d %d %d\n", ntohs(msg.msg_type), ntohl(msg.start_addr), ntohl(msg.bytes_to_send), ntohs(msg.file_name_len));
    printf("\nfile fragment request set\n");
#endif
    return sizeof(struct file_fragment_request) + file_name_length;
}

void send_file_fragment_request(int sockfd, struct user_command* comm, char files_names_buffer[], char buffer[]) {
    size_t data_len = fill_buffer_with_fragment_request(comm, files_names_buffer, buffer);

    if (write(sockfd, buffer, data_len) != data_len)
        syserr("partial / failed write");
}

void receive_file_fragment(int sockfd, char buffer[]) {

}

int main(int argc, char *argv[]) {
    int sockfd;
    uint32_t list_len = 0;
    char files_names_buffer[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    struct user_command comm;
    check_argc(argc, argv);
    //char const *port = argc == 3 ? argv[2] : DEF_PORT_NUM;
    char const *port = "6543";
#ifdef DEBUG
    printf("port num = %s\n", port);
#endif
    connect_with_server(&sockfd, argv[1], port);
#ifdef DEBUG
    printf("server connected\n");
#endif

    send_files_names_request(sockfd);
    receive_files_names(sockfd, files_names_buffer, &list_len);
    display_files_names_list(files_names_buffer, list_len);
    read_user_command(&comm);
    send_file_fragment_request(sockfd, &comm, files_names_buffer, buffer);

    return 0;
}
