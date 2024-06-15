#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

#define MAX_CLIENTS 100

typedef struct {
    int fd;
    int authenticated;
} client_t;

void encrypt_message(char *message, int len) {
    for (int i = 0; i < len; ++i) {
        message[i] ^= 0xAA;  
    }
}

void broadcast_message(int sender_socket, char *message, int message_len, client_t *clients, int client_count) {
    for (int i = 0; i < client_count; ++i) {
        if (clients[i].fd != sender_socket) {
            if (!clients[i].authenticated) {
                char encrypted_message[1024];
                strncpy(encrypted_message, message, message_len);
                encrypt_message(encrypted_message, message_len);
                send(clients[i].fd, encrypted_message, message_len, 0);
            } else {
                send(clients[i].fd, message, message_len, 0);
            }
        }
    }
    printf("Broadcast message from %d: %.*s\n", sender_socket, message_len, message);
}

int main(int argc, char **argv) {
    if (argc != 4 || (strcmp(argv[1], "--password") != 0 && strcmp(argv[1], "-p") != 0)) {
        fprintf(stderr, "Usage: %s --password <password> <port>\n", argv[0]);
        return 1;
    }

    const char *PASSWORD = argv[2];
    const int PORT = atoi(argv[3]);

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

    struct pollfd fds[MAX_CLIENTS + 1];
    client_t clients[MAX_CLIENTS];
    fds[0].fd = server_socket;
    fds[0].events = POLLIN;
    int client_count = 1;

    char recv_buf[1024];
    int recv_size;

    while (1) {
        int poll_count = poll(fds, client_count, -1);
        if (poll_count == -1) {
            perror("poll");
            break;
        }

        for (int i = 0; i < client_count; ++i) {
            if (fds[i].revents & POLLIN) {
                if (fds[i].fd == server_socket) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
                    if (client_socket == -1) {
                        perror("accept");
                    } else {
                        memset(recv_buf, 0, sizeof(recv_buf));
                        recv_size = recv(client_socket, recv_buf, sizeof(recv_buf), 0);
                        if (recv_size > 0) {
                            clients[client_count - 1].fd = client_socket;
                            clients[client_count - 1].authenticated = (strncmp(recv_buf, PASSWORD, strlen(PASSWORD)) == 0);
                            if (client_count < MAX_CLIENTS + 1) {
                                fds[client_count].fd = client_socket;
                                fds[client_count].events = POLLIN;
                                client_count++;
                                printf("New client connected\n");
                            } else {
                                printf("Max clients reached. Rejecting new client.\n");
                                close(client_socket);
                            }
                        } else {
                            printf("Invalid password. Connection rejected.\n");
                            close(client_socket);
                        }
                    }
                } else {
                    memset(recv_buf, 0, sizeof(recv_buf));
                    recv_size = recv(fds[i].fd, recv_buf, sizeof(recv_buf), 0);
                    if (recv_size > 0) {
                        printf("Received message: %.*s\n", recv_size, recv_buf);
                        broadcast_message(fds[i].fd, recv_buf, recv_size, clients, client_count - 1);
                    } else if (recv_size == 0) {
                        printf("Client disconnected\n");
                        close(fds[i].fd);
                        fds[i] = fds[client_count - 1];
                        client_count--;
                    }
                }
            }
        }
    }

    for (int i = 0; i < client_count; ++i) {
        close(fds[i].fd);
    }
    close(server_socket);
    return 0;
}