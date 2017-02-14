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
        for (int y = 0; y < 2; y++) {
            lobby.rooms[i].players[y].pid = -1;
            lobby.rooms[i].players[y].state = PLAYER_DISCONNECTED;
            lobby.rooms[i].players[y].queueId = -1;
        }
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
            printf("Player %s was successfully removed from the players list\n", player->name);
            msgctl(player->queueId, IPC_RMID, 0);
            player->pid = -1;
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
                found = ROOM_PLAYER_AWAITING;
                player->state = PLAYER_AWAITING_FOR_PARTNER;
                lobby->rooms[roomId].state = ROOM_PLAYER_AWAITING;
                lobby->rooms[roomId].players[0] = *player;
            } else if (roomState == ROOM_PLAYER_AWAITING) {
                found = ROOM_IN_GAME;
                player->state = PLAYER_IN_GAME;
                Player *otherPlayer = NULL;
                int otherPlayerIndex = getPlayerIndexById(
                        playersMemory->players, lobby->rooms[roomId].players[0].pid);
                if (otherPlayerIndex >= 0) {
                    otherPlayer = &playersMemory->players[otherPlayerIndex];
                    otherPlayer->state = PLAYER_IN_GAME;
                    lobby->rooms[roomId].players[0] = *otherPlayer;
                } else {
                    lobby->rooms[roomId].players[0].state = PLAYER_IN_GAME;
                }
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
        msgsnd(queueId, &gameMessage, GAME_MESSAGE_SIZE, 0);
    } else {
        strcpy(gameMessage.command, "2");
        int firstPlayerQueue = lobby->rooms[roomId].players[0].queueId;
        msgsnd(firstPlayerQueue, &gameMessage, GAME_MESSAGE_SIZE, 0);
        int secondPlayerQueue = lobby->rooms[roomId].players[1].queueId;
        msgsnd(secondPlayerQueue, &gameMessage, GAME_MESSAGE_SIZE, 0);

    }
    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
}

void finishAndSendResult(int roomId, Lobby *lobby, PlayersMemory *playersMemory, GameMessage *gameMessage,
                         int currentIndex, int winnerPid) {
    printf("looking for a player...\n");
    int playerIndex = getPlayerIndexById(
            playersMemory->players, lobby->rooms[roomId].players[currentIndex].pid);

    if (playerIndex >= 0) {
        printf("player found!\n");
        Player *player = &playersMemory->players[playerIndex];
        if (player->pid == -winnerPid && winnerPid < 0) {
            printf("%splayer %d has left %s\n", ANSI_COLOR_GREEN, player->pid, ANSI_COLOR_RESET);
            playersMemory->players[playerIndex].state = PLAYER_DISCONNECTED;
            return;
        }

        if (player->state == PLAYER_IN_GAME) {
            printf("player %d winner %d\n", player->pid, winnerPid);
            if (winnerPid != 0 && (player->pid == winnerPid ||
                                   (winnerPid < 0 && player->pid != -winnerPid))) {
                strcpy(gameMessage->command, "You won!\n");
                if (winnerPid < 0) {
                    strcat(gameMessage->command, "Second Player has left the game.\n");
                }
            } else if (winnerPid == 0){
                strcpy(gameMessage->command, "Draw!\n");
            } else {
                strcpy(gameMessage->command, "You lost!\n");
            }
            printf("%sSending final message %s to %d%s\n", ANSI_COLOR_YELLOW, gameMessage->command, player->pid, ANSI_COLOR_RESET);
            msgsnd(player->queueId, gameMessage, GAME_MESSAGE_SIZE, 0);
            playersMemory->players[playerIndex].state = PLAYER_AWAITING_FOR_ROOM;
            lobby->rooms[roomId].players[currentIndex].state = PLAYER_AWAITING_FOR_ROOM;
        }
    } else {
        printf("player pid %d not found!\n", lobby->rooms[roomId].players[currentIndex].pid);
    }
}

