#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol.h"

/* ------------------------------------------------------------------ */
/* Struktur data client                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    int    fd;
    char   name[NAME_SIZE];
    int    is_admin;
    int    active;
} Client;

/* State registrasi sementara untuk admin (menunggu password) */
typedef struct {
    int fd;
    int waiting_password; /* 1 = sudah kirim nama ADMIN_NAME, tunggu pw */
} RegState;

static Client        clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static int           server_fd;
static time_t        start_time;
static FILE         *log_file;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/* Prototypes                                                           */
/* ------------------------------------------------------------------ */
void  log_entry(const char *level, const char *msg);
void  broadcast(const char *msg, int exclude_fd);
void  disconnect_client(int idx);
void  handle_admin_rpc(int idx, const char *cmd);
void *client_thread(void *arg);
void  shutdown_server(int sig);

/* ------------------------------------------------------------------ */
/* Logging                                                              */
/* ------------------------------------------------------------------ */
void log_entry(const char *level, const char *msg) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);

    pthread_mutex_lock(&log_mutex);
    fprintf(log_file, "[%s] [%s] [%s]\n", ts, level, msg);
    fflush(log_file);
    pthread_mutex_unlock(&log_mutex);
}

/* ------------------------------------------------------------------ */
/* Broadcast ke semua client biasa (bukan admin), kecuali exclude_fd   */
/* ------------------------------------------------------------------ */
void broadcast(const char *msg, int exclude_fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && !clients[i].is_admin &&
            clients[i].fd != exclude_fd) {
            send(clients[i].fd, msg, strlen(msg), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* ------------------------------------------------------------------ */
/* Disconnect satu client (panggil dengan mutex sudah di-lock)         */
/* ------------------------------------------------------------------ */
void disconnect_client(int idx) {
    if (!clients[idx].active) return;

    char log_msg[BUF_SIZE];
    if (clients[idx].name[0] != '\0') {
        snprintf(log_msg, sizeof(log_msg),
                 "User '%s' disconnected", clients[idx].name);
        log_entry("System", log_msg);
    }
    close(clients[idx].fd);
    clients[idx].fd       = -1;
    clients[idx].active   = 0;
    clients[idx].is_admin = 0;
    clients[idx].name[0]  = '\0';
}

/* ------------------------------------------------------------------ */
/* RPC admin                                                            */
/* ------------------------------------------------------------------ */
void handle_admin_rpc(int idx, const char *cmd) {
    int  fd = clients[idx].fd;
    char out[OUT_SIZE];

    if (strcmp(cmd, "1") == 0) {
        /* RPC_GET_USERS */
        log_entry("Admin", "RPC_GET_USERS");
        char list[OUT_SIZE] = "Active users:\n";
        int  count = 0;

        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && !clients[i].is_admin) {
                count++;
                strncat(list, " - ", sizeof(list) - strlen(list) - 1);
                strncat(list, clients[i].name, sizeof(list) - strlen(list) - 1);
                strncat(list, "\n", sizeof(list) - strlen(list) - 1);
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        snprintf(out, sizeof(out), "%sTotal: %d\nCommand >> ", list, count);
        send(fd, out, strlen(out), 0);

    } else if (strcmp(cmd, "2") == 0) {
        /* RPC_GET_UPTIME */
        log_entry("Admin", "RPC_GET_UPTIME");
        double uptime = difftime(time(NULL), start_time);
        snprintf(out, sizeof(out),
                 "Server uptime: %.0f seconds\nCommand >> ", uptime);
        send(fd, out, strlen(out), 0);

    } else if (strcmp(cmd, "3") == 0) {
        /* RPC_SHUTDOWN */
        log_entry("Admin", "RPC_SHUTDOWN");
        log_entry("System", "EMERGENCY SHUTDOWN INITIATED");

        const char *notice = "[System] Server is shutting down.\n";
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                send(clients[i].fd, notice, strlen(notice), 0);
                close(clients[i].fd);
                clients[i].active = 0;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        fclose(log_file);
        close(server_fd);
        exit(0);

    } else if (strcmp(cmd, "4") == 0) {
        /* Disconnect admin */
        const char *bye = "[System] Disconnecting from The Wired...\n";
        send(fd, bye, strlen(bye), 0);
        pthread_mutex_lock(&clients_mutex);
        disconnect_client(idx);
        pthread_mutex_unlock(&clients_mutex);

    } else {
        snprintf(out, sizeof(out), "[System] Unknown command.\nCommand >> ");
        send(fd, out, strlen(out), 0);
    }
}

/* ------------------------------------------------------------------ */
/* Thread per client — seluruh siklus hidup client ditangani di sini   */
/* ------------------------------------------------------------------ */
void *client_thread(void *arg) {
    int fd  = *(int *)arg;
    free(arg);

    /* Cari slot untuk client ini */
    int slot = -1;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].fd       = fd;
            clients[i].active   = 1;
            clients[i].is_admin = 0;
            clients[i].name[0]  = '\0';
            slot = i;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (slot == -1) {
        const char *full = "[System] Server penuh.\n";
        send(fd, full, strlen(full), 0);
        close(fd);
        return NULL;
    }

    /* Minta nama */
    const char *prompt = "Enter your name: ";
    send(fd, prompt, strlen(prompt), 0);

    char buf[BUF_SIZE];
    int  waiting_password = 0; /* flag: sudah masuk nama admin, tunggu pw */

    /* Loop baca pesan dari client ini */
    while (1) {
        int n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            /* Koneksi terputus tiba-tiba */
            pthread_mutex_lock(&clients_mutex);
            disconnect_client(slot);
            pthread_mutex_unlock(&clients_mutex);
            break;
        }
        buf[n] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';

        /* ---- Belum punya nama: fase registrasi ---- */
        if (clients[slot].name[0] == '\0') {
            /* Cek duplikat nama */
            int duplicate = 0;
            pthread_mutex_lock(&clients_mutex);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (i == slot) continue;
                if (clients[i].active && strcmp(clients[i].name, buf) == 0) {
                    duplicate = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_mutex);

            if (duplicate) {
                const char *err =
                    "[System] The identity is already synchronized in The Wired.\n"
                    "Enter your name: ";
                send(fd, err, strlen(err), 0);
                continue;
            }

            /* Simpan nama */
            pthread_mutex_lock(&clients_mutex);
            strncpy(clients[slot].name, buf, NAME_SIZE - 1);
            pthread_mutex_unlock(&clients_mutex);

            /* Deteksi admin */
            if (strcmp(buf, ADMIN_NAME) == 0) {
                waiting_password = 1;
                const char *pw_prompt = "Enter Password: ";
                send(fd, pw_prompt, strlen(pw_prompt), 0);
                continue;
            }

            /* Client biasa: sambut */
            char welcome[BUF_SIZE];
            snprintf(welcome, sizeof(welcome),
                     "--- Welcome to The Wired, %s ---\n> ", buf);
            send(fd, welcome, strlen(welcome), 0);

            char log_msg[BUF_SIZE];
            snprintf(log_msg, sizeof(log_msg), "User '%s' connected", buf);
            log_entry("System", log_msg);
            continue;
        }

        /* ---- Sudah punya nama tapi menunggu password admin ---- */
        if (waiting_password) {
            if (strcmp(buf, ADMIN_PASS) == 0) {
                pthread_mutex_lock(&clients_mutex);
                clients[slot].is_admin = 1;
                pthread_mutex_unlock(&clients_mutex);

                waiting_password = 0;
                const char *auth_ok =
                    "[System] Authentication Successful. Granted Admin privileges.\n\n"
                    "=== THE KNIGHTS CONSOLE ===\n"
                    "1. Check Active Entities (Users)\n"
                    "2. Check Server Uptime\n"
                    "3. Execute Emergency Shutdown\n"
                    "4. Disconnect\n"
                    "Command >> ";
                send(fd, auth_ok, strlen(auth_ok), 0);

                char log_msg[BUF_SIZE];
                snprintf(log_msg, sizeof(log_msg),
                         "User '%s' connected", ADMIN_NAME);
                log_entry("System", log_msg);
            } else {
                const char *fail =
                    "[System] Authentication Failed.\nEnter Password: ";
                send(fd, fail, strlen(fail), 0);
            }
            continue;
        }

        /* ---- Admin: tangani RPC ---- */
        if (clients[slot].is_admin) {
            handle_admin_rpc(slot, buf);
            /* Jika disconnect_client dipanggil di dalam, keluar loop */
            if (!clients[slot].active) break;
            continue;
        }

        /* ---- Client biasa: /exit ---- */
        if (strcmp(buf, "/exit") == 0) {
            const char *bye = "[System] Disconnecting from The Wired...\n";
            send(fd, bye, strlen(bye), 0);
            pthread_mutex_lock(&clients_mutex);
            disconnect_client(slot);
            pthread_mutex_unlock(&clients_mutex);
            break;
        }

        /* ---- Client biasa: pesan chat → broadcast ---- */
        char out[OUT_SIZE];
        snprintf(out, sizeof(out), "[%s]: %s\n", clients[slot].name, buf);
        broadcast(out, fd);

        /* Prompt kembali untuk pengirim */
        send(fd, "> ", 2, 0);

        /* Log */
        char log_msg[BUF_SIZE];
        snprintf(log_msg, sizeof(log_msg), "[%s]: %s", clients[slot].name, buf);
        log_entry("User", log_msg);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* SIGINT handler                                                       */
/* ------------------------------------------------------------------ */
void shutdown_server(int sig) {
    (void)sig;
    printf("\n[The Wired] Menerima SIGINT, mematikan server...\n");

    const char *notice = "[System] Server is shutting down.\n";
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            send(clients[i].fd, notice, strlen(notice), 0);
            close(clients[i].fd);
            clients[i].active = 0;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    log_entry("System", "SERVER OFFLINE");
    fclose(log_file);
    close(server_fd);
    exit(0);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */
int main(void) {
    start_time = time(NULL);

    log_file = fopen("history.log", "a");
    if (!log_file) { perror("fopen history.log"); return 1; }

    signal(SIGINT, shutdown_server);

    /* Buat socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(server_fd, 10) < 0) { perror("listen"); return 1; }

    memset(clients, 0, sizeof(clients));
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;

    log_entry("System", "SERVER ONLINE");
    printf("[The Wired] Server berjalan di port %d\n", PORT);

    /* Loop accept — setiap client mendapat thread sendiri */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int new_fd = accept(server_fd,
                            (struct sockaddr *)&client_addr, &addr_len);
        if (new_fd < 0) { perror("accept"); continue; }

        /* Kirim fd ke thread baru lewat heap (hindari race condition) */
        int *fd_ptr = malloc(sizeof(int));
        if (!fd_ptr) { close(new_fd); continue; }
        *fd_ptr = new_fd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, fd_ptr) != 0) {
            perror("pthread_create");
            free(fd_ptr);
            close(new_fd);
            continue;
        }
        /* Detach agar resource langsung dibebaskan saat thread selesai */
        pthread_detach(tid);
    }

    return 0;
}
