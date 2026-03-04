// server.c - Multi-client TCP chat server using POSIX sockets and pthreads on Linux

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_CLIENTS 100
#define USERNAME_LEN 32
#define BUFFER_SIZE 512

/* ==========================
 * Data structures and globals
 * ========================== */

typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    char username[USERNAME_LEN];
    pthread_t thread;
    int active;
} Client;

static Client clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t server_running = 1;

/* ==========================
 * Utility helpers
 * ========================== */

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

/* Send the full buffer over the socket */
static int send_all(int sockfd, const char *buf, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(sockfd, buf + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        total_sent += (size_t)sent;
    }
    return 0;
}

/* ==========================
 * Client list management
 * ========================== */

static void add_client(int sockfd, struct sockaddr_in addr, int *out_index) {
    pthread_mutex_lock(&clients_mutex);
    int index = -1;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].active) {
            clients[i].active = 1;
            clients[i].sockfd = sockfd;
            clients[i].addr = addr;
            clients[i].username[0] = '\0';
            index = i;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (index == -1) {
        fprintf(stderr, "Max clients reached, rejecting new connection.\n");
        close(sockfd);
    }
    if (out_index) {
        *out_index = index;
    }
}

static void remove_client(int index) {
    pthread_mutex_lock(&clients_mutex);
    if (index >= 0 && index < MAX_CLIENTS && clients[index].active) {
        clients[index].active = 0;
        close(clients[index].sockfd);
        clients[index].sockfd = -1;
        clients[index].username[0] = '\0';
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Broadcast a message to all clients except the sender socket */
static void broadcast_message(const char *sender_name, const char *message,
                              int exclude_sockfd) {
    char outbuf[BUFFER_SIZE + USERNAME_LEN + 8];
    snprintf(outbuf, sizeof(outbuf), "%s: %s", sender_name, message);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && clients[i].sockfd != exclude_sockfd) {
            if (send_all(clients[i].sockfd, outbuf, strlen(outbuf)) < 0) {
                fprintf(stderr, "Failed to send to client '%s', removing.\n",
                        clients[i].username[0] ? clients[i].username : "(unknown)");
                close(clients[i].sockfd);
                clients[i].active = 0;
                clients[i].sockfd = -1;
                clients[i].username[0] = '\0';
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Broadcast a server-side info message (no sender username) */
static void broadcast_info(const char *info_msg) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active) {
            if (send_all(clients[i].sockfd, info_msg, strlen(info_msg)) < 0) {
                fprintf(stderr, "Failed to send info to client '%s', removing.\n",
                        clients[i].username[0] ? clients[i].username : "(unknown)");
                close(clients[i].sockfd);
                clients[i].active = 0;
                clients[i].sockfd = -1;
                clients[i].username[0] = '\0';
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* ==========================
 * Client handling thread
 * ========================== */

typedef struct {
    int index;
} ClientThreadArgs;

static void *client_thread(void *arg) {
    ClientThreadArgs *cta = (ClientThreadArgs *)arg;
    int index = cta->index;
    free(cta);

    char buffer[BUFFER_SIZE];
    ssize_t n;

    int sockfd;
    pthread_mutex_lock(&clients_mutex);
    sockfd = clients[index].sockfd;
    pthread_mutex_unlock(&clients_mutex);

    /* First message is the username */
    n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        remove_client(index);
        return NULL;
    }
    buffer[n] = '\0';
    trim_newline(buffer);

    char username[USERNAME_LEN];
    pthread_mutex_lock(&clients_mutex);
    strncpy(clients[index].username, buffer, USERNAME_LEN - 1);
    clients[index].username[USERNAME_LEN - 1] = '\0';
    strncpy(username, clients[index].username, USERNAME_LEN);
    username[USERNAME_LEN - 1] = '\0';
    pthread_mutex_unlock(&clients_mutex);

    /* Log connection */
    char addr_str[INET_ADDRSTRLEN];
    pthread_mutex_lock(&clients_mutex);
    inet_ntop(AF_INET, &(clients[index].addr.sin_addr), addr_str, sizeof(addr_str));
    int port = ntohs(clients[index].addr.sin_port);
    pthread_mutex_unlock(&clients_mutex);

    printf("Client connected: %s (%s:%d)\n", username, addr_str, port);

    /* Notify others */
    char join_msg[BUFFER_SIZE];
    snprintf(join_msg, sizeof(join_msg), "*** %s has joined the chat ***\n", username);
    broadcast_info(join_msg);

    /* Main receive/broadcast loop */
    while ((n = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[n] = '\0';
        broadcast_message(username, buffer, sockfd);
    }

    /* Client disconnected */
    printf("Client disconnected: %s (%s:%d)\n", username, addr_str, port);
    snprintf(join_msg, sizeof(join_msg), "*** %s has left the chat ***\n", username);
    broadcast_info(join_msg);

    remove_client(index);
    return NULL;
}

/* ==========================
 * Signal handling
 * ========================== */

static void handle_sigint(int sig) {
    (void)sig;
    server_running = 0;
}

/* ==========================
 * Main server entry point
 * ========================== */

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port number.\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_sigint);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        die("socket");
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        die("setsockopt");
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        die("bind");
    }

    if (listen(listen_fd, 10) < 0) {
        die("listen");
    }

    printf("Server listening on port %d\n", port);
    printf("Press Ctrl+C to stop.\n");

    /* Accept loop */
    while (server_running) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (client_fd < 0) {
            if (errno == EINTR && !server_running) {
                break;
            }
            perror("accept");
            continue;
        }

        int index = -1;
        add_client(client_fd, cli_addr, &index);
        if (index == -1) {
            continue;
        }

        ClientThreadArgs *cta = (ClientThreadArgs *)malloc(sizeof(*cta));
        if (!cta) {
            fprintf(stderr, "Failed to allocate memory for client args.\n");
            remove_client(index);
            continue;
        }
        cta->index = index;

        if (pthread_create(&clients[index].thread, NULL, client_thread, cta) != 0) {
            perror("pthread_create");
            remove_client(index);
            free(cta);
            continue;
        }
        pthread_detach(clients[index].thread);
    }

    printf("Shutting down server...\n");
    close(listen_fd);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active) {
            close(clients[i].sockfd);
            clients[i].active = 0;
            clients[i].sockfd = -1;
            clients[i].username[0] = '\0';
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    return EXIT_SUCCESS;
}

