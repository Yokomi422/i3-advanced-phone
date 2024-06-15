#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

int main(int argc, char **argv) {
    if (argc != 5 || (strcmp(argv[3], "--password") != 0 && strcmp(argv[3], "-p") != 0)) {
        fprintf(stderr, "Usage: %s <ip> <port> --password <password>\n", argv[0]);
        return 1;
    }

    const char *IP_ADDR = argv[1];
    const int PORT = atoi(argv[2]);
    const char *PASSWORD = argv[4];

    int socket_fd = socket(PF_INET, SOCK_STREAM, 0);
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

    // Send password
    send(socket_fd, PASSWORD, strlen(PASSWORD), 0);

    // poll構造体の定義
    struct pollfd fds[2];
    // 受信用のfd
    fds[0].fd = socket_fd;
    fds[0].events = POLLIN;
    // 送信用のfd
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;

    char recv_buf[1024], send_buf[1024];
    int recv_size, send_size;

    while (1) {
        int poll_count = poll(fds, 2, -1);
        if (poll_count == -1) {
            perror("poll");
            break;
        }

        if (fds[0].revents) {
            recv_size = recv(socket_fd, recv_buf, sizeof(recv_buf), 0);
            if (recv_size > 0) {
                write(STDOUT_FILENO, recv_buf, recv_size);
            } else if (recv_size == 0) {
                printf("Server disconnected\n");
                break;
            }
        }

        if (fds[1].revents) {
            send_size = read(STDIN_FILENO, send_buf, sizeof(send_buf));
            if (send_size > 0) {
                send(socket_fd, send_buf, send_size, 0);
            }
        }
    }

    close(socket_fd);
    return 0;
}