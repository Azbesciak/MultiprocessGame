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
#include <sys/shm.h>
#include <sys/wait.h>
#include "../libs/structures.h"


int playersCounter = 0;
struct sembuf semaphore;

void semaphoreOperation(int semId, short todo) {
    semaphore.sem_op = todo;
    semop(semId, &semaphore, 1);
}

PlayersMemory preparePlayersMemory() {
    PlayersMemory playersMemory;
    playersMemory.memKey = shmget(PLAYERS_STRUCTURE_KEY, sizeof(Player) * MAX_PLAYER_AMOUNT, IPC_CREAT | 0777);
    playersMemory.players = shmat(playersMemory.memKey, 0, 0);
    playersMemory.sem = semget(PLAYERS_SEMAPHORE_KEY, 1, IPC_CREAT | 0700);
    semctl(playersMemory.sem, 0, SETVAL, 1);
    return playersMemory;
}

Lobby prepareLobby() {
    Lobby lobby;
    lobby.memKey = shmget(LOBBY_STRUCTURE_KEY, sizeof(Room) * LOBBY_SIZE, IPC_CREAT | 0777);
    lobby.rooms = shmat(lobby.memKey, 0, 0);
    lobby.sem = semget(LOBBY_SEMAPHORE_KEY, 1, IPC_CREAT | 0777);
    semctl(lobby.sem, 0, SETVAL, 1);
    return lobby;
}

void clearPlayersMemory(PlayersMemory memory) {
    semctl(memory.sem, IPC_RMID, 0);
    shmctl(memory.memKey, IPC_RMID, 0);
}

void initializeGlobals(PlayersMemory memory) {
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        Player player;
        memory.players[i] = player;
        memory.players[i].state = DISCONNECTED;
    }
    semaphore.sem_num = 0;
    semaphore.sem_flg = 0;
}

void printAllAvailablePlayers(PlayersMemory memory) {
    int counter = 0;
    semaphoreOperation(memory.sem, SEMAPHORE_DROP);
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        if (memory.players[i].state != DISCONNECTED) {
            printf("User %s, pid : %d, state: %d \n", memory.players[i].name, memory.players[i].pid, memory.players[i].state);
            counter++;
        }
    }
    semaphoreOperation(memory.sem, SEMAPHORE_RAISE);
    printf("counter: %d, should be %d \n", counter, playersCounter);
}

void addPlayer(int pid, int userQueue, char *name, PlayersMemory memory) {
    semaphoreOperation(memory.sem, SEMAPHORE_DROP);
    if (playersCounter < MAX_PLAYER_AMOUNT - 1) {
        int i = 0;
        while (i < MAX_PLAYER_AMOUNT && memory.players[i].state != DISCONNECTED) {
            i++;
        }
        Player player;
        player.pid = pid;
        strcpy(player.name, name);
        player.state = AWAITING;
        player.queueId = userQueue;
        memory.players[i] = player;
        playersCounter++;
    } else {
        printf("Max player amount reached! Player not created");
    }
    semaphoreOperation(memory.sem, SEMAPHORE_RAISE);
}

void removePlayer(int pid, PlayersMemory memory) {
    semaphoreOperation(memory.sem, SEMAPHORE_DROP);
    if (playersCounter > 0) {
        for (int i = 0; i < playersCounter; i++) {
            if (memory.players[i].pid == pid) {
                printf("Player %s left the game", memory.players[i].name);
                memory.players[i].state = DISCONNECTED;
                playersCounter--;
                break;
            }
        }
    } else {
        printf("No players in game!");
    }
    semaphoreOperation(memory.sem, SEMAPHORE_RAISE);
}

void sendMessageToAll(Message message, PlayersMemory memory) {
    message.type = CHAT_SERVER_TO_CLIENT;
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        semaphoreOperation(memory.sem, SEMAPHORE_DROP);
        if (memory.players[i].state != DISCONNECTED) {
            printf("q %d t %s", memory.players[i].queueId, message.content);
            msgsnd(memory.players[i].queueId, &message, MESSAGE_CONTENT_SIZE, 0);
        }
        semaphoreOperation(memory.sem, SEMAPHORE_RAISE);
    }
}

