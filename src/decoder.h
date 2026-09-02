#ifndef DECODER_H
#define DECODER_H

#include "archive.h"
#include <pthread.h>
#include <stdint.h>

#define CACHE_CAPACITY 24

typedef struct {
    int page_idx;
    int width;
    int height;
    unsigned char *rgba;
    size_t buffer_capacity;
    int is_ready;
    int in_progress;
    uint64_t last_access;
} DecodedSlot;

typedef struct {
    CBZArchive *arch;
    pthread_t *threads;
    int num_threads;
    int running;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    int current_requested_page;
    int priority_queue[CACHE_CAPACITY];
    int priority_count;

    DecodedSlot slots[CACHE_CAPACITY];
    uint64_t tick_counter;
} PageDecoder;

PageDecoder *decoder_init(CBZArchive *arch);
void decoder_request_page(PageDecoder *dec, int page_idx, int direction);
DecodedSlot *decoder_get_slot(PageDecoder *dec, int page_idx);
DecodedSlot *decoder_get_slot_locked(PageDecoder *dec, int page_idx);
void decoder_lock(PageDecoder *dec);
void decoder_unlock(PageDecoder *dec);
void decoder_cleanup(PageDecoder *dec);

#endif /* DECODER_H */
