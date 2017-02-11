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
#include "../libs/structures.h"

int main(int argc, char const *argv[]) {

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
        return -1;
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
        printf("%s \n", command);
        system(command);
        printf("chat closed!\n");
    } else if (fork() == 0) { //maintain game
        GameMessage msg;
        int response = 0;
            msgrcv(clientServerQueue, &msg, MESSAGE_CONTENT_SIZE, GAME_SERVER_TO_CLIENT, 0);
        printf("%s\n", msg.command);
        while (response != 2) {
            if (response != 1) {
                scanf("%s", msg.command);
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
