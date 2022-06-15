#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <libgen.h>

#include "common.h"

extern int errno;


/////////////////////////////////////////////// Error related ///////////////////////////////////////////////

void perror_exit(const char* message) {
    perror(message);
    exit(EXIT_FAILURE);
}

void perror_thr(const char* message, long thread_id) {
    fprintf(stderr, "[Thread %ld]: %s\n", thread_id, message);
    pthread_exit(NULL);
}


/////////////////////////////////////////////// Dir - File related ///////////////////////////////////////////////

int is_dir(char* path) {
    struct stat s;
    if (stat(path, &s) == -1) {
        return -1;
    }
    return (S_ISDIR(s.st_mode) ? 1 : 0);
}

int is_file(char* path) {
    struct stat s;
    if (stat(path, &s) == -1) {
        return -1;
    }
    return (S_ISREG(s.st_mode) ? 1 : 0);
}

int file_exists(char* filepath) {
    struct stat s;
    return (stat(filepath, &s) == 0);
}

void recursive_mkdir(const char* dir) {
    char tmp[BUFFER_SIZE];
    char* p = NULL;
    size_t len;
    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, DIR_PERMS) != 0 && errno != EEXIST) {    // ignore 'already exists' errors
                perror_exit("recursive_mkdir mkdir");
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, DIR_PERMS) != 0 && errno != EEXIST) {
        perror_exit("recursive_mkdir mkdir");
    }
}



/////////////////////////////////////////////// Server related ///////////////////////////////////////////////

int count_no_files(char* dirpath) {
    DIR* dir = opendir(dirpath);
    if (!dir) {
        return -1;   // if we could not open even 1 dir - nested or not - we want the process to fail
    }
    int count = 0;
    char* new_path;
    struct dirent* dp;
    while ((dp = readdir(dir))) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }
        switch (dp->d_type) {
        case DT_REG:
            ++count;
            break;
        case DT_DIR:
            new_path = malloc(strlen(dirpath) + strlen(dp->d_name) + 2);
            sprintf(new_path, "%s/%s", dirpath, dp->d_name);
            int t = count_no_files(new_path);
            if (t < 0) {
                count = t;
                return count;
            }
            count += t;
            free(new_path);
            new_path = NULL;
            break;
        default:
            continue;
        }
    }
    closedir(dir);
    return count;
}

void scan_dir(char* dirpath, int fd, int block_size, pthread_mutex_t* client_mutex) {
    DIR* dir = opendir(dirpath);
    if (!dir) {
        close(fd);
        perror_thr("scan_dir: opendir", pthread_self());
    }
    char path[BUFFER_SIZE];
    struct dirent* dp;
    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
            strcpy(path, dirpath);
            strcat(path, "/");
            strcat(path, dp->d_name);
            switch (dp->d_type) {
            case DT_DIR: {
                scan_dir(path, fd, block_size, client_mutex);
                break;
            }
            case DT_REG: {
                // Insert the new file_info into the queue if it is not full, otherwise wait
                FileInfo file_info = create_file_info(fd, block_size, path, client_mutex);
                pthread_mutex_lock(&queue_mutex);
                while (queue->free_slots == 0) {
                    pthread_cond_wait(&queue_non_full, &queue_mutex);
                }
                printf("[Communication Thread %ld]: adding file %s to the queue\n", pthread_self(), path);
                insert_file_info(queue, file_info);
                pthread_cond_signal(&queue_non_empty);
                pthread_mutex_unlock(&queue_mutex);
                break;
            }
            default:
                continue;
            }
        }
    }
    closedir(dir);
}

void send_file(FileInfo file_info) {
    // Extract information
    char* filepath = file_info->filepath;
    int fd = file_info->socket_fd;
    int block_size = file_info->block_size;

    // Make sure the given filepath is indeed a file
    struct stat s;
    if (stat(filepath, &s) == -1) {
        perror_thr("send_file: stat", pthread_self());
    }
    else if (S_ISREG(s.st_mode) == 0) {
        perror_thr("send_file: invalid file", pthread_self());
    }

    // Open the file
    int read_fd;
    if ((read_fd = open(filepath, O_RDONLY)) < 0) {
        close(fd);
        perror_thr("send_file: open", pthread_self());
    }

    // Initialize metadata buffer
    char metadata[MAX_REPR];
    memset(metadata, 0, MAX_REPR);

    // Write filepath and wait response
    ssize_t bytes;
    bytes = write(fd, filepath, strlen(filepath) + 1);
    if (bytes == -1) {
        close(fd);
        close(read_fd);
        perror_thr("send_file: write", pthread_self());
    }
    memset(metadata, 0, MAX_REPR);
    bytes = read(fd, metadata, ACK_LEN);
    if (bytes == -1) {
        close(fd);
        close(read_fd);
        perror_thr("send_file: read", pthread_self());
    }
    if (strcmp(metadata, "FP READ")) {
        close(fd);
        close(read_fd);
        perror_thr("send_file: error during server-client communication", pthread_self());
    }
    memset(metadata, 0, MAX_REPR);

    // Write file size and wait response
    int file_size = s.st_size;
    sprintf(metadata, "%d", file_size);
    bytes = write(fd, metadata, strlen(metadata) + 1);
    if (bytes == -1) {
        close(fd);
        close(read_fd);
        perror_thr("send_file: write", pthread_self());
    }
    memset(metadata, 0, MAX_REPR);

    bytes = read(fd, metadata, ACK_LEN);
    if (bytes == -1) {
        close(fd);
        close(read_fd);
        perror_thr("send_file: read", pthread_self());
    }
    if (strcmp(metadata, "FS READ")) {
        close(fd);
        close(read_fd);
        perror_thr("send_file: error during server-client communication", pthread_self());
    }
    memset(metadata, 0, MAX_REPR);

    // Sent file content
    char* buffer = malloc(sizeof(char) * block_size);
    memset(buffer, 0, block_size);
    while (1) {
        bytes = read(read_fd, buffer, block_size);
        if (bytes == -1) {
            close(fd);
            close(read_fd);
            perror_thr("send_file: read", pthread_self());
        }
        else if (bytes == 0) {
            break;
        }
        bytes = write(fd, buffer, bytes);
        if (bytes == -1) {
            close(fd);
            close(read_fd);
            perror_thr("send_file: write", pthread_self());
        }
        memset(buffer, 0, block_size);
    }

    memset(buffer, 0, block_size);
    bytes = read(fd, buffer, MAX_REPR);
    if (bytes == -1) {
        perror_thr("send_file: read", pthread_self());
    }
    int no_files = atoi(buffer);
    if (!no_files) {
        // Task completed: close fd and free client's mutex
        printf("[Worker Thread %ld]: all files sent, closing client socket %d\n", pthread_self(), fd);
        close(fd);
        free(file_info->mutex);
    }

    // Cleanup
    free(buffer);
    close(read_fd);
}



