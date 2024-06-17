#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
} client_info_t;

int client_sockets[MAX_CLIENTS];
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

void broadcast_message(int sender_socket, char *message, int message_len) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_sockets[i] != 0 && client_sockets[i] != sender_socket) {
            send(client_sockets[i], message, message_len, 0);
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void *handle_client(void *arg) {
    client_info_t *client_info = (client_info_t *)arg;
    int client_socket = client_info->client_socket;
    char buffer[BUFFER_SIZE];
    int recv_size;

    FILE *listening_fd = popen("rec -b 16 -c 1 -e s -r 44100 -t raw - ", "r");
    FILE *speaking_fd = popen("play -b 16 -c 1 -e s -r 44100 -t raw - ", "w");

    if (listening_fd == NULL || speaking_fd == NULL) {
        perror("popen");
        close(client_socket);
        free(client_info);
        return NULL;
    }

    while ((recv_size = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        broadcast_message(client_socket, buffer, recv_size);
        fwrite(buffer, 1, recv_size, speaking_fd);
    }

    pclose(listening_fd);
    pclose(speaking_fd);
    close(client_socket);
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_sockets[i] == client_socket) {
            client_sockets[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
    free(client_info);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int PORT = atoi(argv[1]);
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("listen");
        close(server_socket);
        return 1;
    }

    memset(client_sockets, 0, sizeof(client_sockets));

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) {
            perror("accept");
        } else {
            pthread_mutex_lock(&client_mutex);
            int i;
            for (i = 0; i < MAX_CLIENTS; ++i) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = client_socket;
                    break;
                }
            }
            pthread_mutex_unlock(&client_mutex);

            if (i < MAX_CLIENTS) {
                client_info_t *client_info = malloc(sizeof(client_info_t));
                client_info->client_socket = client_socket;
                client_info->client_addr = client_addr;
                pthread_t thread_id;
                pthread_create(&thread_id, NULL, handle_client, client_info);
                pthread_detach(thread_id);
            } else {
                close(client_socket);
            }
        }
    }

    close(server_socket);
    return 0;
}