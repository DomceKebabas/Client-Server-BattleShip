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

#include "battleship_logic.h"

void* handle_client(void* arg){
    int client_socket = *(int*)arg;
    free(arg);

    Game game;
    place_ships(&game);

    char buffer[1024];
    while (1) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) break;

        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "SHOOT|", 6) == 0) {
            char row = buffer[6];
            int col = atoi(&buffer[7]);
            const char* result = shoot(&game, row, col);

            char reply[2048];
            char board_str[SIZE*SIZE+1];
            board_to_string(&game, board_str);

            sprintf(reply, "RESULT|%s\nBOARD|%s\n", result, board_str);

            if (game.ships_remaining == 0)
                strcat(reply, "STATUS|WIN\n");

            send(client_socket, reply, strlen(reply), 0);
        }
    }

    close(client_socket);
    return NULL;
}

int main() {

    //create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) { perror("socket"); exit(1); }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;           //IPv4
    server_addr.sin_port = htons(8080); //port =8080
    server_addr.sin_addr.s_addr = INADDR_ANY;   //ip = any

    //bind socket to port
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(server_socket, 5) < 0) {
        perror("listen"); exit(1);
    }

    printf("Battleship server listening on port 8080...\n");

    while (1) {
      int* client_socket = malloc(sizeof(int));
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
