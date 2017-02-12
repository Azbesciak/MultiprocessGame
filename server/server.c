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
#include <ctype.h>
#include "../libs/structures.h"


struct sembuf semaphore;

void semaphoreOperation(int semId, short todo) {
    semaphore.sem_op = todo;
    semop(semId, &semaphore, 1);
}

PlayersMemory preparePlayersMemory() {
    PlayersMemory playersMemory;
    playersMemory.memKey = shmget(PLAYERS_STRUCTURE_KEY, sizeof(Player) * MAX_PLAYER_AMOUNT, IPC_CREAT | 0777);
    playersMemory.players = shmat(playersMemory.memKey, 0, 0);
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        Player player;
        player.state = PLAYER_DISCONNECTED;
        playersMemory.players[i] = player;
    }
    playersMemory.sem = semget(PLAYERS_SEMAPHORE_KEY, 1, IPC_CREAT | 0700);
    semctl(playersMemory.sem, 0, SETVAL, 1);
    return playersMemory;
}

Lobby prepareLobby() {
    Lobby lobby;
    lobby.memKey = shmget(LOBBY_STRUCTURE_KEY, sizeof(Room) * LOBBY_SIZE, IPC_CREAT | 0777);
    lobby.rooms = shmat(lobby.memKey, 0, 0);
    for (int i = 0; i < LOBBY_SIZE; i++) {
        Room room;
        room.state = ROOM_EMPTY;
        lobby.rooms[i] = room;
    }
    lobby.sem = semget(LOBBY_SEMAPHORE_KEY, 1, IPC_CREAT | 0777);
    semctl(lobby.sem, 0, SETVAL, 1);
    return lobby;
}

void finishGame(int roomId, Lobby *lobby, PlayersMemory *playersMemory, int winnerPid);

int getPlayerIndexById(Player *players, int playerId) {
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        if (players[i].pid == playerId && players[i].state != PLAYER_DISCONNECTED) {
            return i;
        }
    }
    return -1;
}

void removePlayerFromLobby(Lobby *lobby, PlayersMemory *playersMemory, int playerId) {
    Player *player = NULL;
    int roomId = 0;
    semaphoreOperation(playersMemory->sem, SEMAPHORE_DROP);
    semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
    while (roomId < LOBBY_SIZE) {
        if (lobby->rooms[roomId].players[0].pid == playerId) {
            player = &lobby->rooms[roomId].players[0];
        } else if (lobby->rooms[roomId].players[1].pid == playerId) {
            player = &lobby->rooms[roomId].players[1];
        }
        if (player != NULL) {
            if (player->state != PLAYER_DISCONNECTED) {
                player->state = PLAYER_DISCONNECTED;
            }
            lobby->rooms[roomId].state = ROOM_EMPTY;
            break;
        } else {
            roomId++;
        }
    }
    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
    semaphoreOperation(playersMemory->sem, SEMAPHORE_RAISE);
    finishGame(roomId, lobby, playersMemory, -playerId);
}

void removePlayer(int pid, PlayersMemory *memory, Lobby *lobby) {
    semaphoreOperation(memory->sem, SEMAPHORE_DROP);
    semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
    bool wasFound = false;
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        if (memory->players[i].pid == pid) {
            Player *player = &memory->players[i];
            printf("Player %s left the game\n", player->name);
            msgctl(player->queueId, IPC_RMID, 0);
            memory->players[i].state = PLAYER_DISCONNECTED;
            wasFound = true;
            break;
        }
    }
    if (!wasFound) {
        printf("Player not found!\n");
    }

    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
    semaphoreOperation(memory->sem, SEMAPHORE_RAISE);
}

