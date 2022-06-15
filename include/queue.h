#pragma once

#include "file_info.h"

struct queue {
    FileInfo* data;
    int size;
    int free_slots;
};
typedef struct queue* Queue;


// Create a queue
Queue create_queue(int size);

// Insert the given file info into the first empty position of the queue
void insert_file_info(Queue queue, FileInfo file_info);

// Get the value of the queue's first nonempty position
FileInfo get_first(Queue queue);

// Destroy the queue
void destroy_queue(Queue queue);