void maintainPlayersLifecycle(char *serverPid, PlayersMemory playersMem, Lobby lobby) {
    printf("server is running... \n");
    int mainQueue = msgget(SERVER_QUEUE_KEY, IPC_CREAT | 0777);
    if (mainQueue == -1) {
        perror("error while creating queue");
        return;
    }
    InitialMessage initialMessage;
    playersMem.players = shmat(playersMem.memKey, 0, 0);
    ssize_t result;
    int maintainProcess = fork();
    if (maintainProcess == 0) {
        Message internalChatMessage;
        int internalChatQueue = msgget(SERVER_INTERNAL_QUEUE_KEY, IPC_CREAT | 0777);
        int chatProcess = fork();
        if (chatProcess == 0) {
            while (true) {
                msgrcv(internalChatQueue, &internalChatMessage, MESSAGE_CONTENT_SIZE, CHAT_CLIENT_TO_SERVER, 0);
                sendMessageToAll(internalChatMessage, playersMem);
            }
        } else {
            while (true) {
                result = msgrcv(mainQueue, &initialMessage, MAX_PID_SIZE + USER_NAME_LENGTH, 1, 0);
                if (result == -1) {
                    perror("error while connecting client");
                    sleep(1);
                    continue;
                } else {
                    int clientPid = atoi(initialMessage.pid);
                    int clientServerQueue = msgget(clientPid, IPC_CREAT | 0777); //tworzy kolejkę dla uzytkownika
                    int clientProcess = fork();
                    if (clientProcess == 0) {

                        initialMessage.type = 2;
                        strcpy(initialMessage.pid, serverPid);
                        result = msgsnd(clientServerQueue, &initialMessage, MAX_PID_SIZE, 0);
                        if (result == -1) {
                            perror("error while sending message to client");
                            continue;
                        } else {
                            char *user = initialMessage.userName;
                            if (fork() == 0) {
                                Message msg;
                                while (true) {
                                    int res = msgrcv(clientServerQueue, &msg, MESSAGE_CONTENT_SIZE,
                                                     CHAT_CLIENT_TO_SERVER, 0);
                                    if (res == -1) {
                                        printf("eeee");
                                        msgctl(clientServerQueue, IPC_RMID, 0);
                                        return;
                                    } else {
                                        printf("message from %s : %s \n", user, msg.content);
                                        msgsnd(internalChatQueue, &msg, MESSAGE_CONTENT_SIZE, 0);
                                    }
                                }
                            } else {
                                while (true) {
                                    int isAlive = kill(clientPid, 0);
                                    if (isAlive == -1) {
                                        printf("user %s has disconnected\n", user);
                                        removePlayer(clientPid, playersMem);
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
                        addPlayer(clientPid, clientServerQueue, initialMessage.userName, playersMem);
                        printAllAvailablePlayers(playersMem);
                    }
                }


            }

        }
    } else {
//        wait(0);
        bool canContinue = true;
        char command[10];
        do {
            printf("pass a command");
            scanf("%s", command);
            if (strcmp(command, "end") == 0) {
                canContinue = false;
            } else if (strcmp(command, "players") == 0) {
                printAllAvailablePlayers(playersMem);
            }
        } while (canContinue);
        msgctl(mainQueue, IPC_RMID, 0);
    }
    clearPlayersMemory(playersMem);
}

int main(int argc, char const *argv[]) {
    char serverPid[20];
    sprintf(serverPid, "%d", getpid());
    PlayersMemory players = preparePlayersMemory();
    Lobby lobby = prepareLobby();
    initializeGlobals(players);
    maintainPlayersLifecycle(serverPid, players, lobby);
    return 0;
}