short addPlayerToRoom(int roomId, int playerId, PlayersMemory *playersMemory, Lobby *lobby) {
    short found = 0;
    semaphoreOperation(playersMemory->sem, SEMAPHORE_DROP);
    semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
    Player *player = NULL;
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        if (playersMemory->players[i].pid == playerId) {
            if (playersMemory->players[i].state == PLAYER_AWAITING_FOR_ROOM) {
                playersMemory->players[i].state = PLAYER_AWAITING_FOR_PARTNER;
                player = &(playersMemory->players[i]);
                found = 1;
            }
            break;
        }
    }
    if (found == 0) {
        printf("Couldn't find user with such a pid!\n");
    } else {
        found = 0;
        int roomState = lobby->rooms[roomId].state;
        if (roomState == ROOM_IN_GAME) {
            printf("Player selected not available room!\n");
        } else {
            if (roomState == ROOM_EMPTY) {
                found = 1;
                player->state = PLAYER_AWAITING_FOR_PARTNER;
                lobby->rooms[roomId].state = ROOM_PLAYER_AWAITING;
                lobby->rooms[roomId].players[0] = *player;
            } else if (roomState == ROOM_PLAYER_AWAITING) {
                found = 2;
                player->state = PLAYER_IN_GAME;
                lobby->rooms[roomId].players[0].state = PLAYER_IN_GAME;
                lobby->rooms[roomId].state = ROOM_IN_GAME;
                lobby->rooms[roomId].players[1] = *player;
            }
        }
    }
    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
    semaphoreOperation(playersMemory->sem, SEMAPHORE_RAISE);
    return found;
}

void sendGameStartInfo(short roomState, int roomId, Lobby *lobby) {
    GameMessage gameMessage;
    gameMessage.type = GAME_SERVER_TO_CLIENT;
    semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
    if (roomState == 1) {
        strcpy(gameMessage.command, "1");
        int queueId = lobby->rooms[roomId].players[0].queueId;
        msgsnd(queueId, &gameMessage, MESSAGE_CONTENT_SIZE, 0);
    } else {
        strcpy(gameMessage.command, "2");
        int firstPlayerQueue = lobby->rooms[roomId].players[0].queueId;
        msgsnd(firstPlayerQueue, &gameMessage, MESSAGE_CONTENT_SIZE, 0);
        int secondPlayerQueue = lobby->rooms[roomId].players[1].queueId;
        msgsnd(secondPlayerQueue, &gameMessage, MESSAGE_CONTENT_SIZE, 0);

    }
    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
}

void finishAndSendResult(int roomId, Lobby *lobby, PlayersMemory *playersMemory, GameMessage *gameMessage,
                         int currentIndex, int winnerPid) {
    int playerIndex = getPlayerIndexById(
            playersMemory->players, lobby->rooms[roomId].players[currentIndex].pid);

    if (playerIndex >= 0) {
        Player *player = &playersMemory->players[playerIndex];
        printf("player %d winner %d\n", player->pid, winnerPid);
        if (winnerPid != 0 && (player->pid == winnerPid ||
                (winnerPid < 0 && player->pid != -winnerPid))) {
            strcpy(gameMessage->command, "You won!\n");
            printf("%s\n", gameMessage->command);
            if (winnerPid < 0) {
                strcat(gameMessage->command, "Second Player has left the game.\n");
            }
        } else if (winnerPid == 0){
            strcpy(gameMessage->command, "Draw!\n");
        } else {
            strcpy(gameMessage->command, "You lost!\n");
        }
        msgsnd(player->queueId, gameMessage, MESSAGE_CONTENT_SIZE, 0);
        playersMemory->players[playerIndex].state = PLAYER_AWAITING_FOR_ROOM;
        lobby->rooms[roomId].players[0].state = PLAYER_AWAITING_FOR_ROOM;
    }
}

