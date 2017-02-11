//
// Created by witek on 01.02.17.
//
#ifndef CLIENT_STRUCTURES_H
#define CLIENT_STRUCTURES_H

#define SEMAPHORE_RAISE 1
#define SEMAPHORE_DROP -1


#define PLAYERS_STRUCTURE_KEY 666
#define PLAYERS_SEMAPHORE_KEY 666

#define LOBBY_STRUCTURE_KEY 665
#define LOBBY_SEMAPHORE_KEY 665
#define SERVER_QUEUE_KEY 1234
#define SERVER_INTERNAL_QUEUE_KEY 123
#define MAX_PLAYER_AMOUNT 100
#define MAX_PID_SIZE 10
#define MESSAGE_CONTENT_SIZE 512
#define CHAT_CLIENT_TO_SERVER 1
#define CHAT_SERVER_TO_CLIENT 2
#define USER_NAME_LENGTH 50
#define IN_GAME 2
#define AWAITING 1
#define DISCONNECTED 0
#define LOBBY_SIZE 10



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

typedef struct Message {
    long type;
    char source[USER_NAME_LENGTH];
    char content[MESSAGE_CONTENT_SIZE];
} Message;

typedef struct InitialMessage {
    long type;
    char pid[MAX_PID_SIZE];
    char userName[USER_NAME_LENGTH];
} InitialMessage;


#endif //CLIENT_STRUCTURES_H
