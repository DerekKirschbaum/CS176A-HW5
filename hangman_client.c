#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>

int recv_loop(int fd, void *buf, int len) {
    int total = 0;
    char* p = buf;
    while (total < len) {
        int n = recv(fd, p + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

void sendMessage(int client_fd, int length, char data) {
    if (length == 0) {
        uint8_t len = 0;
        send(client_fd, &len, 1, 0);
    } else {
        uint8_t buf[2];
        buf[0] = 1;
        buf[1] = (uint8_t)data;
        send(client_fd, buf, 2, 0);
    }
}

char waitForGuess(int client_fd) {
    char guess[256];
    char g;
    int valid = 0;
    while(!valid) {
        printf(">>>Letter to guess: ");
        fflush(stdout);

        if (!fgets(guess, sizeof(guess), stdin)) {
            sendMessage(client_fd, 0, '\0');
            printf("\n");
            close(client_fd);
            exit(EXIT_SUCCESS);
        }

        int len = strlen(guess);
        if (len > 0 && guess[len - 1] == '\n') {
            guess[len - 1] = '\0';
            len--;
        }

        if (len != 1 || !isalpha(guess[0])) {
            printf(">>>Error! Please guess one letter.\n");
        } else {
            valid = 1;
            g = tolower(guess[0]);
        }
    }
    return g;
}

void playHangman(int client_fd) {
    while(1) {
        uint8_t message_flag;
        if (recv_loop(client_fd, &message_flag, 1) < 0) break;

        if (message_flag != 0) {
            uint8_t len = message_flag;
            char msg[256];

            if (len > 255) {
                len = 255;
            }
            if (recv_loop(client_fd, msg, len) < 0) break;
            msg[len] = '\0';

            printf(">>>%s\n", msg);
            fflush(stdout);
            continue;
        }

        uint8_t word_length, num_incorrect;
        if (recv_loop(client_fd, &word_length, 1) < 0) break;
        if (recv_loop(client_fd, &num_incorrect, 1) < 0) break;

        char data[256];
        if (recv_loop(client_fd, data, word_length + num_incorrect) < 0) break;

        printf(">>>");
        for (int i = 0; i < word_length; i++) {
            printf("%c", data[i]);
            if (i < word_length - 1) {
                printf(" ");
            }
        }
        printf("\n");

        printf(">>>Incorrect Guesses: ");
        for (int i = 0; i < num_incorrect; i++) {
            printf("%c", data[word_length + i]);
            if (i < num_incorrect - 1) {
                printf(" ");
            }
        }
        printf("\n>>>\n");
        fflush(stdout);

        char guess = waitForGuess(client_fd);
        sendMessage(client_fd, 1, guess);
    }
}
void checkOverloaded(int client_fd) {
    uint8_t flag;
    int n = recv(client_fd, &flag, 1, MSG_DONTWAIT);

    if (n <= 0) {
        return;
    }
    if (flag == 0) {
        return;
    }

    uint8_t len = flag;
    char message[256];

    if (recv_loop(client_fd, message, len) < 0) {
        return;
    }
    message[len] = '\0';

    if (strcmp(message, "server-overloaded") == 0) {
        printf(">>>server-overloaded\n");
        close(client_fd);
        exit(EXIT_SUCCESS);
    }
}


void setupHangman(int client_fd) {
    char line[256];

    printf(">>>Ready to start game? (y/n): ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
        printf("\n");
        close(client_fd);
        exit(EXIT_SUCCESS);
    }

    if (tolower(line[0]) == 'y') {
        sendMessage(client_fd, 0, '\0');
        playHangman(client_fd);
    } else {
        close(client_fd);
        exit(EXIT_SUCCESS);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) return -1;

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        exit(EXIT_FAILURE);
    }

    if (connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        exit(EXIT_FAILURE);
    }
    checkOverloaded(client_fd);
    setupHangman(client_fd);
    close(client_fd);
    return 0;
}
