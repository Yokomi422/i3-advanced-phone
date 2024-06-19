#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define TEXT_PREFIX "TEXT:"
#define COMMAND_PREFIX "Command: "

typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
    char name[50];
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

void send_message(int client_socket, char *message) {
    send(client_socket, message, strlen(message), 0);
}

char *trim_whitespace(char *str) {
    char *end;

    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) 
        return str;

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    end[1] = '\0';

    return str;
}

void *handle_client(void *arg) {
    client_info_t *client_info = (client_info_t *)arg;
    int client_socket = client_info->client_socket;
    char buffer[BUFFER_SIZE];
    int recv_size;

    FILE *speaking_fd = popen("play -r 48000 -b 16 -c 1 -e s -t raw - 2>/dev/null", "w");

    if (speaking_fd == NULL) {
        perror("popen");
        close(client_socket);
        free(client_info);
        return NULL;
    }

    recv_size = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    buffer[recv_size] = '\0';
    strncpy(client_info->name, buffer, sizeof(client_info->name) - 1);
    client_info->name[sizeof(client_info->name) - 1] = '\0';

    printf("Client connected: %s\n", client_info->name);

    while ((recv_size = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[recv_size] = '\0';

        if (strncmp(buffer, TEXT_PREFIX, strlen(TEXT_PREFIX)) == 0) {
            char *text_message = buffer + strlen(TEXT_PREFIX);
            printf("Received text message from %s: %s\n", client_info->name, text_message);

            if (strncmp(text_message, COMMAND_PREFIX, strlen(COMMAND_PREFIX)) == 0) {
                char *command = text_message + strlen(COMMAND_PREFIX);
                command = trim_whitespace(command); 
                printf("Command from %s: '%s'\n", client_info->name, command);  
                char response[BUFFER_SIZE];

                if (strcmp(command, "test") == 0) {
                    snprintf(response, sizeof(response), "nice command!!!");
                } else if (strcmp(command, "happy") == 0) {
                    snprintf(response, sizeof(response), "best command!!!");
                } else {
                    snprintf(response, sizeof(response), "your command is %s", command);
                }

                send_message(client_socket, response);
            } else {
                broadcast_message(client_socket, buffer, recv_size);
            }
        } else {
            fwrite(buffer, 1, recv_size, speaking_fd);
            broadcast_message(client_socket, buffer, recv_size);
        }
    }

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
    printf("Client disconnected: %s\n", client_info->name);
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
            for (i = 0; i < MAX_CLIENTS; i++) {
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