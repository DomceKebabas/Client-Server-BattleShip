///
/// @author Dominykas Nemanis on 3/5/26.
///
/// Battleship server. One thread per client. A client picks a mode (AI or PVP),
/// places its fleet, then shoots in turns. See README.md for the protocol.
///

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#include "battleship_logic.h"

#define PROTOCOL_VERSION 2
#define DEFAULT_PORT 8080
#define LINE_MAX_LEN 256            // longest accepted command line (without the newline)
#define MAX_CLIENTS 64
#define IDLE_TIMEOUT_SEC 600        // drop clients that send nothing for this long
#define OUT_SIZE 1024

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0              // SIGPIPE is ignored in main() as well
#endif

typedef struct Match Match;

typedef struct Session {
    int fd;
    char name[64];                  // "ip:port", for logging
    pthread_mutex_t send_lock;      // one whole message at a time per socket
    Match* match;                   // guarded by g_lobby_lock (only written by others while pairing)
    int idx;                        // seat in the match: 0 or 1
} Session;

enum { PHASE_SETUP, PHASE_PLAYING, PHASE_OVER };

struct Match {
    pthread_mutex_t lock;           // guards everything below, and serialises sends to the players
    int refs;                       // one per human seat still attached
    int pvp;
    Session* p[2];                  // p[1] is NULL in an AI match; NULL once a player left
    Board board[2];                 // board[i] belongs to seat i; seat 1 is the AI in AI mode
    int ready[2];
    int phase;
    int turn;                       // seat that shoots next
    Ai ai;
    Rng rng;
};

static pthread_mutex_t g_lobby_lock = PTHREAD_MUTEX_INITIALIZER;
static Session* g_pending = NULL;   // PVP client waiting for an opponent (guarded by g_lobby_lock)
static atomic_int g_active_clients = 0;
static atomic_uint g_counter = 0;
static volatile sig_atomic_t g_stop = 0;

// ---------------------------------------------------------------- helpers

static void logf_(const Session* s, const char* fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    printf("[%s] %s\n", s ? s->name : "server", msg);
}

typedef struct { char buf[OUT_SIZE]; size_t len; } Out;

