#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <sys/shm.h>
#include "../libs/structures.h"

struct sembuf semaphore;

void initializeSemaphore() {
    semaphore.sem_num = 0;
    semaphore.sem_flg = 0;
}
void semaphoreOperation(int semId, short todo) {
    semaphore.sem_op = todo;
    semop(semId, &semaphore, 1);
}
bool maintainGame(int id, int queue, GameMessage *pMessage, int playerIndex);

void runChat(int  CLIENT_PID, char * userName) {
    char command[80];
    char CLIENT_PID_STRING[10];
    sprintf(CLIENT_PID_STRING, "%d", CLIENT_PID);
    strcpy(command, "xterm -e ../../chat/chat ");
    strcat(command, CLIENT_PID_STRING);
    strcat(command, " ");
    strcat(command, userName);
//    printf("%s \n", command);
    system(command);
    printf("chat closed!\n");
    exit(0);
}

int main(int argc, char const *argv[]) {
    initializeSemaphore();
    int CLIENT_PID = getpid();

    int mainQueue = msgget(SERVER_QUEUE_KEY, IPC_CREAT | 0777);
    printf("pid %d\n", mainQueue);
    InitialMessage handshake;
    handshake.type = 1;
    printf("pass your name:\n");
    scanf("%s", handshake.userName);
    printf("name %s \n", handshake.userName);

    handshake.pid = CLIENT_PID;
    int clientServerQueue = msgget(CLIENT_PID, IPC_CREAT | 0777);
    int result = msgsnd(mainQueue, &handshake, MAX_PID_SIZE + USER_NAME_LENGTH, 0);
    if (result == -1) {
        perror("error while connecting server");
        printf("try again...\n");
        mainQueue = msgget(SERVER_QUEUE_KEY, IPC_CREAT | 0777);
        result = msgsnd(mainQueue, &handshake, MAX_PID_SIZE + USER_NAME_LENGTH, 0);
        if (result == -1) {
            perror("failed again");
            return -1;
        }
    } else {
        printf("Waiting for server response...\n");
    }
    result = msgrcv(clientServerQueue, &handshake, MAX_PID_SIZE, 2, 0);
    if (result == -1) {
        perror("error occurred while connecting server");
        return -1;
    }
    printf("Connected! server pid : %d\n", handshake.pid);
    int serverPid = handshake.pid;
    int gamePid;
    if (fork() == 0) {
        runChat(CLIENT_PID, handshake.userName);
    } else if ((gamePid = fork()) == 0) { //maintain game
        bool wantToContinue = true;
        while (wantToContinue) {
            GameMessage msg;
            int response = 0;
            int roomId = -1;
            int playerIndex = -1;
            msgrcv(clientServerQueue, &msg, MESSAGE_CONTENT_SIZE, GAME_SERVER_TO_CLIENT, 0);
            printf("%s\n", msg.command);
            while (response != 2) {
                if (response != 1) {
                    scanf("%s", msg.command);
                    if (isdigit(msg.command[0])) {
                        roomId = atoi(msg.command);
                    }
                    msg.type = GAME_CLIENT_TO_SERVER;
                    msgsnd(clientServerQueue, &msg, MESSAGE_CONTENT_SIZE, 0);
                }
                result = msgrcv(clientServerQueue, &msg, MESSAGE_CONTENT_SIZE, GAME_SERVER_TO_CLIENT, 0);

                if (result == -1) {
                    perror("server communication broken");
                    return -1;
                }
                if (isdigit(msg.command[0])) {
                    response = atoi(msg.command);
                    if (response == 1) {
                        printf("Successfully added to room!\n");
                        playerIndex = 0;
                    } else if (response == 2) {
                        printf("Game started!\n");
                        if (playerIndex == -1) { //joined to already existing room
                            playerIndex = 1;
                        }
                    }
                } else {
                    printf("%s\n", msg.command);
                }
            }
            wantToContinue = maintainGame(roomId, clientServerQueue, &msg, playerIndex);
        }
        msgctl(clientServerQueue, IPC_RMID, NULL);
        kill(CLIENT_PID, 9);
        exit(0);
    } else {
        int serverState;
        int gameState;
        while (true) { //control server lifestyle
            serverState = kill(serverPid, 0);
            if (serverState == -1) {
                printf("connection lost - server dead\n");
                msgctl(clientServerQueue, IPC_RMID, NULL);
                return -1;
            } else {
                gameState = kill(gamePid, 0);
                if (gameState == -1) {
                    printf("Game finished!\n");
                    msgctl(clientServerQueue, IPC_RMID, NULL);
                    return -1;
                } else {
                    sleep(2);
                }
            }
        }
    }
    return 0;
}

