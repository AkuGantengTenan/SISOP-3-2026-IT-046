#ifndef ARENA_H
#define ARENA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <termios.h>
#include <time.h>
#include <fcntl.h>

#define SHM_KEY 0x1234
#define MSG_KEY 0x5678
#define MAX_USERS 100

typedef struct {
    long mtype;
    pid_t sender_pid;
    char command[50];
    char data1[50]; 
    char data2[50]; 
    int value;      
} Message;

typedef struct {
    char username[50];
    char password[50];
    int gold;
    int lvl;
    int xp;
    int base_damage;
    int base_health;
    int weapon_bonus;
    int is_online;
    int is_searching;
    int in_battle;
    int battle_id;       
    time_t search_start; 
} User;

typedef struct {
    int active; 
    char p1[50];
    char p2[50];
    int hp1;
    int hp2;
    int max_hp1;
    int max_hp2;
    char logs[5][100];
    int is_bot_match;
    int p1_won;       
} BattleArena;

typedef struct {
    User users[MAX_USERS];
    int user_count;
    BattleArena arena[50]; ltan
} SharedData;

#endif
