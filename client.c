// client.c - TCP chat client using POSIX sockets and pthreads on Linux

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define USERNAME_LEN 32
#define BUFFER_SIZE 512

static volatile sig_atomic_t running = 1;
static int sockfd = -1;

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
static int send_all(int sock, const char *buf, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(sock, buf + total_sent, len - total_sent, 0);
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
 * Receiver thread
 * ========================== */

static void *recv_thread(void *arg) {
    (void)arg;
    char buffer[BUFFER_SIZE + 1];
    ssize_t n;

    while (running && (n = recv(sockfd, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[n] = '\0';
        fputs(buffer, stdout);
        fflush(stdout);
    }

    if (n <= 0 && running) {
        printf("\nDisconnected from server.\n");
    }
    running = 0;
    return NULL;
}

/* ==========================
 * Signal handling
 * ========================== */

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
    if (sockfd != -1) {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
        sockfd = -1;
    }
}

/* ==========================
 * Main client entry point
 * ========================== */

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port number.\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_sigint);

    /* Ask for username */
    char username[USERNAME_LEN];
    printf("Enter username (max %d chars): ", USERNAME_LEN - 1);
    fflush(stdout);
    if (!fgets(username, sizeof(username), stdin)) {
        fprintf(stderr, "Failed to read username.\n");
        return EXIT_FAILURE;
    }
    trim_newline(username);
    if (username[0] == '\0') {
        fprintf(stderr, "Username cannot be empty.\n");
        return EXIT_FAILURE;
    }

    /* Create socket and connect */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        die("socket");
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", server_ip);
        close(sockfd);
        sockfd = -1;
        return EXIT_FAILURE;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        die("connect");
    }

    /* Send username as first message */
    char uname_msg[USERNAME_LEN + 2];
    snprintf(uname_msg, sizeof(uname_msg), "%s\n", username);
    if (send_all(sockfd, uname_msg, strlen(uname_msg)) < 0) {
        die("send username");
    }

    printf("Connected. Type messages and press Enter to send.\n");
    printf("Type /quit to disconnect.\n");

    /* Start receiver thread */
    pthread_t recv_tid;
    if (pthread_create(&recv_tid, NULL, recv_thread, NULL) != 0) {
        die("pthread_create");
    }

    /* Main send loop */
    char buffer[BUFFER_SIZE];
    while (running && fgets(buffer, sizeof(buffer), stdin) != NULL) {
        if (strcmp(buffer, "/quit\n") == 0 || strcmp(buffer, "/quit\r\n") == 0) {
            running = 0;
            break;
        }
        if (send_all(sockfd, buffer, strlen(buffer)) < 0) {
            printf("Failed to send message. Disconnecting.\n");
            running = 0;
            break;
        }
    }

    /* Clean up */
    running = 0;
    if (sockfd != -1) {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
        sockfd = -1;
    }

    pthread_join(recv_tid, NULL);
    printf("Disconnected.\n");
    return EXIT_SUCCESS;
}

