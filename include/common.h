#pragma once

#include <pthread.h>

#include "queue.h"

#define ACK_LEN            8    // length of acknowledgement message
#define MAX_REPR         100    // no characters to represent a number
#define DIR_PERMS       0755    // permissions for a newly created directory
#define FILE_PERMS      0644    // permissions for a newly created file
#define BUFFER_SIZE     4096    // buffer size
#define MAX_CONNECTIONS  100    // max number of connections the server can have opened


// Simple struct used to pass information to a communication thread's routine
typedef struct arg_set {
    int fd;
    int block_size;
} arg_set;


// Global variables
pthread_t* workers;
Queue queue;
pthread_mutex_t queue_mutex;
pthread_cond_t queue_non_empty;
pthread_cond_t queue_non_full;


// Print error message and exit process
void perror_exit(const char* message);

// Print error message and exit thread
void perror_thr(const char* message, long thread_id);

// Check if the given path is a directory
int is_dir(char* path);

// Check if the given path is a regular file
int is_file(char* path);

// Check if a file with the given filepath already exists
int file_exists(char* filepath);

// Recursively create all the directories specified in path
void recursive_mkdir(const char* dir);

// Count the number of files inside the given directory 
int count_no_files(char* dirpath);

// Scan the directory and insert its content into the queue
void scan_dir(char* dirpath, int fd, int block_size, pthread_mutex_t* client_mutex);

// Send the file to the client listening at fd
void send_file(FileInfo file_info);

// Receive messages from the server (file name, metadata, file content)
int receive(int socket, char* dirpath, int block_size);

// Worker's logic
void* process(void* args);