void printBorder() {
    printf(" ");
    for (int i = 0; i < GAME_MATRIX_SIZE; i++) {
        printf("-");
    }
    printf(" \n");
}

void printGameState(GameMatrix * matrix) {
    char temp;
    printBorder();
    semaphoreOperation(matrix->sem, SEMAPHORE_DROP);
    for (int row = GAME_MATRIX_SIZE - 1; row >= 0; row--) {
        printf("|");
        for (int column = 0; column < GAME_MATRIX_SIZE; column++) {
            temp = matrix->matrix[row * GAME_MATRIX_SIZE + column];
            if (temp == GAME_PLAYER_0_SIGN ){
                printf("%s%c%s", ANSI_COLOR_GREEN, temp, ANSI_COLOR_RESET);
            } else if (temp == GAME_PLAYER_1_SIGN) {
                printf("%s%c%s", ANSI_COLOR_BLUE, temp, ANSI_COLOR_RESET);
            } else {
                printf(" ");
            }
        }
        printf("|\n");
    }
    semaphoreOperation(matrix->sem, SEMAPHORE_RAISE);
    printBorder();
}
void cleanUpGame(GameMatrix * matrix) {
    shmctl(matrix ->memKey, IPC_RMID, 0);
    semctl(matrix->sem, IPC_RMID, 0);
}

void printPlayerSign(int playerIndex) {
    printf("Your sign - ");
    if (playerIndex == 0) {
        printf("%s%c%s\n", ANSI_COLOR_GREEN, GAME_PLAYER_0_SIGN, ANSI_COLOR_RESET);
    } else {
        printf("%s%c%s\n", ANSI_COLOR_BLUE, GAME_PLAYER_1_SIGN, ANSI_COLOR_RESET);
    }
}

bool maintainGame(int roomId, int serverClientQueue, GameMessage * msg, int playerIndex) {
    GameMatrix matrix;
    matrix.sem = semget(roomId + GAME_ROOM_KEY_ADDER, 1, IPC_CREAT | 0777);
    semctl(matrix.sem, SETVAL, 1);
    matrix.memKey = shmget(roomId + GAME_ROOM_KEY_ADDER, GAME_MATRIX_SIZE * GAME_MATRIX_SIZE + 1, IPC_CREAT | 0777);
    matrix.matrix = shmat(matrix.memKey, 0, 0);
    int result;

    while (true) {
        result = msgrcv(serverClientQueue, msg, MESSAGE_CONTENT_SIZE, GAME_SERVER_TO_CLIENT, 0);
        if (result == -1) {
            printf("queue broken \n");
            cleanUpGame(&matrix);
            return false;
        }
        printGameState(&matrix);
        if (!isdigit(msg->command[0])) {
            printf("%s\n", msg->command);
            return false;
//            printf("want to rejoin? y/n \n");
//            char answer[1];
//            cleanUpGame(&matrix);
//            scanf("%s", answer);
//            if (strcmp(answer,"y") == 0 || strcmp(answer, "Y") == 0) {
//                return true;
//            } else {
//                return false;
//            }
        } else {
            result = atoi(msg->command);
            if (result == GAME_MOVE_REJECTED || result == GAME_YOUR_TOUR) {
                switch (result) {
                    case GAME_MOVE_REJECTED: printf("Move rejected! Try again.\n");
                    default: printf("your move!\n");
                }
                printPlayerSign(playerIndex);
                scanf("%s", msg->command);
                msg->type = GAME_CLIENT_TO_SERVER;
                msgsnd(serverClientQueue, msg, MESSAGE_CONTENT_SIZE, 0);
            } else if (result == GAME_MOVE_ACCEPTED) {
                printf("your oponent's turn!\n");
            } else {
                printf("%s\n", msg->command);

            }
        }
    }
    return false;
}

