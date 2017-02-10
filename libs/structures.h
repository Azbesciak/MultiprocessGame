//
// Created by witek on 01.02.17.
//

#ifndef CLIENT_STRUCTURES_H
#define CLIENT_STRUCTURES_H


#define SERVER_QUEUE_KEY 1234
#define MAX_PLAYER_AMOUNT 100
#define MAX_PID_SIZE 10
#define MESSAGE_CONTENT_SIZE 512
#define USER_NAME_LENGTH 50
#define IN_GAME 1
#define AWAING 0

typedef struct Message {
    long type;
    char content[MESSAGE_CONTENT_SIZE];
} Message;

typedef struct InitialMessage {
    long type;
    char pid[MAX_PID_SIZE];
    char userName[USER_NAME_LENGTH];
} InitialMessage;

typedef struct Player {
    int pid;
    int queueId;
    char name[USER_NAME_LENGTH];
    int state;
} Player;

#endif //CLIENT_STRUCTURES_H
