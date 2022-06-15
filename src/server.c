#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>

#include "common.h"


void* client_communication(void* args);


int main(int argc, char* argv[]) {
    if (argc != 9) {
        fprintf(stderr, "Usage: -p <port_number> -s <thread_pool_size> -q <queue_size> -b <block_size>\n");
        exit(EXIT_FAILURE);
    }

    int port_number, thread_pool_size, queue_size, block_size;
    port_number = thread_pool_size = queue_size = block_size = 0;

    // Parse arguments
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p")) {
            port_number = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-s")) {
            thread_pool_size = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-q")) {
            queue_size = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-b")) {
            block_size = atoi(argv[++i]);
        }
        else {
            fprintf(stderr, "Usage: -p <port_number> -s <thread_pool_size> -q <queue_size> -b <block_size>\n");
            exit(EXIT_FAILURE);
        }
    }

    // Make sure proper values have been given
    if (port_number <= 0 || thread_pool_size <= 0 || queue_size <= 0 || block_size <= 0) {
        fprintf(stderr, "None of the arguments can be less or equal than zero\n");
        exit(EXIT_FAILURE);
    }

    // Create a queue and initialize it's mutex and cond variables
    queue = create_queue(queue_size);
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_non_empty, NULL);
    pthread_cond_init(&queue_non_full, NULL);

    // Create workers thread pool
    workers = malloc(sizeof(pthread_t) * thread_pool_size);
    for (int i = 0; i < thread_pool_size; i++) {
        pthread_create(&workers[i], NULL, process, NULL);
    }

    printf("\nServer's parameters are:\n");
    printf("Port number: %d\n", port_number);
    printf("Thread pool size: %d\n", thread_pool_size);
    printf("Queue size: %d\n", queue_size);
    printf("Block size: %d\n", block_size);
    printf("Server was successfully initialized...\n");


    // Create socket
    int listen_socket;
    if ((listen_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror_exit("main: socket");
    }

    // Initialize sockaddr_in struct
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port_number);

    int reuse = 1;
    if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*) &reuse, sizeof(reuse)) < 0) {
        perror_exit("main: setsockopt");
    }

    // Bind socket to address
    if (bind(listen_socket, (struct sockaddr*) &server, sizeof(server)) < 0) {
        perror_exit("main: bind");
    }

    // Listen for connections
    if (listen(listen_socket, MAX_CONNECTIONS) < 0) {
        perror_exit("main: listen");
    }
    printf("\nListening for connections to port %d...\n", port_number);

    int client_socket;
    struct sockaddr_in client;
    socklen_t client_len;

    while (1) {
        // Accept a connection request
        if ((client_socket = accept(listen_socket, (struct sockaddr*) &client, &client_len)) < 0) {
            perror_exit("main: accept");
        }

        // Set proper args for communication's thread routine
        arg_set args;
        args.fd = client_socket;
        args.block_size = block_size;

        // Create a new communication thread
        pthread_t thr;
        if (pthread_create(&thr, NULL, client_communication, (void*) &args)) {
            fprintf(stderr, "main: pthread_create\n");
            exit(EXIT_FAILURE);
        }
    }

}


/// Note: if an error occurs inside the thread we close the socked and exit the thread. We do not exit the server process !!! ///

void* client_communication(void* args) {
    // Fetch information
    arg_set* a = args;
    int sock = a->fd;
    int block_size = a->block_size;

    // Detach communication thread - we do not need to join
    int error;
    if ((error = pthread_detach(pthread_self()))) {
        close(sock);
        perror_thr("client_communication: pthread_detach", pthread_self());
    }

    // Initialize
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    // Read directory path
    ssize_t bytes;
    bytes = read(sock, buffer, BUFFER_SIZE);
    if (bytes == -1) {
        close(sock);
        perror_thr("client_communication: read", pthread_self());
    }

    // Ensure the path corresponds indeed to a directory
    error = is_dir(buffer);
    if (error != 1) {
        memset(buffer, 0, BUFFER_SIZE);
        strcat(buffer, "INVALID DIR");
        bytes = write(sock, buffer, strlen("INVALID DIR") + 1);
        if (bytes == -1) {
            close(sock);
            perror_thr("client_communication: write", pthread_self());
        }
        close(sock);
        perror_thr("client_communication: invalid directory", pthread_self());
    }

    // Find the number of files that reside inside the given directory
    int no_files = count_no_files(buffer);

    // If we could not open the directory or some nested directory (for example no permissions)
    if (no_files < 0) {
        memset(buffer, 0, BUFFER_SIZE);
        strcat(buffer, "COULD NOT OPEN DIR/S");
        bytes = write(sock, buffer, strlen("COULD NOT OPEN DIR/S") + 1);
        if (bytes == -1) {
            close(sock);
            perror_thr("client_communication: write", pthread_self());
        }
        close(sock);
        perror_thr("count_no_files: opendir", pthread_self());
    }

    // Send the number of files that reside inside the given directory
    char* msg = malloc(sizeof(char) * MAX_REPR);
    sprintf(msg, "%d", no_files);
    bytes = write(sock, msg, strlen(msg) + 1);
    if (bytes == -1) {
        free(msg);
        close(sock);
        perror_thr("client_communication: write", pthread_self());
    }

    // Wait for response
    memset(msg, 0, MAX_REPR);
    bytes = read(sock, msg, ACK_LEN);
    if (bytes == -1) {
        free(msg);
        close(sock);
        perror_thr("send_file: read", pthread_self());
    }
    if (strcmp(msg, "NF READ")) {
        free(msg);
        close(sock);
        perror_thr("send_file: error during server-client communication", pthread_self());
    }

    // Write block size and wait response
    memset(msg, 0, MAX_REPR);
    sprintf(msg, "%d", block_size);
    bytes = write(sock, msg, strlen(msg) + 1);
    if (bytes == -1) {
        free(msg);
        close(sock);
        perror_thr("send_file: write", pthread_self());
    }
    memset(msg, 0, MAX_REPR);
    bytes = read(sock, msg, ACK_LEN);
    if (bytes == -1) {
        free(msg);
        close(sock);
        perror_thr("send_file: read", pthread_self());
    }
    if (strcmp(msg, "BS READ")) {
        free(msg);
        close(sock);
        perror_thr("send_file: error during server-client communication", pthread_self());
    }
    free(msg);

    // Create and initialize client's mutex
    pthread_mutex_t* client_mutex = malloc(sizeof(*client_mutex));
    pthread_mutex_init(client_mutex, NULL);

    // Insert the directory's content into the queue
    printf("[Communication Thread %ld]: about to scan directory %s\n", pthread_self(), buffer);
    scan_dir(buffer, sock, block_size, client_mutex);

    printf("[Communication Thread %ld]: exiting...\n", pthread_self());
    pthread_exit(NULL);
}