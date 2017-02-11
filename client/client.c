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
void maintainGame(int id, int queue, GameMessage *pMessage);

int main(int argc, char const *argv[]) {
    initializeSemaphore();
    int CLIENT_PID = getpid();
    char CLIENT_PID_STRING[10];
    int mainQueue = msgget(SERVER_QUEUE_KEY, IPC_CREAT | 0777);
    printf("pid %d\n", mainQueue);
    InitialMessage handshake;
    handshake.type = 1;
    printf("pass your name:");
    scanf("%s", handshake.userName);
    printf("name %s \n", handshake.userName);
    sprintf(CLIENT_PID_STRING, "%d", CLIENT_PID);
    strcpy(handshake.pid, CLIENT_PID_STRING);
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
    msgrcv(clientServerQueue, &handshake, MAX_PID_SIZE, 2, 0);
    printf("Connected! server pid : %s\n", handshake.pid);
    int serverPid = atoi(handshake.pid);
    if (fork() == 0) {
        char command[80];
        strcat(command, "xterm -e ../../chat/chat ");
        strcat(command, CLIENT_PID_STRING);
        strcat(command, " ");
        strcat(command, handshake.userName);
//        printf("%s \n", command);
        system(command);
        printf("chat closed!\n");
    } else if (fork() == 0) { //maintain game
        GameMessage msg;
        int response = 0;
        int roomId = -1;
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
            msgrcv(clientServerQueue, &msg, MESSAGE_CONTENT_SIZE, GAME_SERVER_TO_CLIENT, 0);

            if (isdigit(msg.command[0])) {
                response = atoi(msg.command);
                if (response == 1) {
                    printf("Successfully added to room!\n");
                } else if (response == 2) {
                    printf("Game started!\n");
                }
            } else {
                printf("%s\n", msg.command);
            }
        }
        maintainGame(roomId, clientServerQueue, &msg);
    } else {
        while (true) { //control server lifestyle
            int res = kill(serverPid, 0);

            if (res == -1) {
                printf("connection lost - server dead");
                msgctl(clientServerQueue, IPC_RMID, NULL);
                return -1;
            } else {
                sleep(2);
            }
        }
    }
    return 0;
}

void printGameState(GameMatrix * matrix) {
    semaphoreOperation(matrix->sem, SEMAPHORE_DROP);
    for (int row = 0; row < GAME_MATRIX_SIZE; row++) {
        for (int column = 0; column < GAME_MATRIX_SIZE; column++) {
            printf("%c",matrix->matrix[row * GAME_MATRIX_SIZE + column]);
        }
        printf("\n");
    }

    semaphoreOperation(matrix->sem, SEMAPHORE_RAISE);
}

void maintainGame(int roomId, int serverClientQueue, GameMessage * msg) {
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
            shmctl(matrix.memKey, IPC_RMID, 0);
            semctl(matrix.sem, IPC_RMID, 0);
            return;
        }
        printGameState(&matrix);
        if (!isdigit(msg->command[0])) {
            printf("%s\n", msg->command);
            shmctl(matrix.memKey, IPC_RMID, 0);
            semctl(matrix.sem, IPC_RMID, 0);
            break;
        } else {
            result = atoi(msg->command);
            if (result == GAME_MOVE_REJECTED || result == GAME_YOUR_TOUR) {
                printf("your move!\n");
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
}

