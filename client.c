///
/// @author Dominykas Nemanis on 3/5/26.
///
/// Terminal client. Usage: client [host] [port]
///

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>

#include "battleship_logic.h"

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT "8080"
#define PROTOCOL_VERSION "2"
#define LINE_MAX_LEN 256

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;
static char my_board[BOARD_STRING_SIZE]    = "";    // only touched by the receive thread
static char track_board[BOARD_STRING_SIZE] = "";

static void print_row(const char* cells, int r) {
    printf("%c ", 'A' + r);
    for (int c = 0; c < SIZE; c++)
        printf(" %c", cells[r * SIZE + c]);
}

static void print_boards(const char* left, const char* right, const char* left_title, const char* right_title) {
    printf("\n  %-22s    %s\n", left_title, right_title);
    printf("   ");
    for (int c = 1; c <= SIZE; c++) printf("%2d", c);
    printf("      ");
    for (int c = 1; c <= SIZE; c++) printf("%2d", c);
    printf("\n");
    for (int r = 0; r < SIZE; r++) {
        print_row(left, r);
        printf("     ");
        print_row(right, r);
        printf("\n");
    }
    printf("\n  . water   S ship   X hit   o miss\n");
}

static void print_help(void) {
    printf("Commands:\n"
           "  ai | pvp            start a game against the computer / another player\n"
           "  random              place the rest of your fleet randomly\n"
           "  place B3 h|v        place the next ship starting at B3 (h = right, v = down)\n"
           "  B7                  shoot at B7 on your turn\n"
           "  chat <text>         talk to your opponent (pvp)\n"
           "  help | exit\n");
}

static void handle_server_line(char* line) {
    char* val = strchr(line, '|');
    if (val) *val++ = '\0'; else val = (char*)"";

    if (strcmp(line, "HELLO") == 0) {
        const char* version = strchr(val, '|');     // "BATTLESHIP|2"
        version = version ? version + 1 : val;
        if (strcmp(version, PROTOCOL_VERSION) != 0)
            printf("Warning: server speaks protocol '%s', this client expects %s\n",
                   version, PROTOCOL_VERSION);
    } else if (strcmp(line, "YOURBOARD") == 0 && strlen(val) == SIZE * SIZE) {
        strcpy(my_board, val);
    } else if (strcmp(line, "TRACKING") == 0 && strlen(val) == SIZE * SIZE) {
        strcpy(track_board, val);
    } else if (strcmp(line, "REVEAL") == 0 && strlen(val) == SIZE * SIZE) {
        print_boards(my_board, val, "YOUR FLEET", "ENEMY FLEET (revealed)");
    } else if (strcmp(line, "MATCH") == 0) {
        printf("Match found: %s.\n", strcmp(val, "AI") == 0 ? "playing against the computer" : "playing against another player");
    } else if (strcmp(line, "FLEET") == 0) {
        printf("Fleet sizes: %s\n", val);
    } else if (strcmp(line, "NEXT_SHIP") == 0) {
        print_boards(my_board, track_board, "YOUR FLEET", "ENEMY WATERS");
        char* sep = strchr(val, '|');
        if (sep) *sep++ = '\0';
        printf("Place your %s (%s cells): 'place B3 h', 'place B3 v' or 'random'.\n", val, sep ? sep : "?");
    } else if (strcmp(line, "PLACED") == 0) {
        printf("%s placed.\n", val);
    } else if (strcmp(line, "RESULT") == 0) {
        printf("You fired: %s\n", val);
    } else if (strcmp(line, "SUNK_SHIP") == 0) {
        printf("You sank their %s!\n", val);
    } else if (strcmp(line, "OPPONENT_SHOT") == 0) {
        char* sep = strchr(val, '|');
        if (sep) *sep++ = '\0';
        printf("Opponent fired at %s: %s\n", val, sep ? sep : "?");
    } else if (strcmp(line, "OPPONENT_SUNK") == 0) {
        printf("They sank your %s!\n", val);
    } else if (strcmp(line, "SHOTS") == 0) {
        printf("Shots taken: %s\n", val);
    } else if (strcmp(line, "CHAT") == 0) {
        printf("Opponent: %s\n", val);
    } else if (strcmp(line, "ERROR") == 0) {
        printf("Error: %s\n", val);
    } else if (strcmp(line, "STATUS") == 0) {
        if (strcmp(val, "YOUR_TURN") == 0) {
            print_boards(my_board, track_board, "YOUR FLEET", "ENEMY WATERS");
            printf("Your turn - enter coordinates (e.g. B7).\n");
        } else if (strcmp(val, "OPPONENT_TURN") == 0) {
            printf("Opponent's turn...\n");
        } else if (strcmp(val, "WAITING_FOR_OPPONENT") == 0) {
            printf("Waiting for an opponent to join...\n");
        } else if (strcmp(val, "WAITING_FOR_OPPONENT_SETUP") == 0) {
            printf("Waiting for the opponent to place their fleet...\n");
        } else if (strcmp(val, "OPPONENT_LEFT") == 0) {
            printf("Your opponent left the game.\n");
        } else if (strcmp(val, "WIN") == 0) {
            printf("*** You WIN! ***\n");
        } else if (strcmp(val, "LOSE") == 0) {
            printf("*** You lost. ***\n");
        } else if (strcmp(val, "CHOOSE_MODE") == 0) {
            printf("Type 'ai' to play the computer or 'pvp' for another player ('help' for commands).\n");
        }
    }
}