// if winner id < 0 -> player disconnected so that who left has won.
// if 0 - draw,
// if winner id > 0 -> it is winner pid
void finishGame(int roomId, Lobby *lobby, PlayersMemory *playersMemory, int winnerPid) {
    GameMessage gameMessage;
    gameMessage.type = GAME_SERVER_TO_CLIENT;
    semaphoreOperation(playersMemory->sem, SEMAPHORE_DROP);
    semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
    finishAndSendResult(roomId, lobby, playersMemory, &gameMessage, 0, winnerPid);
    finishAndSendResult(roomId, lobby, playersMemory, &gameMessage, 1, winnerPid);
    lobby->rooms[roomId].state = ROOM_EMPTY;
    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
    semaphoreOperation(playersMemory->sem, SEMAPHORE_RAISE);
}

char *getLobbyState(Lobby *lobby) {
    static char message[MESSAGE_CONTENT_SIZE];
    semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
    int freeRooms = LOBBY_SIZE;
    strcpy(message, "available rooms: \n");
    for (int i = 0; i < LOBBY_SIZE; i++) {
        char roomIndexTemp[3];
        sprintf(roomIndexTemp, "%d", i);
        if (lobby->rooms[i].state == ROOM_EMPTY) {
            strcat(message, roomIndexTemp);
            strcat(message, " - room empty");
            strcat(message, "\n");
        } else if (lobby->rooms[i].state == ROOM_PLAYER_AWAITING) {
            strcat(message, roomIndexTemp);
            strcat(message, " - awaiting : ");
            strcat(message, lobby->rooms[i].players[0].name);
            strcat(message, "\n");
        } else {
            freeRooms--;
        }
    }
    if (freeRooms == 0) {
        strcat(message, "there is no available room!\n");
    } else {
        strcat(message, "choose room id:\n");
    }
    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
    return message;
}

void prepareLobbyInitialMessage(Lobby *lobby, GameMessage *message) {
    message->type = GAME_SERVER_TO_CLIENT;
    strcpy(message->command, getLobbyState(lobby));
}

void clearMemory(PlayersMemory *memory, Lobby *lobby) {
    semctl(memory->sem, IPC_RMID, 0);
    shmctl(memory->memKey, IPC_RMID, 0);
    semctl(lobby->sem, IPC_RMID, 0);
    shmctl(lobby->memKey, IPC_RMID, 0);
}

void initializeSemaphore() {
    semaphore.sem_num = 0;
    semaphore.sem_flg = 0;
}

void printAllAvailablePlayers(PlayersMemory memory) {
    int counter = 0;
    semaphoreOperation(memory.sem, SEMAPHORE_DROP);
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        if (memory.players[i].state != PLAYER_DISCONNECTED) {
            printf("User %s, pid : %d, state: %d \n", memory.players[i].name, memory.players[i].pid,
                   memory.players[i].state);
            counter++;
        }
    }
    semaphoreOperation(memory.sem, SEMAPHORE_RAISE);
    printf("counter: %d\n", counter);
}

void addPlayer(int pid, int userQueue, char *name, PlayersMemory memory) {
    semaphoreOperation(memory.sem, SEMAPHORE_DROP);
    bool wasPlayerAdded = false;
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        printf("%d\n", memory.players[i].state);
        if (memory.players[i].state == PLAYER_DISCONNECTED) {
            Player player;
            player.pid = pid;
            strcpy(player.name, name);
            player.state = PLAYER_AWAITING_FOR_ROOM;
            player.queueId = userQueue;
            memory.players[i] = player;
            wasPlayerAdded = true;
            break;
        }
    }
    if (!wasPlayerAdded) {
        printf("Max player amount reached! Player not created\n");
    } else {
        printf("Player %s successfully added !\n", name);
    }
    semaphoreOperation(memory.sem, SEMAPHORE_RAISE);
}

void sendMessageToAll(ChatMessage * message, PlayersMemory * memory) {
    message->type = CHAT_SERVER_TO_CLIENT;
    printf("going to send message \n");
    int result;
    semaphoreOperation(memory->sem, SEMAPHORE_DROP);
    for (int i = 0; i < MAX_PLAYER_AMOUNT; i++) {
        if (memory->players[i].state != PLAYER_DISCONNECTED) {
            printf("q %d t %s\n", memory->players[i].queueId, message->content);
            result = msgsnd(memory->players[i].queueId, message, MESSAGE_CONTENT_SIZE + USER_NAME_LENGTH, IPC_NOWAIT);
            if (result == -1) {
                perror("error while sending chat message");
            }
        }
    }
    semaphoreOperation(memory->sem, SEMAPHORE_RAISE);
    printf("after sending \n");
}

