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
#define MAX_PID_SIZE 4

#define USER_NAME_LENGTH 50
#define PLAYER_IN_GAME 3
#define PLAYER_AWAITING_FOR_PARTNER 2
#define PLAYER_AWAITING_FOR_ROOM 1
#define PLAYER_DISCONNECTED 0

// Chat consts
#define MESSAGE_CONTENT_SIZE 512
#define CHAT_CLIENT_TO_SERVER 3
#define CHAT_SERVER_TO_CLIENT 4

// Lobby consts
#define LOBBY_SIZE 10
#define LOBBY_STRUCTURE_KEY 665
#define LOBBY_SEMAPHORE_KEY 665
#define ROOM_EMPTY 0
#define ROOM_PLAYER_AWAITING 1
#define ROOM_IN_GAME 2

// Game consts
#define GAME_CLIENT_TO_SERVER 1
#define GAME_SERVER_TO_CLIENT 2
#define GAME_WANT_TO_CONTINUE 5
#define GAME_MATRIX_SIZE 4
#define GAME_ROOM_KEY_ADDER 50
#define GAME_PLAYER_0_SIGN 'x'
#define GAME_PLAYER_1_SIGN 'o'
#define GAME_FINISHED 3
#define GAME_YOUR_TOUR 2
#define GAME_MOVE_ACCEPTED 1
#define GAME_MOVE_REJECTED 0

// Colors <3
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

typedef struct GameMatrix {
    int sem;
    int memKey;
    char *matrix;
} GameMatrix;

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
    int pid;
    char userName[USER_NAME_LENGTH];
} InitialMessage;


#endif //CLIENT_STRUCTURES_H
