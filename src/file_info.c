#include <stdlib.h>
#include <string.h>

#include "file_info.h"


FileInfo create_file_info(int fd, int bs, char* file_path, pthread_mutex_t* client_mutex) {
    FileInfo file_info = malloc(sizeof(*file_info));
    file_info->socket_fd = fd;
    file_info->block_size = bs;
    file_info->filepath = malloc(sizeof(char) * (strlen(file_path) + 1));
    strcpy(file_info->filepath, file_path);
    file_info->mutex = client_mutex;
    return file_info;
}

void destroy_file_info(FileInfo file_info) {
    file_info->mutex = NULL;
    free(file_info->filepath);
    free(file_info);
}