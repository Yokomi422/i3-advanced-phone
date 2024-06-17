#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#define N 1000

int main(int argc, char *argv[]) {
    if (argc > 3 || argc < 2) {
        fprintf(stderr, "Usage: %s <Port Number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Server Mode
    if (argc == 2) {
        int port_number = atoi(argv[1]);
        int ss = socket(PF_INET, SOCK_STREAM, 0);
        if (ss == -1) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_number);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(ss, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            perror("bind failed");
            close(ss);
            exit(EXIT_FAILURE);
        }

        if (listen(ss, 10) == -1) {
            perror("listen failed");
            close(ss);
            exit(EXIT_FAILURE);
        }

        fd_set read_fds;
        fd_set master_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&master_fds);
        FD_SET(ss, &master_fds);
        int fd_max = ss;

        while (1) {
            read_fds = master_fds;
            if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == -1) {
                perror("select failed");
                exit(EXIT_FAILURE);
            }

            for (int i = 0; i <= fd_max; i++) {
                if (FD_ISSET(i, &read_fds)) {
                    if (i == ss) {
                        // Handle new connections
                        struct sockaddr_in client_addr;
                        socklen_t addrlen = sizeof(client_addr);
                        int new_fd = accept(ss, (struct sockaddr *)&client_addr, &addrlen);
                        if (new_fd == -1) {
                            perror("accept failed");
                        } else {
                            FD_SET(new_fd, &master_fds);
                            if (new_fd > fd_max) {
                                fd_max = new_fd;
                            }
                        }
                    } else {
                        // Handle data from a client
                        short data_recv[N];
                        int n = recv(i, data_recv, sizeof(data_recv), 0);
                        if (n <= 0) {
                            if (n == 0) {
                                printf("socket %d hung up\n", i);
                            } else {
                                perror("recv failed");
                            }
                            close(i);
                            FD_CLR(i, &master_fds);
                        } else {
                            // Forward data to all other clients
                            for (int j = 0; j <= fd_max; j++) {
                                if (FD_ISSET(j, &master_fds) && j != ss && j != i) {
                                    if (send(j, data_recv, n, 0) == -1) {
                                        perror("send failed");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        close(ss);
    }

    // Client Mode
    if (argc == 3) {
        char *IP = argv[1];
        int port_number = atoi(argv[2]);

        int s = socket(PF_INET, SOCK_STREAM, 0);
        if (s == -1) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_number);
        if (inet_pton(AF_INET, IP, &addr.sin_addr) <= 0) {
            perror("IP address conversion failed");
            exit(EXIT_FAILURE);
        }

        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            perror("connect failed");
            close(s);
            exit(EXIT_FAILURE);
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(s, &read_fds);

        while (1) {
            fd_set tmp_fds = read_fds;
            if (select(s + 1, &tmp_fds, NULL, NULL, NULL) == -1) {
                perror("select failed");
                close(s);
                exit(EXIT_FAILURE);
            }

            if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
                short data_send[N];
                int r = read(STDIN_FILENO, data_send, sizeof(data_send));
                if (r == -1) {
                    perror("read from stdin failed");
                    close(s);
                    exit(EXIT_FAILURE);
                }

                if (send(s, data_send, r, 0) == -1) {
                    perror("send failed");
                    close(s);
                    exit(EXIT_FAILURE);
                }

                if (r == 0) {
                    printf("EOF sent\n");
                    break;
                }
            }

            if (FD_ISSET(s, &tmp_fds)) {
                short data_recv[N];
                int n = recv(s, data_recv, sizeof(data_recv), 0);
                if (n <= 0) {
                    if (n == 0) {
                        printf("server hung up\n");
                    } else {
                        perror("recv failed");
                    }
                    close(s);
                    exit(EXIT_FAILURE);
                }
                write(STDOUT_FILENO, data_recv, n);
            }
        }
        close(s);
    }
    return 0;
}


//Server : rec -t raw -b 16 -c 1 -e s -r 44100 - | ./phone3 50000 | play -t raw -b 16 -c 1 -e s -r 44100 -

//Client : rec -t raw -b 16 -c 1 -e s -r 44100 - | ./phone3 <Server IP> 50000 | play -t raw -b 16 -c 1 -e s -r 44100 -
