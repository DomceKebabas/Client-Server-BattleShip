///
/// @author Dominykas Nemanis on 3/5/26.
///

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>

#include "battleship_logic.h"

void* handle_client(void* arg) {
    int client_socket = *(int*)arg;
    free(arg);

    Game board;
    place_ships(&board, time(NULL) ^ client_socket);

    send(client_socket, "STATUS|START\n", 13, 0);

    char buffer[1024];
    while (1) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);    //waiting for client to send message
        if (bytes_received <= 0) {
            printf("Client disconnected\n");
            break;
        }
        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "SHOOT|", 6) != 0) {
            send(client_socket, "ERROR|UNKNOWN_COMMAND\n", 22, 0);
            continue;
        }

        char row = toupper((unsigned char)buffer[6]);
        int  col  = atoi(&buffer[7]);

        if (row < 'A' || row > 'J' || col < 1 || col > 10) {
            send(client_socket, "ERROR|INVALID_COORDINATES Use A-J and 1-10 (e.g. B7)\n", 53, 0);
            continue;
        }

        const char* result = shoot(&board, row, col);

        char reply[256];

        if (strcmp(result, "SUNK_ALL") == 0) {
            sprintf(reply, "RESULT|SUNK\nSTATUS|WIN\n");
            send(client_socket, reply, strlen(reply), 0);
            break;
        }

        sprintf(reply, "RESULT|%s\nSTATUS|YOUR_TURN\n", result);
        send(client_socket, reply, strlen(reply), 0);
    }

    close(client_socket);
    return NULL;
}

int main() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0); // AF_INET = IPv4, SOCK_STREAM = TCP
    if (server_socket < 0) { perror("socket"); exit(1); }

    int opt = 1;    // to deal with address already in use. Bassicaly lets server run on the same port.
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(server_socket, 5) < 0) {
        perror("listen"); exit(1);
    }

    printf("Battleship server listening on port 8080...\n");

    while (1) {
        int* client_socket = malloc(sizeof(int));   //for different sockets.
        *client_socket = accept(server_socket, NULL, NULL);
        if (*client_socket < 0) { free(client_socket); continue; }

        printf("Client connected\n");
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_socket);
        pthread_detach(tid);
    }

    close(server_socket);
    return 0;
}