//
// Created by domcekebabas on 3/5/26.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main()
{
    int sock = socket(AF_INET,SOCK_STREAM,0);

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(connect(sock,(struct sockaddr*)&server,sizeof(server)) < 0) {
        printf("Connection failed\n");
        exit(1);
    }
    printf("Connection established\n");

    char message[1024];
    while(1) {
        printf("Enter message or 'exit' to quit: ");
        fgets(message, sizeof(message), stdin);

        if (strncmp(message, "exit", 4) == 0)
            break;

        send(sock, message, strlen(message),0);

        char reply[1024];
        int bytes_received = recv(sock, reply, sizeof(reply) - 1, 0);
        if (bytes_received <= 0) {
            printf("Connection closed\n");
            break;
        }

        reply[bytes_received] = '\0';
        printf("%s\n", reply);
    }
    close(sock);
    return 0;
}
