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
#include "../libs/structures.h"

int main(int argc, char const *argv[]) {

    int CLIENT_PID = getpid();
    char CLIENT_PID_STRING[10];
    int mainQueue = msgget(SERVER_QUEUE_KEY, IPC_CREAT|0777);
    printf("pid %d\n", mainQueue);
    InitialMessage handshake;
    handshake.type = 1;
    printf("pass your name:");
    scanf("%s", handshake.userName);
    printf("name %s", handshake.userName);
    sprintf(CLIENT_PID_STRING, "%d", CLIENT_PID);
    strcpy(handshake.pid, CLIENT_PID_STRING);
    int clientServerQueue = msgget(CLIENT_PID, IPC_CREAT|0777);
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
//        printf("welcome on chat! \n");
//        Message chatMessage;
//        chatMessage.type = 1;
//        while (true) {
//            scanf("%[s\n]", chatMessage.content);
//            fgets(chatMessage.content, sizeof(chatMessage.content), stdin);
//            printf("message %s", chatMessage.content);
//            msgsnd(clientServerQueue, &chatMessage, MESSAGE_CONTENT_SIZE, 0);
//        }
    } else {
        while (true) {
            int res = kill(serverPid, 0);

            if (res == -1) {
                printf("connection lost - server dead");
                return -1;
            } else {
                sleep(2);
            }
        }
    }

    msgctl(mainQueue, IPC_RMID, NULL);
    msgctl(clientServerQueue, IPC_RMID, NULL);
    // if (fork() == 0) {
    //
    //   msgrcv(mainQueue, &msg, 512, 1, 0);
    // }
    return 0;
}
