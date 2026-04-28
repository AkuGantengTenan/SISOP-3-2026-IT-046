#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol.h"

int main(int argc, char *argv[]) {
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    int         port = (argc > 2) ? atoi(argv[2]) : PORT;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Alamat tidak valid: %s\n", host);
        return 1;
    }
    if (connect(sock, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }

    char   buf[BUF_SIZE];
    fd_set read_fds;

    /*
     * Loop utama: select() memantau socket (pesan dari server)
     * dan STDIN (input user) secara bersamaan — tanpa fork/thread.
     */
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int maxfd = sock + 1;
        if (select(maxfd, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        /* Ada data dari server */
        if (FD_ISSET(sock, &read_fds)) {
            int n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                printf("[System] Koneksi ke server terputus.\n");
                break;
            }
            buf[n] = '\0';
            printf("%s", buf);
            fflush(stdout);
        }

        /* Ada input dari user */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(buf, sizeof(buf), stdin) == NULL) break;
            buf[strcspn(buf, "\n")] = '\0';
            send(sock, buf, strlen(buf), 0);
            if (strcmp(buf, "/exit") == 0) break;
        }
    }

    close(sock);
    return 0;
}