void wasWrongIdSelected(Lobby *lobby, GameMessage *gameMessage, int clientServerQueue) {
    char message[MESSAGE_CONTENT_SIZE] = "please, pass a valid id\n";
    prepareLobbyInitialMessage(lobby, gameMessage);
    strcat(message, gameMessage->command);
    strcpy(gameMessage->command, message);
    gameMessage->type = GAME_SERVER_TO_CLIENT;

    msgsnd(clientServerQueue, gameMessage, MESSAGE_CONTENT_SIZE, 0);
}

void initializeGameMatrix(GameMatrix * matrix) {
    semaphoreOperation(matrix->sem, SEMAPHORE_DROP);
    char tempMatrix[GAME_MATRIX_SIZE * GAME_MATRIX_SIZE];
    for (int row = 0; row < GAME_MATRIX_SIZE; row++) {
        for (int column = 0; column < GAME_MATRIX_SIZE; column++) {
            tempMatrix[row * GAME_MATRIX_SIZE + column] = (char) 176;
        }
    }
    strcpy(matrix->matrix, tempMatrix);
    semaphoreOperation(matrix->sem, SEMAPHORE_RAISE);
}

bool isMovePossible(int currentRow, int currentColumn) {
    return currentColumn < GAME_MATRIX_SIZE && currentColumn >= 0 && currentRow < GAME_MATRIX_SIZE - 1;
}
bool isDraw(int * gameState) {
    for (int i = 0; i < GAME_MATRIX_SIZE; i++) {
        if (gameState[i] < GAME_MATRIX_SIZE - 1) {
            return false;
        }
    }
    return true;
}

bool didPlayerWin(GameMatrix *matrix, char playerSign) {
    bool didWin = false;
    semaphoreOperation(matrix->sem, SEMAPHORE_DROP);
    for (int row = 0; row < GAME_MATRIX_SIZE; row++) {
        for (int column = 0; column < GAME_MATRIX_SIZE; column++) {
            if (row < GAME_MATRIX_SIZE - 3 &&
                matrix->matrix[row * GAME_MATRIX_SIZE + column] == playerSign &&                  // *
                matrix->matrix[(row + 1) * GAME_MATRIX_SIZE + column] == playerSign &&            // *
                matrix->matrix[(row + 2) * GAME_MATRIX_SIZE + column] == playerSign &&            // *
                matrix->matrix[(row + 3) * GAME_MATRIX_SIZE + column] == playerSign) {            // *
                didWin = true;
                break;
            } else if (column < GAME_MATRIX_SIZE - 3 &&
                       matrix->matrix[row * GAME_MATRIX_SIZE + column] == playerSign &&           // * * * *
                       matrix->matrix[row * GAME_MATRIX_SIZE + column + 1] == playerSign &&       //
                       matrix->matrix[row * GAME_MATRIX_SIZE + column + 2] == playerSign &&       //
                       matrix->matrix[row * GAME_MATRIX_SIZE + column + 3] == playerSign) {       //
                didWin = true;
                break;
            } else if (row < GAME_MATRIX_SIZE - 3 && column < GAME_MATRIX_SIZE - 4 &&
                       matrix->matrix[row * GAME_MATRIX_SIZE + column] == playerSign &&             //       *
                       matrix->matrix[(row + 1) * GAME_MATRIX_SIZE + column + 1] == playerSign &&   //     *
                       matrix->matrix[(row + 2) * GAME_MATRIX_SIZE + column + 2] == playerSign &&   //   *
                       matrix->matrix[(row + 3) * GAME_MATRIX_SIZE + column + 3] == playerSign) {   // *
                didWin = true;
                break;
            } else if (row >= 3 && column < GAME_MATRIX_SIZE - 3 &&                                 // *
                       matrix->matrix[row * GAME_MATRIX_SIZE + column] == playerSign &&             //   *
                       matrix->matrix[(row - 1) * GAME_MATRIX_SIZE + column + 1] == playerSign &&   //     *
                       matrix->matrix[(row - 2) * GAME_MATRIX_SIZE + column + 2] == playerSign &&   //       *
                       matrix->matrix[(row - 3) * GAME_MATRIX_SIZE + column + 3] == playerSign) {
                didWin = true;
                break;
            }
        }
    }
    semaphoreOperation(matrix->sem, SEMAPHORE_RAISE);
    return didWin;
}

