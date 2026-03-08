///
/// @author Dominykas Nemanis on 3/5/26.
///

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define BUFFER_SIZE 2048

void* receive_thread(void* arg) {
    int sock = *(int*)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            printf("Connection closed by server\n");
            break;
        }
        buffer[bytes_received] = '\0';
        printf("\n%s", buffer);

        if (strstr(buffer, "STATUS|WIN") != NULL) {
            printf("You win!\n");
            break;
        }
        if (strstr(buffer, "STATUS|LOSE") != NULL) {
            printf("You lose!\n");
            break;
        }
        if (strstr(buffer, "STATUS|DISCONNECT") != NULL) {
            printf("Opponent disconnected!\n");
            break;
        }
    }

    close(sock);
    exit(0);
    return NULL;
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Connection failed\n");
        exit(1);
    }
    printf("Connection established with the server.\n");

    // Sukuriam receive thread
    pthread_t tid;
    pthread_create(&tid, NULL, receive_thread, &sock);
    pthread_detach(tid);

    char input[10];
    while (1) {
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;

        if (strncmp(input, "exit", 4) == 0)
            break;

        char command[20];
        sprintf(command, "SHOOT|%s\n", input);
        send(sock, command, strlen(command), 0);
    }

    close(sock);
    return 0;
}
