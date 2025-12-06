#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>

char words[15][9];
int numWords = 0;
int numClients = 0;
pthread_mutex_t mutex;

struct serverMessage {
    short message_flag;
    short word_length;
    int num_incorrect;
    char data[256];
};

struct clientMessage {
    int message_length;
    char data[2];
};

void sendMessage(int server_fd, int message_flag, int word_length, int num_incorrect, const char *data) {
    struct serverMessage server_msg;
    memset(&server_msg, 0, sizeof(server_msg));
    server_msg.message_flag = message_flag;
    server_msg.word_length = word_length;
    server_msg.num_incorrect = num_incorrect;
    strcpy(server_msg.data, data);
    send(server_fd, &server_msg, sizeof(server_msg), 0);
};

void loadWords(char words[][9], int* numWords) {
    *numWords = 0;
    char l[9];
    FILE *file = fopen("hangman_words.txt", "r");
    if (!file) {
        exit(EXIT_FAILURE);
    }
    while (fgets(l, sizeof(l), file) && *numWords < 15) {
        l[strcspn(l, "\n")] = '\0'; 
        l[strcspn(l, "\r")] = '\0';
        //printf("Loaded word: %s", l);
        //printf("\n");
        strcpy(words[*numWords], l);
        (*numWords)++;
    }
    fclose(file);
}

void *newClient(void* client_socket) {
    int new_socket = *(int*)client_socket;
    int in;
    struct serverMessage server_msg;
    struct clientMessage client_msg;
    if (recv(new_socket, &client_msg, sizeof(client_msg), 0) <= 0) {
        printf("Client did not send start signal or closed the connection.\n");
        pthread_mutex_lock(&mutex);
        numClients--;
        pthread_mutex_unlock(&mutex);
        close(new_socket);
        free(client_socket);
        pthread_exit(NULL);
    }

    if(client_msg.message_length == 0) {
        in = rand() % numWords;
        server_msg.message_flag = 0;
        server_msg.word_length = strlen(words[in]);
        server_msg.num_incorrect = 0;
        memset(server_msg.data, 0, sizeof(server_msg.data));
        for (int i = 0; i < server_msg.word_length; i++) {
            server_msg.data[i] = '_';
        }
        send(new_socket, &server_msg, sizeof(server_msg), 0);
    }
    int valid = 1;
    while(valid) {
        if (recv(new_socket, &client_msg, sizeof(client_msg), 0) <= 0) {
            break;
        }
        if (client_msg.message_length == 1) {
            char g = client_msg.data[0];
            int inWord = 0;
            for(int i = 0; i < server_msg.word_length; i++) {
                if(words[in][i] == g) {
                    inWord = 1;
                    server_msg.data[i] = g;
                }
            }
            if(!inWord) {
                server_msg.num_incorrect++;
                server_msg.data[server_msg.word_length + server_msg.num_incorrect - 1] = g;
            }
            if(server_msg.num_incorrect >= 6) {
                memset(&server_msg.data, 0, sizeof(server_msg.data));
                strcpy(server_msg.data, "The word was ");
                strcat(server_msg.data, words[in]);
                strcat(server_msg.data, "\n");
                strcat(server_msg.data, ">>>You Lose!\n>>>Game Over!\n");
                server_msg.message_flag = strlen(server_msg.data);
                server_msg.data[sizeof(server_msg.data) - 1] = '\0';
                server_msg.word_length = 0;
                server_msg.num_incorrect = 0;
                valid = 0;
                send(new_socket, &server_msg, sizeof(server_msg), 0);
                break;
            }
            int correct = 1;
            for(int i = 0; i < server_msg.word_length; i++) {
                if(server_msg.data[i] == '_') {
                    correct = 0;
                    break;
                }
            }
            if(correct) {
                memset(&server_msg.data, 0, sizeof(server_msg.data));
                strcpy(server_msg.data, "The word was ");
                strcat(server_msg.data, words[in]);
                strcat(server_msg.data, "\n");
                strcat(server_msg.data, ">>>You Win!\n>>>Game Over!\n");
                server_msg.message_flag = strlen(server_msg.data);
                server_msg.data[sizeof(server_msg.data) - 1] = '\0';
                server_msg.word_length = 0;
                server_msg.num_incorrect = 0;
                valid = 0;
                send(new_socket, &server_msg, sizeof(server_msg), 0);
                break;
            }
            server_msg.message_flag = 0;
            send(new_socket, &server_msg, sizeof(server_msg), 0);
        } else {
            pthread_mutex_lock(&mutex);
            numClients--;
            pthread_mutex_unlock(&mutex);
            close(new_socket);
            free(client_socket);
            pthread_exit(NULL);
        }
    }
    pthread_mutex_lock(&mutex);
    numClients--;
    pthread_mutex_unlock(&mutex);
    close(new_socket);
    free(client_socket);
    pthread_exit(NULL);

}

int main(int argc, char* argv[]) {
    if(argc != 2) return -1;
    int port = atoi(argv[1]);
    pthread_mutex_init(&mutex, NULL);

    loadWords(words, &numWords);
    int server_fd, curr_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        exit(EXIT_FAILURE);
    }

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        exit(EXIT_FAILURE);
    }
    while(1) {
        if ((curr_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            exit(EXIT_FAILURE);
        }
        loadWords(words, &numWords);
        pthread_mutex_lock(&mutex);
        if (numClients >= 3) {
            const char *overload_msg = "server-overloaded";
            sendMessage(curr_socket, strlen(overload_msg), 0, 0, overload_msg);
            close(curr_socket);
        } else {
            sendMessage(curr_socket, 0, 0, 0, "");
            numClients++;
            int *new_socket = malloc(sizeof(int));
            *new_socket = curr_socket;
            pthread_t tid;
            if (pthread_create(&tid, NULL, newClient, (void*)new_socket) < 0) {
                free(new_socket);
                continue;
            }
        }
        pthread_mutex_unlock(&mutex);
    }
    pthread_mutex_destroy(&mutex);
    close(server_fd);
    return 0;

}