void maintainGame(Lobby *lobby, PlayersMemory *playersMemory, GameMessage *gameMessage, int roomId) {
    int winnerId = 0;
    int gameState[GAME_MATRIX_SIZE];
    for (int i = 0; i < GAME_MATRIX_SIZE; i++) {
        gameState[i] = -1;
    }
    GameMatrix matrix;
    matrix.sem = semget(roomId + GAME_ROOM_KEY_ADDER, 1, IPC_CREAT | 0777);
    semctl(matrix.sem, SETVAL, 1);
    matrix.memKey = shmget(roomId + GAME_ROOM_KEY_ADDER,
                           GAME_MATRIX_SIZE * GAME_MATRIX_SIZE + 1, IPC_CREAT | 0777);
    matrix.matrix = shmat(matrix.memKey, 0, 0);
    bool finish = false;
    initializeGameMatrix(&matrix);
    Player *currentPlayer;
    int currentPlayerIndex = 1;
    char currentPlayerSign;

    gameMessage->type = GAME_SERVER_TO_CLIENT;
    //let first player see game matrix
    semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
    currentPlayer = &lobby->rooms[roomId].players[currentPlayerIndex];
    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);

    sprintf(gameMessage->command, "%d", GAME_MOVE_ACCEPTED);
    msgsnd(currentPlayer->queueId, gameMessage, MESSAGE_CONTENT_SIZE, 0);
    //yeah i know, nice place to make a method <3

    //let second player know that he has to make a move
    currentPlayerIndex = (currentPlayerIndex + 1) % 2;
    semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
    currentPlayer = &lobby->rooms[roomId].players[currentPlayerIndex];
    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);

    sprintf(gameMessage->command, "%d", GAME_YOUR_TOUR);
    msgsnd(currentPlayer->queueId, gameMessage, MESSAGE_CONTENT_SIZE, 0);
    int result;
    while (!finish) {
        result = msgrcv(currentPlayer->queueId, gameMessage, MESSAGE_CONTENT_SIZE, GAME_CLIENT_TO_SERVER, 0);
        if (result == -1) {
            perror("?");
            finish = true;
            break;
        } else {
            printf("player %d move %s\n", currentPlayerIndex, gameMessage->command);
        }
        gameMessage->type = GAME_SERVER_TO_CLIENT;
        if (isdigit(gameMessage->command[0])) {
            int selectedColumn = atoi(gameMessage->command);
            if (isMovePossible(gameState[selectedColumn], selectedColumn)) {
                gameState[selectedColumn]++;
                semaphoreOperation(matrix.sem, SEMAPHORE_DROP);
                currentPlayerSign = (char) (currentPlayerIndex == 0 ? GAME_PLAYER_0_SIGN : GAME_PLAYER_1_SIGN);
                matrix.matrix[gameState[selectedColumn] * GAME_MATRIX_SIZE + selectedColumn] = currentPlayerSign;
                semaphoreOperation(matrix.sem, SEMAPHORE_RAISE);

                if (didPlayerWin(&matrix, currentPlayerSign)) {
                    winnerId = currentPlayer->pid;
                    finish = true;
                } else if (isDraw(gameState)) {
                    finish = true;
                    break;
                }
                if (!finish) {
                sprintf(gameMessage->command, "%d", GAME_MOVE_ACCEPTED);
                msgsnd(currentPlayer->queueId, gameMessage, MESSAGE_CONTENT_SIZE, 0);
                currentPlayerIndex = (currentPlayerIndex + 1) % 2;
                semaphoreOperation(lobby->sem, SEMAPHORE_DROP);

                currentPlayer = &lobby->rooms[roomId].players[currentPlayerIndex];
                semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
                sprintf(gameMessage->command, "%d", GAME_YOUR_TOUR);
                msgsnd(currentPlayer->queueId, gameMessage, MESSAGE_CONTENT_SIZE, 0);
                }
            } else {
                sprintf(gameMessage->command, "%d", GAME_MOVE_REJECTED);
                msgsnd(currentPlayer->queueId, gameMessage, MESSAGE_CONTENT_SIZE, 0);
            }
        } else {
            sprintf(gameMessage->command, "%d", GAME_MOVE_REJECTED);
            msgsnd(currentPlayer->queueId, gameMessage, MESSAGE_CONTENT_SIZE, 0);
        }

    }
    shmctl(matrix.memKey, IPC_RMID, 0);
    semctl(matrix.sem, IPC_RMID, 0);
    printf("Game in lobby %d finished \n", roomId);
    finishGame(roomId, lobby, playersMemory, winnerId);

}
int createMainQueue() {
    return msgget(SERVER_QUEUE_KEY, IPC_CREAT | 0777);
}

