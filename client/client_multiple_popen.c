#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

typedef struct {
    int socket_fd;
    FILE *listening_fd;
    FILE *speaking_fd;
} client_info_t;

void *receive_messages(void *arg) {
    client_info_t *client_info = (client_info_t *)arg;
    int socket_fd = client_info->socket_fd;
    char recv_buf[BUFFER_SIZE];
    int recv_size;

    while ((recv_size = recv(socket_fd, recv_buf, sizeof(recv_buf), 0)) > 0) {
        fwrite(recv_buf, 1, recv_size, client_info->speaking_fd);
    }

    pclose(client_info->listening_fd);
    pclose(client_info->speaking_fd);
    close(socket_fd);
    free(client_info);
    pthread_exit(NULL);
}

void *send_messages(void *arg) {
    client_info_t *client_info = (client_info_t *)arg;
    int socket_fd = client_info->socket_fd;
    char send_buf[BUFFER_SIZE];
    int send_size;

    while ((send_size = fread(send_buf, 1, sizeof(send_buf), client_info->listening_fd)) > 0) {
        send(socket_fd, send_buf, send_size, 0);
    }

    pclose(client_info->listening_fd);
    pclose(client_info->speaking_fd);
    close(socket_fd);
    free(client_info);
    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
        return 1;
    }

    const char *IP_ADDR = argv[1];
    const int PORT = atoi(argv[2]);

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(IP_ADDR);
    addr.sin_port = htons(PORT);

    if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(socket_fd);
        return 1;
    }

    FILE *listening_fd = popen("rec -b 16 -c 1 -e s -r 44100 -t raw - ", "r");
    FILE *speaking_fd = popen("play -b 16 -c 1 -e s -r 44100 -t raw - ", "w");

    if (listening_fd == NULL || speaking_fd == NULL) {
        perror("popen");
        close(socket_fd);
        return 1;
    }

    pthread_t recv_thread, send_thread;
    client_info_t *client_info = malloc(sizeof(client_info_t));
    client_info->socket_fd = socket_fd;
    client_info->listening_fd = listening_fd;
    client_info->speaking_fd = speaking_fd;

    pthread_create(&recv_thread, NULL, receive_messages, client_info);
    pthread_create(&send_thread, NULL, send_messages, client_info);

    pthread_join(recv_thread, NULL);
    pthread_join(send_thread, NULL);

    return 0;
}