# SISOP-3-2026

<details>
<summary>Soal 1</summary>

**Penjelasan**

Pertama buat file bernama `protocol.h`

```
#ifndef PROTOCOL_H
#define PROTOCOL_H

#define PORT        4242
#define MAX_CLIENTS 64
#define NAME_SIZE   64
#define BUF_SIZE    4096
#define OUT_SIZE    (BUF_SIZE + NAME_SIZE + 64)
#define ADMIN_NAME  "The Knights"
#define ADMIN_PASS  "protocol7"

#endif
```

Include guard di baris pertama dipakai supaya header tidak didefinisikan ganda. PORT 4242 adalah nomor port TCP yang harus sama antara server dan client. MAX_CLIENTS 64 jadi batas jumlah koneksi sekaligus. NAME_SIZE (64) dan BUF_SIZE (4096) dipakai untuk buffer nama dan pesan, sedangkan OUT_SIZE lebih besar agar gabungan nama + pesan tidak overflow. ADMIN_NAME dan ADMIN_PASS adalah kredensial admin yang di-hardcode untuk verifikasi oleh server.

Buat file `wired.c`

```c
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
```

`log_entry()` dipakai untuk mencatat kejadian penting ke history.log dengan format waktu `[YYYY-MM-DD HH:MM:SS] [Level] [Pesan]`, menggunakan `time()`, `localtime()`, dan `strftime()`. Setelah menulis, `fflush()` memastikan data langsung tersimpan ke disk, dan seluruh proses dibungkus mutex agar aman dipakai banyak thread. Sedangkan `broadcast()` meneruskan pesan ke semua client kecuali pengirim (exclude_fd) dan admin, dengan mutex menjaga konsistensi array clients selama iterasi.

```c
void handle_admin_rpc(int idx, const char *cmd) {
    int  fd = clients[idx].fd;
    char out[OUT_SIZE];

    if (strcmp(cmd, "1") == 0) {
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
        log_entry("Admin", "RPC_GET_UPTIME");
        double uptime = difftime(time(NULL), start_time);
        snprintf(out, sizeof(out), "Server uptime: %.0f seconds\nCommand >> ", uptime);
        send(fd, out, strlen(out), 0);

    } else if (strcmp(cmd, "3") == 0) {
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
```

`handle_admin_rpc()` menjalankan perintah RPC dari admin berdasarkan input angka 1–4. Menu 1 mengirim daftar nama semua client aktif (non-admin) beserta jumlahnya. Menu 2 menghitung uptime dengan `difftime()` dari waktu mulai server. Menu 3 mematikan server: memberi tahu semua client, menutup koneksi, log, socket, lalu `exit(0)`. Menu 4 hanya memutus koneksi admin, sementara server tetap melayani client lain.

```c
void *client_thread(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    int slot = -1;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].fd     = fd;
            clients[i].active = 1;
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

    send(fd, "Enter your name: ", 17, 0);
```

Setiap client yang masuk dibuatkan satu thread baru yang menjalankan fungsi ini.

```c
// KONDISI 1: belum punya nama → registrasi
        if (clients[slot].name[0] == '\0') {
            int duplicate = 0;
            pthread_mutex_lock(&clients_mutex);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (i == slot) continue;
                if (clients[i].active && strcmp(clients[i].name, buf) == 0)
                    { duplicate = 1; break; }
            }
            pthread_mutex_unlock(&clients_mutex);

            if (duplicate) {
                send(fd, "[System] The identity is already synchronized in The Wired.\nEnter your name: ", 78, 0);
                continue;
            }

            strncpy(clients[slot].name, buf, NAME_SIZE - 1);

            if (strcmp(buf, ADMIN_NAME) == 0) {
                waiting_password = 1;
                send(fd, "Enter Password: ", 16, 0);
                continue;
            }

            char welcome[BUF_SIZE];
            snprintf(welcome, sizeof(welcome), "--- Welcome to The Wired, %s ---\n> ", buf);
            send(fd, welcome, strlen(welcome), 0);
            char log_msg[BUF_SIZE];
            snprintf(log_msg, sizeof(log_msg), "User '%s' connected", buf);
            log_entry("System", log_msg);
            continue;
        }
// KONDISI 2: menunggu password admin
        if (waiting_password) {
            if (strcmp(buf, ADMIN_PASS) == 0) {
                pthread_mutex_lock(&clients_mutex);
                clients[slot].is_admin = 1;
                pthread_mutex_unlock(&clients_mutex);
                waiting_password = 0;
                send(fd, "[System] Authentication Successful...\n\n=== THE KNIGHTS CONSOLE ===\n1. Check Active Entities (Users)\n2. Check Server Uptime\n3. Execute Emergency Shutdown\n4. Disconnect\nCommand >> ", 170, 0);
                log_entry("System", "User 'The Knights' connected");
            } else {
                send(fd, "[System] Authentication Failed.\nEnter Password: ", 48, 0);
            }
            continue;
        }

        // KONDISI 3: admin mengirim perintah RPC
        if (clients[slot].is_admin) {
            handle_admin_rpc(slot, buf);
            if (!clients[slot].active) break;
            continue;
        }

        // KONDISI 4: client biasa mengetik /exit
        if (strcmp(buf, "/exit") == 0) {
            send(fd, "[System] Disconnecting from The Wired...\n", 41, 0);
            pthread_mutex_lock(&clients_mutex);
            disconnect_client(slot);
            pthread_mutex_unlock(&clients_mutex);
            break;
        }

        // KONDISI 5: pesan chat biasa → broadcast
        char out[OUT_SIZE];
        snprintf(out, sizeof(out), "[%s]: %s\n", clients[slot].name, buf);
        broadcast(out, fd);
        send(fd, "> ", 2, 0);
        char log_msg[BUF_SIZE];
        snprintf(log_msg, sizeof(log_msg), "[%s]: %s", clients[slot].name, buf);
        log_entry("User", log_msg);
    }
    return NULL;
}

```

