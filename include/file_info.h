#pragma once

#include <pthread.h> 

struct file_info {
    int socket_fd;
    int block_size;
    char* filepath;
    pthread_mutex_t* mutex;
};
typedef struct file_info* FileInfo;

FileInfo create_file_info(int fd, int bs, char* file_path, pthread_mutex_t* client_mutex);

void destroy_file_info(FileInfo file_info);