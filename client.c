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
#include <ctype.h>

#define BUFFER_SIZE 2048

volatile int game_over = 0; // lets chenge the int from different thread.

void* receive_thread(void* arg) {
    int sock = *(int*)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            printf("Connection closed by server\n");
            game_over = 1;
            break;
        }
        buffer[bytes_received] = '\0';
        printf("\n%s", buffer);

        if (strstr(buffer, "STATUS|WIN") != NULL) { //to find string in string
            printf("You sank all ships! You WIN!\n");
            game_over = 1;
            break;
        }
    }

    pthread_exit(NULL);
    return NULL;
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0); //same as in server
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Connection failed\n");
        exit(1);
    }
    printf("Connected to server.\n");

    // on thread for receiving messages and one for waiting user inp
    pthread_t tid;
    pthread_create(&tid, NULL, receive_thread, &sock);
    pthread_detach(tid);

    char input[10];
    while (1) {
        if (game_over) break;

        printf("Enter coordinates (A1-J10): ");
        fgets(input, sizeof(input), stdin);

        // If input was too long, newline wont be in buffer
        if (strcspn(input, "\n") == sizeof(input) - 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
        }

        input[strcspn(input, "\n")] = '\0';

        if (strncmp(input, "exit", 4) == 0) break;
        if (game_over) break;

        char row = toupper((unsigned char)input[0]);
        int  col  = atoi(input + 1);

        if (row < 'A' || row > 'J' || col < 1 || col > 10) {
            printf("Invalid input. Use A-J and 1-10 (e.g. B7 or J10).\n");
            continue;
        }

        char command[20];
        sprintf(command, "SHOOT|%c%d\n", row, col);
        send(sock, command, strlen(command), 0);
    }

    printf("Game ended. Goodbye!\n");
    close(sock);
    return 0;
}