int restartMainQueue(int existingQueueId) {
    msgctl(existingQueueId, IPC_RMID, 0);
    return createMainQueue();
}

void maintainPlayersLifecycle(int serverPid, PlayersMemory playersMem, Lobby lobby) {
    printf("server is running... pid %d\n", serverPid);
    int mainQueue = createMainQueue();
    if (mainQueue == -1) {
        perror("error while creating queue");
        return;
    }
    InitialMessage initialMessage;
    playersMem.players = shmat(playersMem.memKey, 0, 0);
    ssize_t result;
    int maintainProcess = fork();
    if (maintainProcess == 0) {
        ChatMessage internalChatMessage;
        int internalChatQueue = msgget(SERVER_INTERNAL_QUEUE_KEY, IPC_CREAT | 0777);
        int chatProcess = fork();
        if (chatProcess == 0) {
            while (true) {
                printf("listen for message\n");
                result = msgrcv(internalChatQueue, &internalChatMessage, MESSAGE_CONTENT_SIZE + USER_NAME_LENGTH,
                       CHAT_CLIENT_TO_SERVER, 0);
                if (result == -1) {
                    perror("internal queue error:");
                } else {
                    printf("sending message to all\n");
                    sendMessageToAll(&internalChatMessage, &playersMem);
                }
            }
        } else {
            while (true) {
                result = msgrcv(mainQueue, &initialMessage, MAX_PID_SIZE + USER_NAME_LENGTH, GAME_CLIENT_TO_SERVER, 0);
                if (result == -1) {
                    perror("error while connecting client");
//                    printf("will try to create restart queue\n");
//                    mainQueue = restartMainQueue(mainQueue);
                    sleep(1);
                    continue;
                } else {
                    int clientPid = initialMessage.pid;
                    int clientServerQueue = msgget(clientPid, IPC_CREAT | 0777); //tworzy kolejkę dla uzytkownika
                    int clientProcess = fork();
                    if (clientProcess == 0) {

                        initialMessage.type = GAME_SERVER_TO_CLIENT;
                        initialMessage.pid = serverPid;
                        printf("sending to client pid = %d\n", initialMessage.pid);
                        result = msgsnd(clientServerQueue, &initialMessage, MAX_PID_SIZE + USER_NAME_LENGTH, 0);
                        printf("sent to user %d server pid %d\n", clientPid, serverPid);
                        if (result == -1) {
                            perror("error while sending message to client");
                            continue;
                        } else {
                            char *user = initialMessage.userName;
                            if (fork() == 0) {
                                ChatMessage msg;
                                while (true) {
                                    printf("user quque %d\n", clientServerQueue);
                                    int res = msgrcv(clientServerQueue, &msg, MESSAGE_CONTENT_SIZE + USER_NAME_LENGTH,
                                                     CHAT_CLIENT_TO_SERVER, 0);
                                    if (res == -1 || kill(clientPid, 0) == -1) {
                                        perror("An error occured while trying to receive chat message");
                                        sleep(1);
                                        msgctl(clientServerQueue, IPC_RMID, 0);
                                        return;
                                    } else {
                                        printf("message came\n");
                                        msg.type = CHAT_CLIENT_TO_SERVER;
                                        msgsnd(internalChatQueue, &msg, MESSAGE_CONTENT_SIZE + USER_NAME_LENGTH, 0);
                                    }
                                }
                            } else if (fork() == 0) {
                                GameMessage gameMessage;
                                prepareLobbyInitialMessage(&lobby, &gameMessage);
                                msgsnd(clientServerQueue, &gameMessage, MESSAGE_CONTENT_SIZE, 0);
                                bool playerWasAddedToRoom = false;
                                do {
                                    result = msgrcv(clientServerQueue, &gameMessage, MESSAGE_CONTENT_SIZE,
                                                    GAME_CLIENT_TO_SERVER, 0);
                                    if (result == -1) {
                                        return;
                                    }
                                    printf("command %s\n", gameMessage.command);
                                    if (isdigit(gameMessage.command[0])) {
                                        int roomId = atoi(gameMessage.command);
                                        if (roomId < 0 || roomId >= LOBBY_SIZE) {
                                            printf("WRONG\n");
                                            wasWrongIdSelected(&lobby, &gameMessage, clientServerQueue);
                                        } else {
                                            playerWasAddedToRoom = true;
                                            short roomState = addPlayerToRoom(roomId, clientPid, &playersMem, &lobby);
                                            printf("player %s was added to room %d\n", user, roomId);
                                            sendGameStartInfo(roomState, roomId, &lobby);
                                            if (roomState == 2) {
                                                int gameMaintainProcess = fork();
                                                if (gameMaintainProcess == 0) {
                                                    maintainGame(&lobby, &playersMem, &gameMessage, roomId);
                                                }
                                            }
                                        }
                                    } else {
                                        wasWrongIdSelected(&lobby, &gameMessage, clientServerQueue);
                                    };
                                } while (!playerWasAddedToRoom);
                                printf("client process ended!\n");

                            } else {
                                while (true) {
                                    int isAlive = kill(clientPid, 0);
                                    if (isAlive == -1) {
                                        printf("Player %s has disconnected\n", user);
                                        removePlayerFromLobby(&lobby, &playersMem, clientPid);
                                        removePlayer(clientPid, &playersMem, &lobby);
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
        bool canContinue = true;
        char command[10];
        do {
            printf("pass a command\n");
            scanf("%s", command);
            if (strcmp(command, "end") == 0) {
                canContinue = false;
            } else if (strcmp(command, "players") == 0) {
                printAllAvailablePlayers(playersMem);
            } else if (strcmp(command, "lobby") == 0) {
                char *string = getLobbyState(&lobby);
                printf("%s\n", string);
            }
        } while (canContinue);
        msgctl(mainQueue, IPC_RMID, 0);
    }
    clearMemory(&playersMem, &lobby);
}

int main(int argc, char const *argv[]) {
//    char serverPid[20];
//    sprintf(serverPid, "%d", getpid());
    int serverPid = getpid();
    PlayersMemory players = preparePlayersMemory();
    Lobby lobby = prepareLobby();
    initializeSemaphore();
    maintainPlayersLifecycle(serverPid, players, lobby);
    return 0;
}
