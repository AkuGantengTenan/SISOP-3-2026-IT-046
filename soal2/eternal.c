#include "arena.h"

SharedData *shm;
int msgid;
pid_t my_pid;
char my_user[50];
int in_battle_loop = 0;

void set_terminal_mode(int enable) {
    static struct termios oldt, newt;
    if(enable) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    } else { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); }
}

void send_request(Message *req, Message *res) {
    req->mtype = 1; req->sender_pid = my_pid;
    msgsnd(msgid, req, sizeof(Message) - sizeof(long), 0);
    msgrcv(msgid, res, sizeof(Message) - sizeof(long), my_pid, 0);
}

// THREAD 1: Merender Layar Real-time
void *ui_thread_func(void *arg) {
    int u_idx;
    while(in_battle_loop) {
        for(int i=0; i<shm->user_count; i++) { if(strcmp(shm->users[i].username, my_user)==0) u_idx = i; }
        int a_id = shm->users[u_idx].battle_id;
        
        system("clear");
        printf("=== ARENA ===\n");
        printf("%s [%d/%d]  VS  %s [%d/%d]\n\n", 
               shm->arena[a_id].p1, shm->arena[a_id].hp1, shm->arena[a_id].max_hp1,
               shm->arena[a_id].p2, shm->arena[a_id].hp2, shm->arena[a_id].max_hp2);
        
        printf("Combat Log:\n");
        for(int i=0; i<5; i++) printf("%s\n", shm->arena[a_id].logs[i]);
        printf("\n[Tekan 'a' Attack | 'u' Ultimate]\n");
        
        if(shm->arena[a_id].active == 2) {
            in_battle_loop = 0; // Menghentikan input loop
            break;
        }
        usleep(100000); // Update 10x per detik
    }
    return NULL;
}

void battle_mode() {
    Message req, res;
    req.mtype = 1; req.sender_pid = my_pid;
    strcpy(req.command, "FIND_MATCH"); strcpy(req.data1, my_user);
    send_request(&req, &res);

    int u_idx;
    // Menunggu Matchmaking (Loop UI Searching)
    while(1) {
        for(int i=0; i<shm->user_count; i++) { if(strcmp(shm->users[i].username, my_user)==0) u_idx = i; }
        if(shm->users[u_idx].in_battle == 1) break;
        
        system("clear");
        time_t now = time(NULL);
        int elapsed = difftime(now, shm->users[u_idx].search_start);
        printf("Searching for an opponent... [%d s]\n", elapsed);
        sleep(1);
    }

    // MEMASUKI ARENA (Mulai Multithreading)
    in_battle_loop = 1;
    set_terminal_mode(1); // Mode Raw (tanpa enter)
    
    pthread_t ui_thread;
    pthread_create(&ui_thread, NULL, ui_thread_func, NULL);

    time_t last_attack = 0;

    // THREAD 2 (Main): Membaca Input Keyboard Non-Blocking
    while(in_battle_loop) {
        char c = getchar();
        if(c == 'a') {
            time_t now = time(NULL);
            if(difftime(now, last_attack) >= 1) { // Cooldown 1 detik
                last_attack = now;
                int dmg = shm->users[u_idx].base_damage + (shm->users[u_idx].xp / 50) + shm->users[u_idx].weapon_bonus;
                
                Message atk_req;
                atk_req.mtype = 1; atk_req.sender_pid = my_pid;
                strcpy(atk_req.command, "ATTACK"); strcpy(atk_req.data1, my_user);
                atk_req.value = dmg;
                msgsnd(msgid, &atk_req, sizeof(Message) - sizeof(long), 0); // Fire & forget
            }
        }
        // Tambahkan logika 'u' Ultimate mirip dengan 'a' jika ada weapon
    }

    pthread_join(ui_thread, NULL);
    set_terminal_mode(0); // Kembalikan terminal ke normal
    
    // Kalkulasi Menang/Kalah
    system("clear");
    int a_id = shm->users[u_idx].battle_id;
    int is_p1 = (strcmp(shm->arena[a_id].p1, my_user) == 0);
    int i_won = (is_p1 && shm->arena[a_id].p1_won) || (!is_p1 && !shm->arena[a_id].p1_won);

    if(i_won) {
        printf("== VICTORY ==\nBattle ended. Press [ENTER] to continue...\n");
        // Logika update Gold & XP bisa dihandle client atau server (Idealnya server, tapi untuk simpel client-side update SHM)
        shm->users[u_idx].xp += 50; shm->users[u_idx].gold += 120;
    } else {
        printf("== DEFEAT ==\nBattle ended. Press [ENTER] to continue...\n");
        shm->users[u_idx].xp += 15; shm->users[u_idx].gold += 30;
    }
    
    // Level Up
    shm->users[u_idx].lvl = (shm->users[u_idx].xp / 100) + 1;
    shm->users[u_idx].in_battle = 0; shm->users[u_idx].battle_id = -1;
    
    getchar(); // Menunggu Enter
}

void lobby_menu() {
    int choice;
    while(1) {
        system("clear");
        int u_idx;
        for(int i = 0; i < shm->user_count; i++) {
            if(strcmp(shm->users[i].username, my_user) == 0) { u_idx = i; break; }
        }

        printf("===============================\n");
        printf("           PROFILE             \n");
        printf("===============================\n");
        printf("Name : %-10s Lvl : %d\n", shm->users[u_idx].username, shm->users[u_idx].lvl);
        printf("Gold : %-10d XP  : %d\n", shm->users[u_idx].gold, shm->users[u_idx].xp);
        printf("===============================\n\n");
        printf("1. Battle\n2. Armory\n3. History\n4. Logout\n> Choice: ");
        scanf("%d", &choice);

        if(choice == 1) { battle_mode(); }
        else if(choice == 4) {
            Message req, res;
            req.mtype = 1; req.sender_pid = my_pid;
            strcpy(req.command, "LOGOUT"); strcpy(req.data1, my_user);
            send_request(&req, &res);
            break;
        }
    }
}

int main() {
    my_pid = getpid();
    int shmid = shmget(SHM_KEY, sizeof(SharedData), 0666);
    if(shmid < 0) { printf("Orion are you there?\n"); return 1; }
    shm = (SharedData*) shmat(shmid, NULL, 0);
    msgid = msgget(MSG_KEY, 0666);

    int choice;
    Message req, res;
    while(1) {
        system("clear");
        printf("1. Register\n2. Login\n3. Exit\nChoice: ");
        scanf("%d", &choice);

        if(choice == 1 || choice == 2) {
            strcpy(req.command, (choice == 1) ? "REGISTER" : "LOGIN");
            printf("Username: "); scanf("%s", req.data1);
            printf("Password: "); scanf("%s", req.data2);
            send_request(&req, &res);
            
            if(strcmp(res.command, "SUCCESS") == 0 && choice == 2) {
                strcpy(my_user, req.data1);
                lobby_menu();
            } else { printf("%s\n", res.command); sleep(2); }
        } else if(choice == 3) break;
    }
    return 0;
}