static void out_add(Out* o, const char* fmt, ...) {
    if (o->len >= sizeof(o->buf)) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(o->buf + o->len, sizeof(o->buf) - o->len, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    o->len += (size_t)n;
    if (o->len >= sizeof(o->buf)) o->len = sizeof(o->buf) - 1;   // truncated
}

static int send_all(int sock, const char* msg, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, msg + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int session_send(Session* s, const char* msg) {
    pthread_mutex_lock(&s->send_lock);
    int rc = send_all(s->fd, msg, strlen(msg));
    pthread_mutex_unlock(&s->send_lock);
    return rc;
}

static int session_send_out(Session* s, const Out* o) {
    pthread_mutex_lock(&s->send_lock);
    int rc = send_all(s->fd, o->buf, o->len);
    pthread_mutex_unlock(&s->send_lock);
    return rc;
}

// Sends to a seat if a player is still attached. Caller holds m->lock.
static void seat_send(Match* m, int seat, const char* msg) {
    if (m->p[seat]) session_send(m->p[seat], msg);
}

static void seat_send_out(Match* m, int seat, const Out* o) {
    if (m->p[seat]) session_send_out(m->p[seat], o);
}

static void add_board(Out* o, const char* key, const Board* b, int reveal) {
    char cells[BOARD_STRING_SIZE];
    board_render(b, reveal, cells, sizeof(cells));
    out_add(o, "%s|%s\n", key, cells);
}

// ------------------------------------------------------------ match logic
// All functions below expect m->lock to be held.

static void send_views(Match* m, int seat) {
    Out o = {0};
    add_board(&o, "YOURBOARD", &m->board[seat], 1);
    add_board(&o, "TRACKING", &m->board[1 - seat], 0);
    seat_send_out(m, seat, &o);
}

static void begin_play(Match* m) {
    m->phase = PHASE_PLAYING;
    m->turn = m->pvp ? (int)(rng_next(&m->rng) % 2) : 0;       // the human always opens vs the AI
    for (int i = 0; i < 2; i++) {
        send_views(m, i);
        seat_send(m, i, m->turn == i ? "STATUS|YOUR_TURN\n" : "STATUS|OPPONENT_TURN\n");
    }
}

static void finish(Match* m, int winner) {
    m->phase = PHASE_OVER;
    int loser = 1 - winner;

    Out w = {0};
    out_add(&w, "SHOTS|%d\nSTATUS|WIN\n", m->board[loser].shots);
    add_board(&w, "REVEAL", &m->board[loser], 1);
    out_add(&w, "STATUS|CHOOSE_MODE\n");
    seat_send_out(m, winner, &w);

    Out l = {0};
    out_add(&l, "STATUS|LOSE\n");
    add_board(&l, "REVEAL", &m->board[winner], 1);
    out_add(&l, "STATUS|CHOOSE_MODE\n");
    seat_send_out(m, loser, &l);
}

// Reports a shot at `target`'s board to the seat that was shot at.
static void notify_target(Match* m, int target, char row, int col, ShotResult res, int sunk) {
    Out o = {0};
    out_add(&o, "OPPONENT_SHOT|%c%d|%s\n", row, col, shot_result_name(res));
    if (sunk >= 0) out_add(&o, "OPPONENT_SUNK|%s\n", FLEET_NAMES[sunk]);
    add_board(&o, "YOURBOARD", &m->board[target], 1);
    seat_send_out(m, target, &o);
}

static void do_shoot(Match* m, int seat, char row, int col) {
    int target = 1 - seat;
    int sunk;
    ShotResult res = shoot(&m->board[target], row, col, &sunk);

    if (res == SHOT_ALREADY_SHOT) {             // not counted, the turn is kept
        seat_send(m, seat, "RESULT|ALREADY_SHOT\nSTATUS|YOUR_TURN\n");
        return;
    }

    Out o = {0};
    out_add(&o, "RESULT|%s\n", shot_result_name(res));
    if (sunk >= 0) out_add(&o, "SUNK_SHIP|%s\n", FLEET_NAMES[sunk]);
    add_board(&o, "TRACKING", &m->board[target], 0);
    seat_send_out(m, seat, &o);

    if (m->pvp) notify_target(m, target, row, col, res, sunk);

    if (res == SHOT_SUNK_ALL) {
        finish(m, seat);
        return;
    }

    if (m->pvp) {
        m->turn = target;
        seat_send(m, seat, "STATUS|OPPONENT_TURN\n");
        seat_send(m, target, "STATUS|YOUR_TURN\n");
        return;
    }

    // Computer opponent answers immediately.
    char arow; int acol; int asunk;
    ai_next_shot(&m->ai, &arow, &acol);
    ShotResult ares = shoot(&m->board[seat], arow, acol, &asunk);
    ai_report(&m->ai, arow, acol, ares);
    notify_target(m, seat, arow, acol, ares, asunk);

    if (ares == SHOT_SUNK_ALL) {
        finish(m, 1);
        return;
    }
    seat_send(m, seat, "STATUS|YOUR_TURN\n");
}

static void do_place(Match* m, int seat, char* arg) {
    if (m->phase != PHASE_SETUP || m->ready[seat]) {
        seat_send(m, seat, "ERROR|NOT_IN_SETUP\n");
        return;
    }

    Board* b = &m->board[seat];
    Out o = {0};

    if (strcmp(arg, "RANDOM") == 0) {
        board_place_random(b, &m->rng);
    } else {
        char* orient = strchr(arg, '|');
        char row; int col;
        if (orient) *orient++ = '\0';
        int vertical = -1;
        if (orient && orient[1] == '\0') {
            if (orient[0] == 'H' || orient[0] == 'h') vertical = 0;
            if (orient[0] == 'V' || orient[0] == 'v') vertical = 1;
        }
        int ship = b->ships_placed;
        if (vertical < 0 || !parse_coordinates(arg, &row, &col)
            || !board_place_ship(b, row - 'A', col - 1, vertical)) {
            seat_send(m, seat, "ERROR|INVALID_PLACEMENT\n");
            return;
        }
        out_add(&o, "PLACED|%s\n", FLEET_NAMES[ship]);
    }

    add_board(&o, "YOURBOARD", b, 1);

    if (!board_ready(b)) {
        out_add(&o, "NEXT_SHIP|%s|%d\n", FLEET_NAMES[b->ships_placed], FLEET_SIZES[b->ships_placed]);
        seat_send_out(m, seat, &o);
        return;
    }

    m->ready[seat] = 1;
    if (!m->pvp) {
        board_place_random(&m->board[1], &m->rng);
        m->ready[1] = 1;
    }
    if (m->ready[0] && m->ready[1]) {
        seat_send_out(m, seat, &o);
        begin_play(m);
    } else {
        out_add(&o, "STATUS|WAITING_FOR_OPPONENT_SETUP\n");
        seat_send_out(m, seat, &o);
    }
}

static void start_setup(Match* m) {
    for (int i = 0; i < (m->pvp ? 2 : 1); i++) {
        Out o = {0};
        out_add(&o, "MATCH|%s\nFLEET|", m->pvp ? "PVP" : "AI");
        for (int s = 0; s < NUM_SHIPS; s++)
            out_add(&o, "%s%d", s ? "," : "", FLEET_SIZES[s]);
        out_add(&o, "\nSTATUS|PLACE_SHIPS\n");
        add_board(&o, "YOURBOARD", &m->board[i], 1);
        out_add(&o, "NEXT_SHIP|%s|%d\n", FLEET_NAMES[0], FLEET_SIZES[0]);
        seat_send_out(m, i, &o);
    }
}

static Match* match_create(int pvp, unsigned int seed) {
    Match* m = calloc(1, sizeof(Match));
    if (!m) return NULL;
    pthread_mutex_init(&m->lock, NULL);
    m->pvp = pvp;
    m->refs = pvp ? 2 : 1;
    board_init(&m->board[0]);
    board_init(&m->board[1]);
    rng_seed(&m->rng, seed);
    ai_init(&m->ai, seed ^ 0xA5A5A5A5u);
    return m;
}

// Detaches a session from its match (if any). If the game was still running,
// the opponent wins by forfeit.
static void leave_match(Session* s) {
    pthread_mutex_lock(&g_lobby_lock);
    Match* m = s->match;
    int idx = s->idx;
    s->match = NULL;
    if (g_pending == s) g_pending = NULL;
    pthread_mutex_unlock(&g_lobby_lock);

    if (!m) return;

    pthread_mutex_lock(&m->lock);
    m->p[idx] = NULL;
    Session* other = m->p[1 - idx];
    if (m->pvp && other && m->phase != PHASE_OVER) {
        m->phase = PHASE_OVER;
        session_send(other, "STATUS|OPPONENT_LEFT\nSTATUS|WIN\nSTATUS|CHOOSE_MODE\n");
    }
    int last = --m->refs == 0;
    pthread_mutex_unlock(&m->lock);

    if (last) {
        pthread_mutex_destroy(&m->lock);
        free(m);
    }
}

// ------------------------------------------------------------- commands

static unsigned int make_seed(const Session* s) {
    return (unsigned int)time(NULL)
         ^ ((unsigned int)s->fd << 16)
         ^ (atomic_fetch_add(&g_counter, 1) * 2654435761u);
}

static Match* current_match(Session* s, int* idx) {
    pthread_mutex_lock(&g_lobby_lock);
    Match* m = s->match;
    *idx = s->idx;
    pthread_mutex_unlock(&g_lobby_lock);
    return m;
}

static int match_is_over(Match* m) {
    pthread_mutex_lock(&m->lock);
    int over = m->phase == PHASE_OVER;
    pthread_mutex_unlock(&m->lock);
    return over;
}

static void cmd_mode(Session* s, const char* arg) {
    int pvp;
    if (strcmp(arg, "AI") == 0) pvp = 0;
    else if (strcmp(arg, "PVP") == 0) pvp = 1;
    else { session_send(s, "ERROR|INVALID_MODE\n"); return; }

    pthread_mutex_lock(&g_lobby_lock);
    if (s->match) {
        pthread_mutex_unlock(&g_lobby_lock);
        session_send(s, "ERROR|IN_GAME\n");
        return;
    }

    if (g_pending == s) {
        if (pvp) {
            pthread_mutex_unlock(&g_lobby_lock);
            session_send(s, "ERROR|ALREADY_WAITING\n");
            return;
        }
        g_pending = NULL;               // switching from waiting to a solo game
    }

    Session* waiting = (pvp && g_pending) ? g_pending : NULL;

    if (pvp && !waiting) {
        g_pending = s;
        pthread_mutex_unlock(&g_lobby_lock);
        logf_(s, "waiting for an opponent");
        session_send(s, "STATUS|WAITING_FOR_OPPONENT\n");
        return;
    }

    Match* m = match_create(pvp, make_seed(s));
    if (!m) {
        pthread_mutex_unlock(&g_lobby_lock);
        session_send(s, "ERROR|SERVER_ERROR\n");
        return;
    }
    if (waiting) {
        g_pending = NULL;
        m->p[0] = waiting; waiting->match = m; waiting->idx = 0;
        m->p[1] = s;       s->match = m;       s->idx = 1;
    } else {
        m->p[0] = s;       s->match = m;       s->idx = 0;
    }
    pthread_mutex_unlock(&g_lobby_lock);

    logf_(s, "%s match started", pvp ? "PVP" : "AI");
    pthread_mutex_lock(&m->lock);
    start_setup(m);
    pthread_mutex_unlock(&m->lock);
}

// Handles one complete command line. Returns 1 to keep the connection, 0 to close it.
static int handle_line(Session* s, char* line) {
    char* arg = strchr(line, '|');
    if (arg) *arg++ = '\0'; else arg = (char*)"";

    if (strcmp(line, "QUIT") == 0)
        return 0;

    int idx;
    Match* m = current_match(s, &idx);
    if (m && match_is_over(m)) {        // finished game: back to the lobby
        leave_match(s);
        m = NULL;
    }

    if (strcmp(line, "MODE") == 0) {
        cmd_mode(s, arg);
    } else if (strcmp(line, "PLACE") == 0) {
        if (!m) { session_send(s, "ERROR|NO_GAME\n"); return 1; }
        pthread_mutex_lock(&m->lock);
        do_place(m, idx, arg);
        pthread_mutex_unlock(&m->lock);
    } else if (strcmp(line, "SHOOT") == 0) {
        if (!m) { session_send(s, "ERROR|NO_GAME\n"); return 1; }
        char row; int col;
        pthread_mutex_lock(&m->lock);
        if (m->phase != PHASE_PLAYING)
            seat_send(m, idx, "ERROR|NOT_PLAYING\n");
        else if (m->turn != idx)
            seat_send(m, idx, "ERROR|NOT_YOUR_TURN\n");
        else if (!parse_coordinates(arg, &row, &col))
            seat_send(m, idx, "ERROR|INVALID_COORDINATES\n");
        else
            do_shoot(m, idx, row, col);
        pthread_mutex_unlock(&m->lock);
    } else if (strcmp(line, "CHAT") == 0) {
        if (!m || !m->pvp) { session_send(s, "ERROR|NO_OPPONENT\n"); return 1; }
        Out o = {0};
        out_add(&o, "CHAT|%s\n", arg);
        pthread_mutex_lock(&m->lock);
        seat_send_out(m, 1 - idx, &o);
        pthread_mutex_unlock(&m->lock);
    } else {
        session_send(s, "ERROR|UNKNOWN_COMMAND\n");
    }
    return 1;
}

static void* handle_client(void* arg) {
    Session* s = arg;

    struct timeval tv = { IDLE_TIMEOUT_SEC, 0 };
    setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    logf_(s, "connected");
    char hello[64];
    snprintf(hello, sizeof(hello), "HELLO|BATTLESHIP|%d\nSTATUS|CHOOSE_MODE\n", PROTOCOL_VERSION);
    int running = session_send(s, hello) == 0;

    // TCP is a byte stream: collect bytes and only act on complete lines.
    char line[LINE_MAX_LEN + 1];
    size_t line_len = 0;
    int discarding = 0;         // inside an over-long line, skip until its newline
    char chunk[512];

    while (running && !g_stop) {
        ssize_t n = recv(s->fd, chunk, sizeof(chunk), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            logf_(s, "idle timeout");
            session_send(s, "ERROR|IDLE_TIMEOUT\n");
            break;
        }
        if (n <= 0) break;

        for (ssize_t i = 0; i < n && running; i++) {
            char c = chunk[i];

            if (c != '\n') {
                if (discarding) continue;
                if (line_len < LINE_MAX_LEN) {
                    line[line_len++] = c;
                } else {
                    discarding = 1;
                    running = session_send(s, "ERROR|LINE_TOO_LONG\n") == 0;
                }
                continue;
            }

            if (discarding) {               // end of the over-long line
                discarding = 0;
                line_len = 0;
                continue;
            }

            if (line_len > 0 && line[line_len - 1] == '\r') line_len--;     // accept CRLF
            line[line_len] = '\0';
            line_len = 0;

            if (line[0] == '\0') continue;  // ignore blank lines
            running = handle_line(s, line);
        }
    }

    logf_(s, "disconnected");
    leave_match(s);
    close(s->fd);
    pthread_mutex_destroy(&s->send_lock);
    free(s);
    atomic_fetch_sub(&g_active_clients, 1);
    return NULL;
}

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

int main(int argc, char** argv) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port < 1 || port > 65535) {
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            return 1;
        }
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);   // a vanished client must not kill the whole server

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;  // no SA_RESTART: accept() returns EINTR so we can stop
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0); // AF_INET = IPv4, SOCK_STREAM = TCP
    if (server_socket < 0) { perror("socket"); exit(1); }

    int opt = 1;    // lets the server rebind the port right after a restart
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(server_socket, 16) < 0) {
        perror("listen"); exit(1);
    }

    logf_(NULL, "Battleship server (protocol %d) listening on port %d...", PROTOCOL_VERSION, port);

    while (!g_stop) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int fd = accept(server_socket, (struct sockaddr*)&peer, &peer_len);
        if (fd < 0) {
            if (errno != EINTR) { perror("accept"); usleep(100000); }   // avoid a busy loop
            continue;
        }

        if (atomic_load(&g_active_clients) >= MAX_CLIENTS) {
            const char* msg = "ERROR|SERVER_FULL\n";
            send_all(fd, msg, strlen(msg));
            close(fd);
            continue;
        }

        Session* s = calloc(1, sizeof(Session));
        if (!s) { close(fd); continue; }
        s->fd = fd;
        pthread_mutex_init(&s->send_lock, NULL);
        char ip[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        snprintf(s->name, sizeof(s->name), "%s:%d", ip, ntohs(peer.sin_port));

        atomic_fetch_add(&g_active_clients, 1);
        pthread_t tid;
        int err = pthread_create(&tid, NULL, handle_client, s);
        if (err != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(err));
            atomic_fetch_sub(&g_active_clients, 1);
            close(fd);
            pthread_mutex_destroy(&s->send_lock);
            free(s);
            continue;
        }
        pthread_detach(tid);
    }

    logf_(NULL, "shutting down");
    close(server_socket);
    return 0;
}
