#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"


Queue create_queue(int size) {
    Queue queue = malloc(sizeof(*queue));
    queue->data = malloc(sizeof(FileInfo) * size);
    for (int i = 0; i < size; i++) {
        queue->data[i] = NULL;
    }
    queue->free_slots = queue->size = size;
    return queue;
}

void insert_file_info(Queue queue, FileInfo file_info) {
    for (int i = 0; i < queue->size; i++) {
        if (queue->data[i] == NULL) {
            queue->data[i] = file_info;
            queue->free_slots--;
            return;
        }
    }
}

FileInfo get_first(Queue queue) {
    for (int i = 0; i < queue->size; i++) {
        if (queue->data[i] != NULL) {
            FileInfo file_info = queue->data[i];
            queue->data[i] = NULL;
            queue->free_slots++;
            return file_info;
        }
    }
    return NULL;
}

void destroy_queue(Queue queue) {
    for (int i = 0; i < queue->size; i++) {
        destroy_file_info(queue->data[i]);
    }
    free(queue->data);
    free(queue);
    queue = NULL;
}
