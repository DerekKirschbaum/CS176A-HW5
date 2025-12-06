#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>
#include <stdint.h>

#define MAX_WORDS 15
#define MAX_WORD_LEN 8

char words[MAX_WORDS][MAX_WORD_LEN + 1];
int numWords = 0;
int numClients = 0;
pthread_mutex_t mutex;

int recv_loop(int fd, void* buf, int len) {
    int total = 0;
    char* p = buf;
    while (total < len) {
        int n = recv(fd, p + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

void loadWords() {
    numWords = 0;
    FILE *file = fopen("hangman_words.txt", "r");
    if (!file) {
        exit(EXIT_FAILURE);
    }
    char line[MAX_WORD_LEN + 2];
    while (fgets(line, sizeof(line), file) && numWords < MAX_WORDS) {
        line[strcspn(line, "\r\n")] = '\0';
        int len = strlen(line);
        if (len >= 3 && len <= MAX_WORD_LEN) {
            strcpy(words[numWords], line);
            numWords++;
        }
    }
    fclose(file);
}

void sendMessage(int fd, const char *msg) {
    uint8_t len = (uint8_t)strlen(msg);
    send(fd, &len, 1, 0);
    if (len > 0) {
        send(fd, msg, len, 0);
    }
}

void sendMessageHeader(int fd, char *progress, char *incorrect, uint8_t word_length, uint8_t num_incorrect) {
    uint8_t header[3];
    header[0] = 0;
    header[1] = word_length;
    header[2] = num_incorrect;
    send(fd, header, 3, 0);

    int total = word_length + num_incorrect;
    char data[255];
    memcpy(data, progress, word_length);
    if (num_incorrect > 0) {
        memcpy(data + word_length, incorrect, num_incorrect);
    }
    send(fd, data, total, 0);
}

void *newClient(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);

    uint8_t len;
    if (recv_loop(client_fd, &len, 1) < 0) {
        close(client_fd);
        pthread_mutex_lock(&mutex);
        numClients--;
        pthread_mutex_unlock(&mutex);
        return NULL;
    }

    if (len != 0) {
        close(client_fd);
        pthread_mutex_lock(&mutex);
        numClients--;
        pthread_mutex_unlock(&mutex);
        return NULL;
    }

    int in = rand() % numWords;
    char word[MAX_WORD_LEN + 1];
    strcpy(word, words[in]);
    uint8_t word_length = (uint8_t)strlen(word);

    char progress[MAX_WORD_LEN + 1];
    for (int i = 0; i < word_length; i++) {
        progress[i] = '_';
    }
    progress[word_length] = '\0';

    char incorrect[6];
    uint8_t num_incorrect = 0;

    sendMessageHeader(client_fd, progress, incorrect, word_length, num_incorrect);

    int valid = 1;
    while (valid) {
        if (recv_loop(client_fd, &len, 1) < 0) {
            break;
        }
        if (len == 0) {
            continue;
        }
        if (len != 1) {
            break;
        }

        uint8_t g;
        if (recv_loop(client_fd, &g, 1) < 0) {
            break;
        }
        char guess = (char)g;

        int inWord = 0;
        for (int i = 0; i < word_length; i++) {
            if (word[i] == guess) {
                progress[i] = guess;
                inWord = 1;
            }
        }

        if (!inWord && num_incorrect < 6) {
            incorrect[num_incorrect] = guess;
            num_incorrect++;
        }

        int correctWord = 1;
        for (int i = 0; i < word_length; i++) {
            if (progress[i] == '_') {
                correctWord = 0;
                break;
            }
        }

        if (correctWord) {
            char buf[64];
            int pos = snprintf(buf, sizeof(buf), "The word was ");
            for (int i = 0; i < word_length; i++) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%c%s", word[i], (i == word_length - 1) ? "" : " ");
            }
            sendMessage(client_fd, buf);
            sendMessage(client_fd, "You Win!");
            sendMessage(client_fd, "Game Over!");
            break;
        }

        if (num_incorrect >= 6) {
            char buf[64];
            int pos = snprintf(buf, sizeof(buf), "The word was ");
            for (int i = 0; i < word_length; i++) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%c%s", word[i], (i == word_length - 1) ? "" : " ");
            }
            sendMessage(client_fd, buf);
            sendMessage(client_fd, "You Lose!");
            sendMessage(client_fd, "Game Over!");
            break;
        }
        sendMessageHeader(client_fd, progress, incorrect, word_length, num_incorrect);
    }
    close(client_fd);
    pthread_mutex_lock(&mutex);
    numClients--;
    pthread_mutex_unlock(&mutex);
    return NULL;
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        return -1;
    }

    int port = atoi(argv[1]);
    srand((unsigned int)time(NULL));

    pthread_mutex_init(&mutex, NULL);
    loadWords();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        exit(EXIT_FAILURE);
    }

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            continue;
        }

        loadWords();

        pthread_mutex_lock(&mutex);
        if (numClients >= 3) {
            sendMessage(client_fd, "server-overloaded");
            close(client_fd);
        } else {
            numClients++;
            int *pfd = malloc(sizeof(int));
            if (!pfd) {
                close(client_fd);
                numClients--;
            } else {
                *pfd = client_fd;
                pthread_t tid;
                if (pthread_create(&tid, NULL, newClient, pfd) != 0) {
                    close(client_fd);
                    free(pfd);
                    numClients--;
                } else {
                    pthread_detach(tid);
                }
            }
        }
        pthread_mutex_unlock(&mutex);
    }

    close(server_fd);
    pthread_mutex_destroy(&mutex);
    return 0;
}
