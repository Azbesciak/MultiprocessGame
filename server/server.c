#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "../libs/structures.h"
Player players[MAX_PLAYER_AMOUNT];
bool isAvailable[MAX_PLAYER_AMOUNT];
int playersCounter = 0;

void initializeGlobals() {
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        isAvailable[i] = true;
    }
}
void printAllAvailablePlayers() {
    int counter = 0;
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        if (!isAvailable[i]) {
            printf("User %s, pid : %d, state: %d \n", players[i].name, players[i].pid, players[i].state);
            counter++;
        }
    }
    printf("counter: %d, should be %d \n", counter, playersCounter);
    printf("sizeof %d \n", (int)sizeof(players));
}

void addPlayer(int pid, int userQueue, char *name) {
    if (playersCounter < MAX_PLAYER_AMOUNT - 1) {
        int i = 0;
        while (i < MAX_PLAYER_AMOUNT && !isAvailable[i]) {
            i++;
        }
        Player player;
        player.pid = pid;
        strcpy(player.name, name);
        player.state = AWAING;
        player.queueId = msgget(pid, IPC_CREAT|07777);
        players[i] = player;
        isAvailable[i] = false;
        playersCounter++;
    } else {
        printf("Max player amount reached! Player not created");
    }
}

void removePlayer(int pid) {
    if (playersCounter > 0) {
        for (int i = 0; i < playersCounter; i++) {
            if (players[i].pid == pid) {
                printf("Player %s left the game", players[i].name);
                isAvailable[i] = true;
                playersCounter--;
                break;
            }
        }
    } else {
        printf("No players in game!");
    }
}

void maintainPlayersLifecycle(char* serverPid) {
    printf("server is running... \n");
    int mainQueue = msgget(SERVER_QUEUE_KEY, IPC_CREAT|0777);
    if (mainQueue == -1) {
        perror("error while creating queue");
        return;
    }
    InitialMessage initialMessage;
    ssize_t result;
    int maintainProcess = fork();
    if (maintainProcess == 0) {
        while (true) {
            result = msgrcv(mainQueue, &initialMessage, MAX_PID_SIZE + USER_NAME_LENGTH, 1, 0);
            if (result == -1) {
                perror("error while connecting client");
                continue;
            } else {
                int clientPid = atoi(initialMessage.pid);
                int clientServerQueue = msgget(clientPid, IPC_CREAT|0777); //tworzy kolejkę dla uzytkownika
                int clientProcess = fork();
                if (clientProcess == 0) {
                    Message msg;
                    msg.type = 2;
                    strcpy(msg.content, serverPid);
                    result = msgsnd(clientServerQueue, &msg, MAX_PID_SIZE, 0);
                    if (result == -1) {
                        perror("error while sending message to client");
                        continue;
                    } else {
                        char *user = initialMessage.userName;
                        if (fork() == 0) {
                            while (true) {
                                int res = msgrcv(clientServerQueue, &msg, MESSAGE_CONTENT_SIZE, 1, 0);
                                if (res == -1) {
                                    msgctl(clientServerQueue, IPC_RMID, 0);
                                    return;
                                } else {
                                    printf("message from %s : %s \n", user, msg.content);
                                }
                            }
                        } else {
                            while (true) {
                                int isAlive = kill(clientPid, 0);
                                if (isAlive == -1) {
                                    printf("user %s has disconnected\n", user);
                                    removePlayer(clientPid);
                                    msgctl(clientServerQueue, IPC_RMID, 0);
                                    break;
                                } else {
                                    sleep(2);

                                }
                            }
                        }
                    }

                } else {
                    printf("player %s connected to server - pid : %d \n", initialMessage.userName, clientPid);
                    addPlayer(clientPid, clientServerQueue, initialMessage.userName);
                    printAllAvailablePlayers();
                }
            }


        }
    }
    else {
        wait(0);
        bool canContinue = true;
        char command[10];
        do {
            printf("pass a command");
            scanf("%s", command);
            if (strcmp(command, "end") == 0) {
                canContinue = false;
            }
        } while (canContinue);
        msgctl(mainQueue, IPC_RMID, 0);
    }

}

int main(int argc, char const *argv[]) {
    initializeGlobals();
    char serverPid[20];
    sprintf(serverPid, "%d", getpid());
    maintainPlayersLifecycle(serverPid);
    return 0;
}