/////////////////////////////////////////////// Client related ///////////////////////////////////////////////

int receive(int socket, char* dirpath, int block_size) {
    // We use two static buffers (avoid stack allocation each time):
    // - buffer is used to read/write from/to socket and is modified
    // - b_buffer is used to store important info so it doesn't get lost
    static char buffer[BUFFER_SIZE];
    static char b_buffer[BUFFER_SIZE];

    // Initialize variables
    ssize_t bytes;
    int file_size, count;
    bytes = file_size = count = 0;

    // Read filename and make sure that  the task has not been completed
    bytes = read(socket, buffer, BUFFER_SIZE);
    if (bytes == -1) {
        perror_exit("receive: read");
    }
    printf("\nFile to be received: %s\n", buffer);

    // Send response
    strcat(b_buffer, "FP READ");
    bytes = write(socket, b_buffer, ACK_LEN);
    if (bytes == -1) {
        perror_exit("receive: write");
    }
    memset(b_buffer, 0, BUFFER_SIZE);
    strcat(b_buffer, dirpath);

    // Create the nested directories if needed
    char* base_name = basename(buffer);
    char* dir_name = dirname(buffer);
    strcat(b_buffer, dir_name);
    recursive_mkdir(b_buffer);
    memset(b_buffer, 0, BUFFER_SIZE);

    // If the current file exists, delete it
    strcat(b_buffer, dirpath);
    strcat(b_buffer, dir_name);
    strcat(b_buffer, "/");
    strcat(b_buffer, base_name);
    if (file_exists(b_buffer)) {
        if (remove(b_buffer)) {
            perror_exit("receive: remove");
        }
    }

    // Create the file
    int write_fd;
    if ((write_fd = open(b_buffer, O_CREAT | O_WRONLY, FILE_PERMS)) == -1) {
        perror_exit("receive: open");
    }

    // Read file size
    bytes = read(socket, buffer, MAX_REPR);
    if (bytes == -1) {
        perror_exit("receive: read");
    }
    file_size = atoi(buffer);
    printf("File size: %d bytes\n", file_size);

    // Send response
    memset(buffer, 0, BUFFER_SIZE);
    strcat(buffer, "FS READ");
    bytes = write(socket, buffer, ACK_LEN);
    if (bytes == -1) {
        perror_exit("receive: write");
    }
    memset(buffer, 0, BUFFER_SIZE);

    // Read server's file content and write it to client's file
    printf("Receiving file's content...\n");
    while (count < file_size) {
        bytes = read(socket, buffer, block_size);
        if (bytes == -1) {
            perror_exit("receive: read");
        }
        count += bytes;
        bytes = write(write_fd, buffer, bytes);
        if (bytes == -1) {
            perror_exit("receive: write");
        }
        memset(buffer, 0, BUFFER_SIZE);
    }
    printf("File received successfully\n\n");

    // Cleanup (set static buffers to 0)
    close(write_fd);
    memset(buffer, 0, BUFFER_SIZE);
    memset(b_buffer, 0, BUFFER_SIZE);

    return 0;
}



/////////////////////////////////////////////// Worker related ///////////////////////////////////////////////

void* process(void* arg) {
    // Detach worker thread - we do not need to join
    if (pthread_detach(pthread_self())) {
        perror_thr("process: pthread_detach", pthread_self());
    }
    while (1) {
        // If the queue is empty wait
        pthread_mutex_lock(&queue_mutex);
        while (queue->free_slots == queue->size) {
            pthread_cond_wait(&queue_non_empty, &queue_mutex);
        }

        // Get file_info and broadcast that the queue is no longer full - if no threads were blocked, broadcast will have no effect
        FileInfo file_info = get_first(queue);
        pthread_cond_broadcast(&queue_non_full);
        pthread_mutex_unlock(&queue_mutex);

        // Lock client's mutex so that only one worker can send a file to the client each time
        pthread_mutex_lock(file_info->mutex);
        printf("[Worker Thread] %ld]: sending file %s to socket %d\n", pthread_self(), file_info->filepath, file_info->socket_fd);
        send_file(file_info);
        pthread_mutex_unlock(file_info->mutex);

        // Cleanup
        destroy_file_info(file_info);
    }
    return NULL;
}
