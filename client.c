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

#define BUFFER_SIZE 2048

int main()
{
    int sock = socket(AF_INET,SOCK_STREAM,0);
    if(sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(connect(sock,(struct sockaddr*)&server,sizeof(server)) < 0) {
        printf("Connection failed\n");
        exit(1);
    }
    printf("Connection established with the server.\n");

    char buffer[BUFFER_SIZE];
    char input[10];

    while(1) {
        printf("Enter coordinates to shoot (A1 - J10) or 'exit' to quit: ");
        fgets(input, sizeof(input), stdin);

        if (strncmp(buffer, "exit", 4) == 0)
            break;

        input[strcspn(input, "\n")] = 0;//to delete \n from input

        char command[20];
        sprintf(command, "SHOOT|%s", input);

        send(sock, command, strlen(command),0);

        // get response from the server
        int bytes_received = recv(sock, buffer, sizeof(buffer)-1, 0);
        if (bytes_received <= 0) {
            printf("Connection closed by server\n");
            break;
        }

        buffer[bytes_received] = '\0';
        printf("\nServer reply:\n%s\n", buffer);

        // If STATUS|WIN is the answer - game ends.
        if (strstr(buffer, "STATUS|WIN") != NULL) {
            printf("You sank all ships! You win!\n");
            break;
        }
    }

    close(sock);
    return 0;
}