Kondisi 2 menangani verifikasi password admin — kalau cocok dengan ADMIN_PASS, flag is_admin diset dan console admin ditampilkan, kalau salah client diminta input ulang tanpa ada batasan percobaan. Kondisi 3 meneruskan semua input admin ke handle_admin_rpc(), lalu mengecek apakah setelah RPC dieksekusi client masih aktif (misalnya admin memilih menu 4 untuk disconnect). Kondisi 4 menangani perintah /exit dari user biasa — kirim pesan perpisahan, cleanup, keluar loop. Kondisi 5 adalah jalur paling sering dilalui — pesan chat diformat menjadi [nama]: pesan, di-broadcast ke semua client lain, prompt > dikembalikan ke pengirim, dan pesan dicatat ke log.

```c
int main(void) {
    start_time = time(NULL);
    log_file = fopen("history.log", "a");
    signal(SIGINT, shutdown_server);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 10);

    memset(clients, 0, sizeof(clients));
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;

    log_entry("System", "SERVER ONLINE");
    printf("[The Wired] Server berjalan di port %d\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int new_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (new_fd < 0) { perror("accept"); continue; }

        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = new_fd;

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, fd_ptr);
        pthread_detach(tid);
    }
    return 0;
}
```

`main()` menginisialisasi server lalu masuk ke loop utama. Pertama mencatat `start_time` dan membuka `history.log` dengan mode append. Socket dibuat dengan `socket()`, lalu `setsockopt(SO_REUSEADDR)` agar bisa bind ulang cepat setelah restart. `bind()` mengikat ke port 4242 di semua interface, dan `listen()` mulai menerima koneksi dengan backlog 10. Setelah itu, loop `while(1)` menunggu client baru lewat `accept()`, menghasilkan fd yang disalin ke heap untuk mencegah race condition. Setiap koneksi dijalankan di thread baru dengan `pthread_create` yang langsung di-`pthread_detach` agar resource dibersihkan otomatis tanpa perlu `pthread_join`.

Buat file `navi.c`

```c
int main(int argc, char *argv[]) {
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    int         port = (argc > 2) ? atoi(argv[2]) : PORT;

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);
    inet_pton(AF_INET, host, &server_addr.sin_addr);

    connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
```

Client menerima host dan port opsional dari argumen command line — jika tidak diisi, default ke 127.0.0.1 (localhost) dan port dari protocol.h. Ini memungkinkan client dijalankan sebagai ./navi untuk konek lokal, atau ./navi 192.168.1.5 4242 untuk konek ke server di komputer lain. inet_pton() mengkonversi string IP seperti "127.0.0.1" ke format biner yang dipahami sistem. connect() adalah titik di mana koneksi TCP benar-benar terjadi.

```c
char   buf[BUF_SIZE];
    fd_set read_fds;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        select(sock + 1, &read_fds, NULL, NULL, NULL);

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
```

`select()` yang memberitahu program mana dari beberapa fd yang sudah siap dibaca tanpa harus memblok di salah satunya. `FD_ZERO` mereset set fd, `FD_SET` mendaftarkan dua fd yang ingin dipantau yaitu sock (socket ke server) dan `STDIN_FILENO` (keyboard). `select()` kemudian memblok sampai salah satu atau keduanya ada data, lalu `FD_ISSET` mengecek mana yang siap. Jika socket yang siap, data dari server diterima dan ditampilkan ke layar. Jika stdin yang siap, ketikan user dibaca dan dikirim ke server. 

**Output**


**Kendala**

Tidak ada kendala

</details>

   

Tidak ada kendala
</details>
