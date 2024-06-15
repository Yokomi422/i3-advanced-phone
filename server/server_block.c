#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

#define MAX_CLIENTS 100
#define MAX_NAME_LEN 50

typedef struct {
    int fd;
    char name[MAX_NAME_LEN];
    char blocklist[MAX_CLIENTS][MAX_NAME_LEN];
    int blocklist_count;
} Client;

void broadcast_message(Client *clients, int sender_index, char *message, int message_len, int client_count) {
    for (int i = 1; i < client_count; ++i) { 
        if (i != sender_index) {
            int is_blocked = 0;
            for (int j = 0; j < clients[i].blocklist_count; ++j) {
                if (strcmp(clients[i].blocklist[j], clients[sender_index].name) == 0) {
                    is_blocked = 1;
                    break;
                }
            }
            if (!is_blocked) {
                send(clients[i].fd, message, message_len, 0);
            }
        }
    }
    printf("Broadcast message from %s: %.*s\n", clients[sender_index].name, message_len, message);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    const int PORT = atoi(argv[1]);

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
    Client clients[MAX_CLIENTS + 1];
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
                        if (client_count < MAX_CLIENTS + 1) {
                            fds[client_count].fd = client_socket;
                            fds[client_count].events = POLLIN;
                            clients[client_count].fd = client_socket;
                            clients[client_count].blocklist_count = 0;

                            recv_size = recv(client_socket, clients[client_count].name, MAX_NAME_LEN, 0);
                            if (recv_size > 0) {
                                clients[client_count].name[recv_size] = '\0';
                                client_count++;
                                printf("New client connected: %s\n", clients[client_count - 1].name);
                            } else {
                                close(client_socket);
                            }
                        } else {
                            printf("Max clients reached. Rejecting new client.\n");
                            close(client_socket);
                        }
                    }
                } else {
                    memset(recv_buf, 0, sizeof(recv_buf)); 
                    recv_size = recv(fds[i].fd, recv_buf, sizeof(recv_buf), 0);
                    if (recv_size > 0) {
                        printf("Received message from %s: %.*s\n", clients[i].name, recv_size, recv_buf);

                        if (strncmp(recv_buf, "--block ", 8) == 0) {
                            char block_name[MAX_NAME_LEN];
                            sscanf(recv_buf + 8, "%s", block_name);

                            strcpy(clients[i].blocklist[clients[i].blocklist_count], block_name);
                            clients[i].blocklist_count++;
                            printf("%s blocked %s\n", clients[i].name, block_name);
                        } else {
                            broadcast_message(clients, i, recv_buf, recv_size, client_count);
                        }
                    } else if (recv_size == 0) {
                        printf("Client %s disconnected\n", clients[i].name);
                        close(fds[i].fd);
                        fds[i] = fds[client_count - 1];
                        clients[i] = clients[client_count - 1];
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
