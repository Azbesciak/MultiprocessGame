//
// Created by witek on 01.02.17.
//
#ifndef CLIENT_STRUCTURES_H
#define CLIENT_STRUCTURES_H

#define SEMAPHORE_RAISE 1
#define SEMAPHORE_DROP -1


#define PLAYERS_STRUCTURE_KEY 666
#define PLAYERS_SEMAPHORE_KEY 666


#define SERVER_QUEUE_KEY 1234
#define SERVER_INTERNAL_QUEUE_KEY 123
#define MAX_PLAYER_AMOUNT 100
#define MAX_PID_SIZE 10

#define USER_NAME_LENGTH 50
#define IN_GAME 2
#define AWAITING 1
#define DISCONNECTED 0

// Chat consts
#define MESSAGE_CONTENT_SIZE 512
#define CHAT_CLIENT_TO_SERVER 3
#define CHAT_SERVER_TO_CLIENT 4

// Lobby consts
#define LOBBY_SIZE 10
#define LOBBY_STRUCTURE_KEY 665
#define LOBBY_SEMAPHORE_KEY 665
#define EMPTY 0
#define PLAYER_AWAITING 1
#define IN_GAME 2

// Game consts
#define GAME_CLIENT_TO_SERVER 1
#define GAME_SERVER_TO_CLIENT 2

typedef struct Player {
    int pid;
    int queueId;
    char name[USER_NAME_LENGTH];
    int state;
} Player;

typedef struct Room {
    int state;
    Player players[2];
} Room;

typedef struct Lobby {
    int sem;
    int memKey;
    Room * rooms;
} Lobby;

typedef struct PlayersMemory {
    int sem;
    int memKey;
    Player * players;
} PlayersMemory;

typedef struct GameMessage {
    long type;
    char command[MESSAGE_CONTENT_SIZE];
} GameMessage;

typedef struct ChatMessage {
    long type;
    char source[USER_NAME_LENGTH];
    char content[MESSAGE_CONTENT_SIZE];
} ChatMessage;

typedef struct InitialMessage {
    long type;
    char pid[MAX_PID_SIZE];
    char userName[USER_NAME_LENGTH];
} InitialMessage;


#endif //CLIENT_STRUCTURES_H