// if winner id < 0 -> player disconnected so that who left has won.
// if 0 - draw,
// if winner id > 0 -> it is winner pid
void finishGame(int roomId, Lobby *lobby, PlayersMemory *playersMemory, int winnerPid) {
    printf("Game in lobby %d finished - result : %d\n", roomId, winnerPid);
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

void getFullLobbyState(Lobby *lobby) {
    semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
    for (int i = 0; i < LOBBY_SIZE; i++) {
        printf("ROOM %d  roomState %d | pl1 pid %d state %d | pl2 pid %d state %d\n",
               i, lobby->rooms[i].state,
               lobby->rooms[i].players[0].pid, lobby->rooms[i].players[0].state,
               lobby->rooms[i].players[1].pid, lobby->rooms[i].players[1].state);
    }
    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
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
            result = msgsnd(memory->players[i].queueId, message, CHAT_MESSAGE_SIZE, IPC_NOWAIT);
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

    msgsnd(clientServerQueue, gameMessage, GAME_MESSAGE_SIZE, 0);
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
            } else if (row < GAME_MATRIX_SIZE - 3 && column < GAME_MATRIX_SIZE - 3 &&
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

char getPlayerSign(int playerIndex) {
    return (char) (playerIndex == 0 ? GAME_PLAYER_0_SIGN : GAME_PLAYER_1_SIGN);
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
    msgsnd(currentPlayer->queueId, gameMessage, GAME_MESSAGE_SIZE, 0);
    //yeah i know, nice place to make a method <3

    //let second player know that he has to make a move
    currentPlayerIndex = (currentPlayerIndex + 1) % 2;
    semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
    currentPlayer = &lobby->rooms[roomId].players[currentPlayerIndex];
    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);

    sprintf(gameMessage->command, "%d", GAME_YOUR_TOUR);
    msgsnd(currentPlayer->queueId, gameMessage, GAME_MESSAGE_SIZE, 0);
    int result;
    while (!finish) {
        result = msgrcv(currentPlayer->queueId, gameMessage, GAME_MESSAGE_SIZE, GAME_CLIENT_TO_SERVER, 0);
        if (result == -1) {
            perror("?");
            winnerId = - currentPlayer->pid;
            break;
        } else {
            int otherPlayerIndex = (currentPlayerIndex +1 ) % 2;
            semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
            int otherPlayerPid = lobby->rooms[roomId].players[otherPlayerIndex].pid;
            semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
            if (kill(otherPlayerPid, 0) == -1) {
                finish = true;
                winnerId = -otherPlayerPid;
                break;
            }
            printf("player %d move %s\n", currentPlayerIndex, gameMessage->command);
        }
        gameMessage->type = GAME_SERVER_TO_CLIENT;
        if (isdigit(gameMessage->command[0])) {
            int selectedColumn = atoi(gameMessage->command);
            if (isMovePossible(gameState[selectedColumn], selectedColumn)) {
                gameState[selectedColumn]++;
                semaphoreOperation(matrix.sem, SEMAPHORE_DROP);
                currentPlayerSign = getPlayerSign(currentPlayerIndex);
                matrix.matrix[gameState[selectedColumn] * GAME_MATRIX_SIZE + selectedColumn] = currentPlayerSign;
                semaphoreOperation(matrix.sem, SEMAPHORE_RAISE);

                if (didPlayerWin(&matrix, currentPlayerSign)) {
                    winnerId = currentPlayer->pid;
                    finish = true;
                } else if (isDraw(gameState)) {
                    finish = true;
                    break;
                }
                semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
                if (kill(lobby->rooms[roomId].players[0].pid, 0) == -1) {
                    finish = true;
                    winnerId = -lobby->rooms[roomId].players[0].pid;
                    printf("game over\n");
                } else if (kill(lobby->rooms[roomId].players[1].pid, 0) == -1) {
                    finish = true;
                    winnerId = -lobby->rooms[roomId].players[1].pid;
                    printf("game over\n");
                }
                semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
                printf("inside and will send a message\n");
                if (!finish) {
                sprintf(gameMessage->command, "%d", GAME_MOVE_ACCEPTED);
                    printf("%ssending message %s to player %d(%s) %s\n", ANSI_COLOR_MAGENTA,
                           gameMessage->command, currentPlayer->pid, currentPlayer->name, ANSI_COLOR_RESET);
                msgsnd(currentPlayer->queueId, gameMessage, GAME_MESSAGE_SIZE, 0);
                currentPlayerIndex = (currentPlayerIndex + 1) % 2;

                semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
                currentPlayer = &lobby->rooms[roomId].players[currentPlayerIndex];
                semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
                sprintf(gameMessage->command, "%d", GAME_YOUR_TOUR);
                    printf("%ssending message %s to player %s %s\n",
                           ANSI_COLOR_MAGENTA, gameMessage->command, currentPlayer->name, ANSI_COLOR_RESET);
                msgsnd(currentPlayer->queueId, gameMessage, GAME_MESSAGE_SIZE, 0);
                }
            } else {
                sprintf(gameMessage->command, "%d", GAME_MOVE_REJECTED);
                msgsnd(currentPlayer->queueId, gameMessage, GAME_MESSAGE_SIZE, 0);
            }
        } else {
            sprintf(gameMessage->command, "%d", GAME_MOVE_REJECTED);
            msgsnd(currentPlayer->queueId, gameMessage, GAME_MESSAGE_SIZE, 0);
        }
    }

    shmctl(matrix.memKey, IPC_RMID, 0);
    semctl(matrix.sem, IPC_RMID, 0);
    printf("game normally finished\n");
    finishGame(roomId, lobby, playersMemory, winnerId);
}

int createMainQueue() {
    return msgget(SERVER_QUEUE_KEY, IPC_CREAT | 0777);
}

void lobbyChecker(PlayersMemory * playersMemory, Lobby * lobby) {
    while (true) {
        for (int roomId = 0; roomId < LOBBY_SIZE; roomId++) {
            semaphoreOperation(playersMemory->sem, SEMAPHORE_DROP);
            semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
            if (lobby->rooms[roomId].players[0].state == PLAYER_IN_GAME &&
                lobby->rooms[roomId].players[1].state == PLAYER_DISCONNECTED) {
                semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
                semaphoreOperation(playersMemory->sem, SEMAPHORE_RAISE);
                sleep(3);
                semaphoreOperation(playersMemory->sem, SEMAPHORE_DROP);
                semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
                if (lobby->rooms[roomId].players[0].state == PLAYER_IN_GAME &&
                    lobby->rooms[roomId].players[1].state == PLAYER_DISCONNECTED) {
                    semaphoreOperation(playersMemory->sem, SEMAPHORE_RAISE);
                    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
                    printf("%sINDICATED PLAYER %d DEADLOCK%s\n", ANSI_COLOR_RED, lobby->rooms[roomId].players[0].pid, ANSI_COLOR_RESET);
                    finishGame(roomId, lobby, playersMemory, -lobby->rooms[roomId].players[1].pid);
                } else {
                    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
                    semaphoreOperation(playersMemory->sem, SEMAPHORE_RAISE);
                }

            } else if (lobby->rooms[roomId].players[1].state == PLAYER_IN_GAME &&
                       lobby->rooms[roomId].players[0].state == PLAYER_DISCONNECTED) {
                semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
                semaphoreOperation(playersMemory->sem, SEMAPHORE_RAISE);
                sleep(3);
                semaphoreOperation(playersMemory->sem, SEMAPHORE_DROP);
                semaphoreOperation(lobby->sem, SEMAPHORE_DROP);
                if (lobby->rooms[roomId].players[1].state == PLAYER_IN_GAME &&
                    lobby->rooms[roomId].players[0].state == PLAYER_DISCONNECTED) {
                    semaphoreOperation(playersMemory->sem, SEMAPHORE_RAISE);
                    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
                    printf("%sINDICATED PLAYER %d DEADLOCK%s", ANSI_COLOR_RED, lobby->rooms[roomId].players[1].pid, ANSI_COLOR_RESET);
                    finishGame(roomId, lobby, playersMemory, -lobby->rooms[roomId].players[0].pid);
                } else {
                    semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
                    semaphoreOperation(playersMemory->sem, SEMAPHORE_RAISE);
                }

            } else {
                semaphoreOperation(lobby->sem, SEMAPHORE_RAISE);
                semaphoreOperation(playersMemory->sem, SEMAPHORE_RAISE);
            }
        }
        sleep(5);
    }

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
                printf("listen for a chat message...\n");
                result = msgrcv(internalChatQueue, &internalChatMessage, INITIAL_MESSAGE_SIZE,
                       CHAT_CLIENT_TO_SERVER, 0);
                if (result == -1) {
                    perror("internal queue error:");
                    msgctl(internalChatQueue, IPC_RMID, 0);
                    exit(0);
                } else {
                    printf("sending message to all\n");
                    sendMessageToAll(&internalChatMessage, &playersMem);
                }
            }

        } if (fork() == 0) {
            lobbyChecker(&playersMem, &lobby);
        } else {
            while (true) {
                result = msgrcv(mainQueue, &initialMessage, INITIAL_MESSAGE_SIZE, GAME_CLIENT_TO_SERVER, 0);
                if (result == -1) {
                    perror("error while connecting client");
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
                        result = msgsnd(clientServerQueue, &initialMessage, INITIAL_MESSAGE_SIZE, 0);
                        printf("sent to user %d server pid %d\n", clientPid, serverPid);
                        if (result == -1) {
                            perror("error while sending message to client");
                            continue;
                        } else {
                            char *user = initialMessage.userName;
                            if (fork() == 0) {
                                ChatMessage chatMessage;
                                while (true) {
                                    printf("user quque %d\n", clientServerQueue);
                                    int res = msgrcv(clientServerQueue, &chatMessage, CHAT_MESSAGE_SIZE, CHAT_CLIENT_TO_SERVER, 0);
                                    if (res == -1 || kill(clientPid, 0) == -1) {
                                        perror("An error occured while trying to receive chat message");
                                        sleep(1);
                                        msgctl(clientServerQueue, IPC_RMID, 0);
                                        return;
                                    } else {
                                        printf("message came\n");
                                        chatMessage.type = CHAT_CLIENT_TO_SERVER;
                                        msgsnd(internalChatQueue, &chatMessage, CHAT_MESSAGE_SIZE, 0);
                                    }
                                }
                            } else if (fork() == 0) {
                                while (true) {
                                    GameMessage gameMessage;
                                    prepareLobbyInitialMessage(&lobby, &gameMessage);
                                    msgsnd(clientServerQueue, &gameMessage, GAME_MESSAGE_SIZE, 0);
                                    bool playerWasAddedToRoom = false;
                                    do {
                                        result = msgrcv(clientServerQueue, &gameMessage, GAME_MESSAGE_SIZE,
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
                                                if (roomState == ROOM_IN_GAME) {
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
                                    result = msgrcv(clientServerQueue, &gameMessage, GAME_MESSAGE_SIZE, GAME_WANT_TO_CONTINUE, 0);
                                    if (result == -1) {
                                        printf("%sPlayer %s left the game %s\n", ANSI_COLOR_BLUE, user, ANSI_COLOR_RESET);
                                        exit(0);
                                    } else {
                                        printf("%sPlayer %s will be moved to the lobby%s\n", ANSI_COLOR_BLUE, user, ANSI_COLOR_RESET);
                                    }
                                }
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
            } else if (strcmp(command, "flobby") == 0) {
                getFullLobbyState(&lobby);
            }
        } while (canContinue);
        msgctl(mainQueue, IPC_RMID, 0);
    }
    clearMemory(&playersMem, &lobby);
}

int main(int argc, char const *argv[]) {
    int serverPid = getpid();
    printf("%d %d %d\n", GAME_MESSAGE_SIZE, CHAT_MESSAGE_SIZE, INITIAL_MESSAGE_SIZE);
    PlayersMemory players = preparePlayersMemory();
    Lobby lobby = prepareLobby();
    initializeSemaphore();
    maintainPlayersLifecycle(serverPid, players, lobby);
    return 0;
}
