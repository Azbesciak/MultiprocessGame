#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/msg.h>
#include <string.h>
#include <stdbool.h>
#include "../libs/structures.h"

int main(int argc, char const *argv[]) {
    Message chatMessage;
    int CLIENT_PID = 1234;
    if (argc > 1) {
        printf("Podłączony do chatu nr %s \n", argv[1]);
        CLIENT_PID = atoi(argv[1]);
        strcpy(chatMessage.source, argv[1]);
    }
    if (argc > 2) {
        strcpy(chatMessage.source, argv[2]);
    } else {
        strcat(chatMessage.source, "unknown");
    }
    int clientServerQueue = msgget(CLIENT_PID, IPC_CREAT | 0777);
    printf("Witaj w chacie, %s \n", chatMessage.source);
    chatMessage.type = CHAT_CLIENT_TO_SERVER;
    if (fork() == 0) {
        while (true) {
            scanf("%[s\n]", chatMessage.content);
            fgets(chatMessage.content, sizeof(chatMessage.content), stdin);
            msgsnd(clientServerQueue, &chatMessage, MESSAGE_CONTENT_SIZE, 0);
        }
    } else {
        while (true) {
            msgrcv(clientServerQueue, &chatMessage, MESSAGE_CONTENT_SIZE, CHAT_SERVER_TO_CLIENT, 0);
            printf("%s : %s\n", chatMessage.source, chatMessage.content);
        }
    }
    return 0;
}
