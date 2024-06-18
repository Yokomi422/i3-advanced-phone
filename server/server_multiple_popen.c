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

/*
client_socket: int は、client_socketのファイルディスクリプタ、clientを識別するために使う
client_addr: struct sockaddr_in は、clientのアドレス情報を保持する構造体
*/
typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
} client_info_t;

int client_sockets[MAX_CLIENTS];
/*
Mutexについて
マルチスレッド(並列処理)をする際に、複数のスレッドが同時に同じデータにアクセスして
データの不整合が起こることを防ぐために使われる
参考
- https://daeudaeu.com/c_mutex/
*/
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

    // popenで子プロセスの作成 speaking_fdでserverがclientに音声を送る
    FILE *speaking_fd = popen("play -r 48000 -b 16 -c 1 -e s -t raw - 2>/dev/null", "w");

    if (speaking_fd == NULL) {
        perror("popen");
        close(client_socket);
        free(client_info);
        return NULL;
    }

    while ((recv_size = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[recv_size] = '\0';

        // "TEXT: "で始めるデータはテキスト
        if (strncmp(buffer, TEXT_PREFIX, strlen(TEXT_PREFIX)) == 0) {
            // TEXT_PREFIXを取り除く
            char *text_message = buffer + strlen(TEXT_PREFIX);
            printf("Received text message: %s\n", text_message);

            if (strncmp(text_message, COMMAND_PREFIX, strlen(COMMAND_PREFIX)) == 0) {
                char *command = text_message + strlen(COMMAND_PREFIX);
                command = trim_whitespace(command); 
                printf("Your typed command: '%s'\n", command);  
                char response[BUFFER_SIZE];

                if (strcmp(command, "test") == 0) {
                    snprintf(response, sizeof(response), "nice command!!!");
                    printf("Response: nice command!!!\n");
                } else if (strcmp(command, "happy") == 0) {
                    snprintf(response, sizeof(response), "best command!!!");
                    printf("Response: best command!!!\n");
                } else {
                    snprintf(response, sizeof(response), "your command is %s", command);
                    printf("Response: your command is %s\n", command);
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
    free(client_info);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int PORT = atoi(argv[1]);
    // socket(プロトコルの種類、socket type、protocol type)  AF_INET: IPv4 インターネットプロトコル SOCK_STREAM: TCP
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    // socketが失敗すると、-1を返す. 成功するとsocket descriptor: intを返し、後で識別するのに使う
    if (server_socket == -1) {
        perror("socket");
        return 1;
    }

    // ネットワーク層レベルの通信に必要な情報を保持する構造体(sockaddr_in)を作成
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // サーバはすべてのIPから受け取る
    server_addr.sin_port = htons(PORT); // サーバが起動するport

    // bindは、上で設定したserver_socketとsockeraddr_inを関連付ける 成功したら0
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_socket);
        return 1;
    }

    // server_socketをlisten状態にして、serverとしての機能を開始する
    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("listen");
        close(server_socket);
        return 1;
    }

    memset(client_sockets, 0, sizeof(client_sockets));

    while (1) {
        struct sockaddr_in client_addr;
        // socketが運ぶデータの大きさ byte
        socklen_t client_len = sizeof(client_addr);
        // acceptでclientからの接続を待つ client_socketはsocket descriptor
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) {
            perror("accept");
        } else {
            // 28行目で宣言したmutexをlockして、lockしたこのthreadだけがclient_socketsにアクセスできるようにする
            pthread_mutex_lock(&client_mutex);
            int i;
            // 新しいclient_socket(ファイルディスクリプタ)をclient_socketsに追加する
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = client_socket;
                    break;
                }
            }
            // lockを解除する
            pthread_mutex_unlock(&client_mutex);

            if (i < MAX_CLIENTS) {
                client_info_t *client_info = malloc(sizeof(client_info_t));
                client_info->client_socket = client_socket;
                client_info->client_addr = client_addr;

                // 新しくスレッドを作成する
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