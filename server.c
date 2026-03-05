#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>

void* handle_client(void* arg){
    int client_socket = *(int*)arg;
    free(arg);

    char buffer[1024];
    while (1) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            printf("Client disconnected\n");
            break;
        }
        buffer[bytes_received] = '\0';
        printf("Client: %s\n", buffer);

        char reply[1024];
        sprintf(reply, "Server received: %s", buffer);
        send(client_socket, reply, strlen(reply), 0);
    }

    close(client_socket);
    return NULL;
}

int main() {

    //create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket < 0) {
        printf("socket creation failed\n");
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;           //IPv4
    server_addr.sin_port = htons(8080); //port =8080
    server_addr.sin_addr.s_addr = INADDR_ANY;   //ip = any

    //bind socket to port
    if (bind(server_socket,
        (struct sockaddr *)&server_addr,
        sizeof(server_addr)) < 0) {
        printf("bind failed\n");
        exit(1);
        }

    //listen for connections
    if (listen(server_socket,5) < 0) {
        printf("listen failed\n");
        exit(1);
        }

    while (1) {
      int* client_socket = malloc(sizeof(int));
        *client_socket = accept(server_socket, NULL, NULL);
        if (*client_socket < 0) {
            printf("accept failed\n");
            free(client_socket);
            continue;
        }
        printf("Client connected\n");

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_socket);
        pthread_detach(tid);
    }

    close(server_socket);
    return 0;
}
