#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define TEXT_PREFIX "TEXT:"

typedef struct {
    int socket_fd;
    FILE *listening_fd;
    FILE *speaking_fd;
    char name[50];
} client_info_t;

void *receive_messages(void *arg) {
    client_info_t *client_info = (client_info_t *)arg;
    int socket_fd = client_info->socket_fd;
    char recv_buf[BUFFER_SIZE];
    int recv_size;

    while ((recv_size = recv(socket_fd, recv_buf, sizeof(recv_buf), 0)) > 0) {
        fwrite(recv_buf, 1, recv_size, client_info->speaking_fd);
    }

    // control + cで終了したとき、スレッドを終了する
    pclose(client_info->listening_fd);
    pclose(client_info->speaking_fd);
    close(socket_fd);
    free(client_info);
    pthread_exit(NULL);
}

void *send_audio_messages(void *arg) {
    client_info_t *client_info = (client_info_t *)arg;
    int socket_fd = client_info->socket_fd;
    char send_buf[BUFFER_SIZE];
    int send_size;

    while ((send_size = fread(send_buf, 1, sizeof(send_buf), client_info->listening_fd)) > 0) {
        send(socket_fd, send_buf, send_size, 0);
    }

    pclose(client_info->listening_fd);
    close(socket_fd);
    free(client_info);
    pthread_exit(NULL);
}

void *handle_terminal_input(void *arg) {
    client_info_t *client_info = (client_info_t *)arg;
    int socket_fd = client_info->socket_fd;
    char send_buf[BUFFER_SIZE];
    char prefixed_buf[BUFFER_SIZE + 6]; 
    int send_size;

    while (fgets(send_buf, sizeof(send_buf), stdin) != NULL) {
        // 入力された文字列の先頭に"TEXT:"を付与して送信
        snprintf(prefixed_buf, sizeof(prefixed_buf), "%s%s", TEXT_PREFIX, send_buf);
        send_size = strlen(prefixed_buf);
        send(socket_fd, prefixed_buf, send_size, 0);
    }

    close(socket_fd);
    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    if (argc != 4) { 
        fprintf(stderr, "Usage: %s <ip> <port> <name>\n", argv[0]);
        return 1;
    }

    const char *IP_ADDR = argv[1];
    const int PORT = atoi(argv[2]);
    const char *name = argv[3];

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

    FILE *listening_fd = popen("rec -r 48000 -b 16 -c 1 -e signed-integer -t raw - 2>/dev/null", "r");
    FILE *speaking_fd = popen("play -r 48000 -b 16 -c 1 -e signed-integer -t raw - 2>/dev/null", "w");

    if (listening_fd == NULL || speaking_fd == NULL) {
        perror("popen");
        close(socket_fd);
        return 1;
    }

    client_info_t *client_info = malloc(sizeof(client_info_t));
    client_info->socket_fd = socket_fd;
    client_info->listening_fd = listening_fd;
    client_info->speaking_fd = speaking_fd;
    strncpy(client_info->name, name, sizeof(client_info->name) - 1); 
    client_info->name[sizeof(client_info->name) - 1] = '\0';

    send(socket_fd, client_info->name, strlen(client_info->name), 0);

    pthread_t recv_thread, send_audio_thread, terminal_thread;
    pthread_create(&recv_thread, NULL, receive_messages, client_info);
    pthread_create(&send_audio_thread, NULL, send_audio_messages, client_info);
    pthread_create(&terminal_thread, NULL, handle_terminal_input, client_info);

    pthread_join(recv_thread, NULL);
    pthread_join(send_audio_thread, NULL);
    pthread_join(terminal_thread, NULL);

    return 0;
}