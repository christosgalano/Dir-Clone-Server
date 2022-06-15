#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <libgen.h>

#include "common.h"


int main(int argc, char* argv[]) {
    if (argc != 7) {
        fprintf(stderr, "Usage: -i <server_ip> -p <server_port> -d <directory>\n");
        exit(EXIT_FAILURE);
    }

    int server_port = 0;
    char* server_ip, * directory;
    server_ip = directory = NULL;

    // Parse arguments
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i")) {
            server_ip = argv[++i];
        }
        else if (!strcmp(argv[i], "-p")) {
            server_port = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-d")) {
            directory = argv[++i];
            if (directory[0] != '/' || strlen(directory) == 1) {
                fprintf(stderr, "Directory must begin with '/' and have length > 1\n");
                exit(EXIT_FAILURE);
            }
        }
        else {
            fprintf(stderr, "Usage: -i <server_ip> -p <server_port> -d <directory>\n");
            exit(EXIT_FAILURE);
        }
    }

    if (!server_ip || !server_port || !directory) {
        fprintf(stderr, "All arguments must be initialized\n");
        exit(EXIT_FAILURE);
    }

    // Initialize buffer
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    // Create socket
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror_exit("main: socket");
    }

    // Initialize server sockaddr_in struct
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(server_ip);
    server.sin_port = htons(server_port);

    // Initiate connection
    if (connect(sock, (struct sockaddr*) &server, sizeof(server)) < 0) {
        perror_exit("main: connect");
    }
    printf("\nConnecting to %s port %d\n", server_ip, server_port);

    // Send dir to clone
    ssize_t bytes;
    bytes = write(sock, directory, strlen(directory) + 1);
    if (bytes == -1) {
        perror_exit("main: write");
    }

    // Read number of files that reside in the directory and make sure the the dir given is valid
    int no_files;
    memset(buffer, 0, BUFFER_SIZE);
    bytes = read(sock, buffer, MAX_REPR);
    if (bytes == -1) {
        perror_exit("main: read");
    }
    if (!strcmp(buffer, "INVALID DIR")) {
        fprintf(stderr, "Invalid directory\n");
        exit(EXIT_FAILURE);
    }
    else if (!strcmp(buffer, "COULD NOT OPEN DIR/S")) {
        fprintf(stderr, "Server had no permissions to open the specified directory or a directory that resides inside it\n");
        exit(EXIT_FAILURE);
    }
    no_files = atoi(buffer);
    printf("Number of files inside %s: %d\n", directory, no_files); // includes nested directories

    // Create dir clone in results, only if dir was valid
    memset(buffer, 0, BUFFER_SIZE);
    strcat(buffer, "results");
    strcat(buffer, directory);
    recursive_mkdir(buffer);

    // Send response
    memset(buffer, 0, BUFFER_SIZE);
    strcat(buffer, "NF READ");
    bytes = write(sock, buffer, ACK_LEN);
    if (bytes == -1) {
        perror_exit("main: write");
    }

    // Read block size
    int block_size;
    memset(buffer, 0, BUFFER_SIZE);
    bytes = read(sock, buffer, MAX_REPR);
    if (bytes == -1) {
        perror_exit("receive: read");
    }
    block_size = atoi(buffer);
    printf("Block size: %d bytes\n", block_size);

    // Send response
    memset(buffer, 0, BUFFER_SIZE);
    strcat(buffer, "BS READ");
    bytes = write(sock, buffer, ACK_LEN);
    if (bytes == -1) {
        perror_exit("main: write");
    }

    // While the task has not been completed
    while (no_files > 0) {
        // Receive a file
        receive(sock, "results", block_size);
        no_files--;

        // Inform the server how many files remain to be received
        memset(buffer, 0, BUFFER_SIZE);
        sprintf(buffer, "%d", no_files);
        bytes = write(sock, buffer, strlen(buffer) + 1);
        if (bytes == -1) {
            perror_exit("main: write");
        }
    }

    if (!no_files) {
        printf("Directory %s has been successfully cloned in results.\n", directory);
    }

    close(sock);
    exit(EXIT_SUCCESS);
}
