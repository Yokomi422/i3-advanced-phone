## 班員へ

`client`フォルダにクライアント関係、`server`フォルダに server 関係のコードを書てください。
ボイスチェンジや暗号化などの拡張機能は `plugins` フォルダにしまってください。

## `simple`なものたち

一対一の通信

```
gcc -o simple_client simple_client.c
gcc -o simple_server simple_server.c

./simple_server [port]
./simple_client [ip addr] [port]
```

とすることで、一対一の通信が可能です。

## 多人数の通信

```
gcc -o client_multiple_users client_multiple_users.c
gcc -o server_multiple_users server_multiple_users.c

./simple_server [port]

./simple_client [ip addr] [port]
// 別のポートで
./simple_client [ip addr] [port]
// and so on
```

こちらは、server が仲介することで、複数のクライアントが通信できるようになります。
server はただデータをクライアントに転送するだけです。
