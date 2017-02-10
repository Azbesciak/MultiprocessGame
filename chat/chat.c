#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <string.h>
#include <stdbool.h>
#include "../libs/structures.h"

int main(int argc, char const *argv[]) {
    Message msg;
    int clientServerQueue = 1234;
    if (argc > 1) {
        printf("%s", argv[1]);
        clientServerQueue = atoi(argv[1]);
    }
    int queue = msgget(clientServerQueue, IPC_CREAT | 0777);
    msg.type = 1;
    printf("witaj w chacie!");
//    while (1) {
//        scanf("%s", msg.text);
//        msgsnd(queue, &msg, 10, 0);
//    }
    Message chatMessage;
    chatMessage.type = 1;
    while (true) {
        scanf("%[s\n]", chatMessage.content);
        fgets(chatMessage.content, sizeof(chatMessage.content), stdin);
        printf("message %s", chatMessage.content);
        msgsnd(clientServerQueue, &chatMessage, MESSAGE_CONTENT_SIZE, 0);
    }



    return 0;
}
