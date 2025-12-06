#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

static int recv_exact(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = buf;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n <= 0) {
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

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

struct serverMessage server_msg;

// void sendMessage(int client_fd, int length, char data) {
//     struct clientMessage client_msg;
//     client_msg.message_length = length;
//     client_msg.data[0] = data;
//     send(client_fd, &client_msg, sizeof(client_msg), 0);
// }

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
        if(!fgets(guess, sizeof(guess), stdin)) {
            sendMessage(client_fd, 0, '\0');
            printf("\n");
            close(client_fd);
            exit(EXIT_SUCCESS);
        }
        if(strlen(guess) > 0 && guess[strlen(guess) - 1] == '\n') {
            guess[strlen(guess) - 1] = '\0';
        }
        if (strlen(guess) != 1 || !isalpha(guess[0])) {
            printf(">>>Error! Please guess one letter.\n");
        } else {
            g = tolower(guess[0]);
            valid = 1;
        }
    }   
    return g;
}

// void playHangman(int client_fd) {
//     int gameOver = 0;
//     while(!gameOver) {
//         recv(client_fd, &server_msg, sizeof(server_msg), 0);

//         if(server_msg.message_flag != 0) {
//             for(int i = 0; i < server_msg.message_flag; i++) {
//                 printf("%c", server_msg.data[i]);
//             }
//         } else {
//             printf(">>>");
//             for (int i = 0; i < server_msg.word_length; i++) {
//                 printf("%c", server_msg.data[i]);
//                 if (i < server_msg.word_length - 1) {
//                     printf(" ");
//                 }
//             }
//             printf("\n");
//             printf(">>>Incorrect Guesses: ");
//             for (int i = 0; i < server_msg.num_incorrect; i++) {
//                 printf("%c", server_msg.data[server_msg.word_length + i]);
//                 if (i < server_msg.num_incorrect - 1) {
//                     printf(" ");
//                 }
//             }
//             printf("\n>>>\n");
//         }

//         if(server_msg.message_flag == 0) {
//             char guess = waitForGuess(client_fd);
//             sendMessage(client_fd, 1, guess);
//         } else {
//             gameOver = 1;
//         }
//     }
    
// }

void playHangman(int client_fd) {
    for (;;) {
        uint8_t flag;
        if (recv_exact(client_fd, &flag, 1) < 0) {
            break;
        }

        if (flag != 0) {
            uint8_t len = flag;
            char msg[256];

            if (len > sizeof(msg) - 1) {
                if (recv_exact(client_fd, msg, sizeof(msg) - 1) < 0) break;
                msg[sizeof(msg) - 1] = '\0';
            } else {
                if (recv_exact(client_fd, msg, len) < 0) break;
                msg[len] = '\0';
            }

            printf(">>>%s\n", msg);
            continue;
        }

        uint8_t word_len, num_incorrect;
        if (recv_exact(client_fd, &word_len, 1) < 0) break;
        if (recv_exact(client_fd, &num_incorrect, 1) < 0) break;

        int total = word_len + num_incorrect;
        if (total > 255) break;

        char data[256];
        if (recv_exact(client_fd, data, total) < 0) break;

        printf(">>> ");
        for (int i = 0; i < word_len; i++) {
            printf("%c", data[i]);
            if (i < word_len - 1) {
                printf(" ");
            }
        }
        printf("\n");

        printf(">>>Incorrect Guesses:");
        for (int i = 0; i < num_incorrect; i++) {
            printf(" %c", data[word_len + i]);
        }
        printf("\n>>>\n");

        char guess = waitForGuess(client_fd);
        sendMessage(client_fd, 1, guess);
    }
}


// void setupHangman(int client_fd) {
//     if(recv(client_fd, &server_msg, sizeof(server_msg), 0) <= 0) {
//         exit(EXIT_FAILURE);
//     }

//     if (server_msg.message_flag != 0 && strcmp(server_msg.data, "server-overloaded") == 0) {
//         printf(">>>server-overloaded\n");
//         close(client_fd);
//         exit(EXIT_SUCCESS);
//     } else {
//         printf(">>>Ready to start game? (y/n): ");
//         char guess[256];
//         if (!fgets(guess, sizeof(guess), stdin)) {
//             close(client_fd);
//             exit(EXIT_SUCCESS);
//         }
//         if (tolower(guess[0]) == 'y') {
//             sendMessage(client_fd, 0, '\0');
//             playHangman(client_fd);
//         } else {
//             close(client_fd);
//             exit(EXIT_SUCCESS);
//         }
//     }
// }

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


int main (int argc, char* argv[]) {
    if(argc != 3) return -1;
    int port = atoi(argv[2]);
    const char* ip = argv[1];
    int client_fd = socket(AF_INET, SOCK_STREAM, 0); 
    if(client_fd < 0) {
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        exit(EXIT_FAILURE);
    }

    if (connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        exit(EXIT_FAILURE);
    }

    setupHangman(client_fd);
    close(client_fd);
    return 0;

}

