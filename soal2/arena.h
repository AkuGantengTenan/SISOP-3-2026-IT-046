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

// Struktur Pesan (Message Queue)
typedef struct {
    long mtype;
    pid_t sender_pid;
    char command[50];
    char data1[50]; // Username pengirim
    char data2[50]; // Password atau Target
    int value;      // Damage / Choice
} Message;

// Struktur Profil Pemain
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
    int battle_id;       // Sedang di arena nomor berapa
    time_t search_start; // Waktu mulai mencari lawan
} User;

// Struktur Arena Pertempuran
typedef struct {
    int active; // 0 = Kosong, 1 = Berjalan, 2 = Selesai
    char p1[50];
    char p2[50];
    int hp1;
    int hp2;
    int max_hp1;
    int max_hp2;
    char logs[5][100]; // 5 Log pertempuran
    int is_bot_match;
    int p1_won;        // 1 jika p1 menang, 0 jika p2 menang
} BattleArena;

// Memori Bersama (Shared Memory)
typedef struct {
    User users[MAX_USERS];
    int user_count;
    BattleArena arena[50]; // Maksimal 50 arena simultan
} SharedData;

#endif
