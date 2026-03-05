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

int waiting_socket = -1;
GameSession* pending_session = NULL;
pthread_mutex_t lobby_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t lobby_cond = PTHREAD_COND_INITIALIZER;

void* handle_client(void* arg){
    int client_socket = *(int*)arg;
    free(arg);

    GameSession* session = NULL;

    pthread_mutex_lock(&lobby_mutex);

    if (waiting_socket == -1) {
        waiting_socket = client_socket;
        send(client_socket, "STATUS|WAITING\n", 15, 0);

        pthread_cond_wait(&lobby_cond, &lobby_mutex);
        session = pending_session;
        pthread_mutex_unlock(&lobby_mutex);
    } else {
        session = malloc(sizeof(GameSession));
        session->socket[0] = waiting_socket;
        session->socket[1] = client_socket;
        session->current_turn = 0;
        session->active = 1;

        place_ships(&session->board[0]);
        place_ships(&session->board[1]);

        waiting_socket = -1;
        pending_session = session;                     // forwards session to the first one

        pthread_cond_signal(&lobby_cond);              // "wakesup" player 1
        pthread_mutex_unlock(&lobby_mutex);

        // Tells both game is started.
        send(session->socket[0], "STATUS|START|YOUR_TURN\n", 23, 0);
        send(session->socket[1], "STATUS|START|WAIT\n", 18, 0);
    }

    int my_index = (client_socket == session->socket[0]) ? 0 : 1;
    int opponent_index = 1 - my_index;

    char buffer[1024];
    while (1) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            send(session->socket[opponent_index], "STATUS|DISCONNECT\n", 18, 0);
            break;
        }

        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "SHOOT|", 6) == 0) {

            pthread_mutex_lock(&lobby_mutex);
            if (session->current_turn != my_index) {
                pthread_mutex_unlock(&lobby_mutex);
                send(client_socket, "ERROR|NOT_YOUR_TURN\n", 20, 0);
                continue;
            }

            char row = buffer[6];
            int col = atoi(&buffer[7]);

            const char* result = shoot(&session->board[opponent_index], row, col);

            session->current_turn = opponent_index;
            pthread_mutex_unlock(&lobby_mutex);

            char my_reply[256];
            char opponent_reply[256];

            sprintf(my_reply, "RESULT|%s\n", result);
            sprintf(opponent_reply, "OPPONENT_SHOT|%c%d|%s\n", row, col, result);

            if (session->board[opponent_index].ships_remaining == 0) {
                strcat(my_reply, "STATUS|WIN\n");
                strcat(opponent_reply, "STATUS|LOSE\n");
            } else {
                strcat(my_reply, "STATUS|WAIT\n");
                strcat(opponent_reply, "STATUS|YOUR_TURN\n");
            }

            send(client_socket, my_reply, strlen(my_reply), 0);
            send(session->socket[opponent_index], opponent_reply, strlen(opponent_reply), 0);
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