static void* receive_thread(void* arg) {
    int sock = *(int*)arg;
    char line[LINE_MAX_LEN + 1];
    size_t line_len = 0;
    char chunk[512];

    // The server sends newline-terminated lines, but recv() may deliver
    // several of them at once or only part of one, so reassemble first.
    for (;;) {
        ssize_t n = recv(sock, chunk, sizeof(chunk), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            pthread_mutex_lock(&print_lock);
            printf("\nConnection closed by server.\n");
            fflush(stdout);
            _exit(0);
        }

        pthread_mutex_lock(&print_lock);
        for (ssize_t i = 0; i < n; i++) {
            char c = chunk[i];
            if (c == '\n') {
                if (line_len > 0 && line[line_len - 1] == '\r') line_len--;
                line[line_len] = '\0';
                line_len = 0;
                if (line[0] != '\0') handle_server_line(line);
            } else if (line_len < LINE_MAX_LEN) {
                line[line_len++] = c;
            }   // anything beyond the limit is dropped
        }
        printf("> ");
        fflush(stdout);
        pthread_mutex_unlock(&print_lock);
    }
    return NULL;
}

static int send_line(int sock, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static int send_line(int sock, const char* fmt, ...) {
    char cmd[LINE_MAX_LEN + 2];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(cmd, sizeof(cmd) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n > sizeof(cmd) - 2) n = (int)sizeof(cmd) - 2;
    cmd[n++] = '\n';
    return send(sock, cmd, (size_t)n, MSG_NOSIGNAL) < 0 ? -1 : 0;
}

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : DEFAULT_HOST;
    const char* port = argc > 2 ? argv[2] : DEFAULT_PORT;

    signal(SIGPIPE, SIG_IGN);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "Cannot resolve %s: %s\n", host, gai_strerror(rc));
        return 1;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { perror("socket"); return 1; }
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        perror("Connection failed");
        return 1;
    }
    freeaddrinfo(res);
    printf("Connected to %s:%s.\n", host, port);

    pthread_t tid;
    if (pthread_create(&tid, NULL, receive_thread, &sock) != 0) {
        fprintf(stderr, "Could not start receive thread\n");
        close(sock);
        return 1;
    }
    pthread_detach(tid);

    char input[LINE_MAX_LEN];
    for (;;) {
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;                      // EOF (Ctrl-D / closed pipe) or read error

        if (strchr(input, '\n') == NULL) {      // over-long line: drop the rest of it
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
        }
        input[strcspn(input, "\r\n")] = '\0';

        char word[LINE_MAX_LEN];
        if (sscanf(input, "%255s", word) != 1) continue;     // blank line
        const char* rest = input + (strstr(input, word) - input) + strlen(word);
        while (*rest == ' ') rest++;

        char row; int col;
        char coord[16], orient[16];

        pthread_mutex_lock(&print_lock);
        int fail = 0;
        if (strcmp(word, "exit") == 0 || strcmp(word, "quit") == 0) {
            send_line(sock, "QUIT");
            pthread_mutex_unlock(&print_lock);
            break;
        } else if (strcmp(word, "help") == 0) {
            print_help();
        } else if (strcmp(word, "ai") == 0) {
            fail = send_line(sock, "MODE|AI");
        } else if (strcmp(word, "pvp") == 0) {
            fail = send_line(sock, "MODE|PVP");
        } else if (strcmp(word, "random") == 0) {
            fail = send_line(sock, "PLACE|RANDOM");
        } else if (strcmp(word, "place") == 0) {
            if (sscanf(rest, "%15s %15s", coord, orient) == 2
                && parse_coordinates(coord, &row, &col)
                && (orient[0] == 'h' || orient[0] == 'H' || orient[0] == 'v' || orient[0] == 'V')
                && orient[1] == '\0')
                fail = send_line(sock, "PLACE|%c%d|%c", row, col, orient[0] & ~0x20);
            else
                printf("Usage: place B3 h   (h = right, v = down)\n");
        } else if (strcmp(word, "chat") == 0) {
            fail = send_line(sock, "CHAT|%s", rest);
        } else if (parse_coordinates(word, &row, &col)) {
            fail = send_line(sock, "SHOOT|%c%d", row, col);
        } else {
            printf("Unknown input. Type 'help' for commands.\n");
        }
        if (fail) {
            perror("send");
            pthread_mutex_unlock(&print_lock);
            break;
        }
        printf("> ");
        fflush(stdout);
        pthread_mutex_unlock(&print_lock);
    }

    printf("Goodbye!\n");
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return 0;
}
