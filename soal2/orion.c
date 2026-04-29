#include "arena.h"

SharedData *shm;
int msgid;

void save_data() {
    FILE *f = fopen("users.dat", "wb");
    if(f) { fwrite(shm, sizeof(SharedData), 1, f); fclose(f); }
}

void load_data() {
    FILE *f = fopen("users.dat", "rb");
    if(f) { fread(shm, sizeof(SharedData), 1, f); fclose(f); }
   
    for(int i=0; i<shm->user_count; i++) {
        shm->users[i].is_online = 0;
        shm->users[i].is_searching = 0;
        shm->users[i].in_battle = 0;
    }
}


void add_log(int arena_id, const char* new_log) {
    for(int i=0; i<4; i++) strcpy(shm->arena[arena_id].logs[i], shm->arena[arena_id].logs[i+1]);
    strcpy(shm->arena[arena_id].logs[4], new_log);
}


void *matchmaking_thread(void *arg) {
    while(1) {
        for(int i=0; i<shm->user_count; i++) {
            if(shm->users[i].is_searching) {
                time_t now = time(NULL);
                int wait_time = difftime(now, shm->users[i].search_start);

                int found = -1;
                for(int j=0; j<shm->user_count; j++) {
                    if(i != j && shm->users[j].is_searching) { found = j; break; }
                }

                if(found != -1) {
                    int a_id = -1;
                    for(int k=0; k<50; k++) { if(shm->arena[k].active == 0) { a_id = k; break; } }
                    
                    shm->arena[a_id].active = 1;
                    shm->arena[a_id].is_bot_match = 0;
                    strcpy(shm->arena[a_id].p1, shm->users[i].username);
                    strcpy(shm->arena[a_id].p2, shm->users[found].username);
                    
                    shm->arena[a_id].max_hp1 = shm->users[i].base_health + (shm->users[i].xp / 10);
                    shm->arena[a_id].max_hp2 = shm->users[found].base_health + (shm->users[found].xp / 10);
                    shm->arena[a_id].hp1 = shm->arena[a_id].max_hp1;
                    shm->arena[a_id].hp2 = shm->arena[a_id].max_hp2;
                    add_log(a_id, "Pertempuran PvP Dimulai!");

                    shm->users[i].is_searching = 0; shm->users[i].in_battle = 1; shm->users[i].battle_id = a_id;
                    shm->users[found].is_searching = 0; shm->users[found].in_battle = 1; shm->users[found].battle_id = a_id;
                } 
                else if(wait_time >= 35) {
                 
                    int a_id = -1;
                    for(int k=0; k<50; k++) { if(shm->arena[k].active == 0) { a_id = k; break; } }
                    
                    shm->arena[a_id].active = 1;
                    shm->arena[a_id].is_bot_match = 1;
                    strcpy(shm->arena[a_id].p1, shm->users[i].username);
                    strcpy(shm->arena[a_id].p2, "Monster (Bot)");
                    
                    shm->arena[a_id].max_hp1 = shm->users[i].base_health + (shm->users[i].xp / 10);
                    shm->arena[a_id].max_hp2 = 100;
                    shm->arena[a_id].hp1 = shm->arena[a_id].max_hp1;
                    shm->arena[a_id].hp2 = shm->arena[a_id].max_hp2;
                    add_log(a_id, "Melawan Monster Bot!");

                    shm->users[i].is_searching = 0; shm->users[i].in_battle = 1; shm->users[i].battle_id = a_id;
                }
            }
        }
        sleep(1);
    }
    return NULL;
}

int main() {
    int shmid = shmget(SHM_KEY, sizeof(SharedData), IPC_CREAT | 0666);
    shm = (SharedData*) shmat(shmid, NULL, 0);
    msgid = msgget(MSG_KEY, IPC_CREAT | 0666);

    load_data();
    printf("Orion is ready (PID: %d)\n", getpid());

    pthread_t mm_thread;
    pthread_create(&mm_thread, NULL, matchmaking_thread, NULL);

    Message req, res;
    while(1) {
        msgrcv(msgid, &req, sizeof(Message) - sizeof(long), 1, 0);
        res.mtype = req.sender_pid;

        if(strcmp(req.command, "REGISTER") == 0) {
            int exists = 0;
            for(int i=0; i<shm->user_count; i++) { if(strcmp(shm->users[i].username, req.data1) == 0) exists = 1; }
            if(exists) strcpy(res.command, "FAILED: Username exist");
            else {
                User u;
                strcpy(u.username, req.data1); strcpy(u.password, req.data2);
                u.gold = 150; u.lvl = 1; u.xp = 0; u.base_damage = 10; u.base_health = 100;
                u.weapon_bonus = 0; u.is_online = 0; u.is_searching = 0; u.in_battle = 0;
                shm->users[shm->user_count++] = u;
                save_data();
                strcpy(res.command, "SUCCESS");
            }
            msgsnd(msgid, &res, sizeof(Message) - sizeof(long), 0);
        }
        else if(strcmp(req.command, "LOGIN") == 0) {
            int success = 0;
            for(int i=0; i<shm->user_count; i++) {
                if(strcmp(shm->users[i].username, req.data1) == 0 && strcmp(shm->users[i].password, req.data2) == 0) {
                    if(shm->users[i].is_online) { strcpy(res.command, "FAILED: Already login"); success = -1; break; }
                    shm->users[i].is_online = 1; success = 1; break;
                }
            }
            if(success == 1) strcpy(res.command, "SUCCESS");
            else if(success == 0) strcpy(res.command, "FAILED: Wrong auth");
            msgsnd(msgid, &res, sizeof(Message) - sizeof(long), 0);
        }
        else if(strcmp(req.command, "LOGOUT") == 0) {
            for(int i=0; i<shm->user_count; i++) {
                if(strcmp(shm->users[i].username, req.data1) == 0) { shm->users[i].is_online = 0; break; }
            }
            strcpy(res.command, "SUCCESS"); msgsnd(msgid, &res, sizeof(Message) - sizeof(long), 0);
        }
        else if(strcmp(req.command, "FIND_MATCH") == 0) {
            for(int i=0; i<shm->user_count; i++) {
                if(strcmp(shm->users[i].username, req.data1) == 0) {
                    shm->users[i].is_searching = 1;
                    shm->users[i].search_start = time(NULL);
                    break;
                }
            }
            strcpy(res.command, "SEARCHING"); msgsnd(msgid, &res, sizeof(Message) - sizeof(long), 0);
        }
        else if(strcmp(req.command, "ATTACK") == 0) {
            char log_msg[100];
            int dmg = req.value;
           
            for(int i=0; i<shm->user_count; i++) {
                if(strcmp(shm->users[i].username, req.data1) == 0) {
                    int a_id = shm->users[i].battle_id;
                    if(strcmp(shm->arena[a_id].p1, req.data1) == 0) {
                        shm->arena[a_id].hp2 -= dmg;
                        sprintf(log_msg, "> %s hit for %d damage!", req.data1, dmg);
                    } else {
                        shm->arena[a_id].hp1 -= dmg;
                        sprintf(log_msg, "> %s hit for %d damage!", req.data1, dmg);
                    }
                    add_log(a_id, log_msg);

                    if(shm->arena[a_id].hp1 <= 0 || shm->arena[a_id].hp2 <= 0) {
                        shm->arena[a_id].active = 2;
                        shm->arena[a_id].p1_won = (shm->arena[a_id].hp2 <= 0) ? 1 : 0;
                    }
                    break;
                }
            }
           
        }
    }
    return 0;
}
