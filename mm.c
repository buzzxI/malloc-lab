/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

#define MIN_UNIT 4
/* address length(6 Bytes is enough) */
#define ADD_LEN 8
/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
/* max list size 2^{24}(16MB) */
#define LIST_SIZE 25
/* minimum request block size */
#define MIN_CHUNK 1 << 12
/* every block(allocated/freed) should be larger than 24 Bytes */
#define MIN_BLOCK 24
/* illegal address */
#define NULL_ADD 0

/* get block size from header */
#define BLOCK_SIZE(header) (*(UI*)(header) & ~0x7)
/* build new header(footer) */
#define PACK(header, block_size, pre, cur) (*(UI*)(header) = (block_size) | ((pre) & 1) << 1 | ((cur) & 1))
/* rebuild header with new size */
#define NEW_SIZE(header, size) (*(UI*)(header) = (size) | *(UI*)(header) & 0x7)
/* get block footer */
#define GET_FOOTER(header) ((header) + BLOCK_SIZE(header) - MIN_UNIT)
/* rebuild block's header and footer */
#define REBUILD_HF(header, size) (NEW_SIZE(header, size), PACK(GET_FOOTER(header), (size), (*(UI*)(header) & 0x2) >> 1, *(UI*)(header) & 0x1))
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

typedef unsigned long long ULL;
typedef unsigned int UI;

/* segregated list */
static ULL list[LIST_SIZE];
/* points to the first block of heap */
static char* heapp;
static char* end;
/* arrays used by high_bit */
const static UI b[] = {0x2, 0xc, 0xf0, 0xff00, 0xffff0000};
const static int s[] = {1, 2, 4, 8, 16};
/* helper functions */
static int high_bit(UI val);
static void* extend_heap(size_t size);
static void* coalesce(void* header);
static void detach_off(void* header);
static void* allocate_block(void* header, size_t size);
static void split_block(void* header, UI block_size);
static void link_to_list(void* header);
int a_hits = 0;
int f_hits = 0;

/**
 * mm_init - initialize the malloc package.
 * prologue block and epilogue block are 4 Bytes, they are marked with block size zero
 * prologue block begins from the heap, every list has an epilogue block
*/
int mm_init(void)
{
    // create prologue block and epilogue block 
    if ((heapp = mem_sbrk(2 * MIN_UNIT)) == (void*)-1) return -1;
    // mark prologue block
    PACK(heapp, 0, 0, 1);
    // mark epilogue block
    PACK(heapp + MIN_UNIT, 0, 1, 1);
    int i;
    // initialize segregated list
    for (i = 0; i < LIST_SIZE; i++) list[i] = NULL_ADD;
    extend_heap(MIN_CHUNK);
    return 0;
}

/**
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
*/
void *mm_malloc(size_t size)
{
    if (size == 0) return NULL;
    // every allocated block has a header with 4 Bytes
    size += MIN_UNIT;
    size = ALIGN(size);
    if (size < MIN_BLOCK) size = MIN_BLOCK;
    int idx;
    for (idx = high_bit(size); idx < LIST_SIZE; idx++) {
        ULL header;
        for (header = list[idx]; header != NULL_ADD; header = *(ULL*)(header + MIN_UNIT + ADD_LEN)) {
            UI tmp_size = BLOCK_SIZE((void*)header);
            if (tmp_size >= size) return allocate_block((void*)header, size);
        }
    }
    void* header = extend_heap(size);
    if (header == NULL) return NULL;
    return allocate_block(header, size);
    
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    // coalesce block immediately
    void* header = coalesce(ptr - MIN_UNIT);
    // link free block to segregated free list
    link_to_list(header);
    // clear next block's pre block allocation bit
    UI size = BLOCK_SIZE(header);
    *(UI*)(header + size) &= ~2;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }
    UI request_size = size + MIN_UNIT;
    request_size = ALIGN(request_size);
    void* header = ptr - MIN_UNIT;
    UI ori_size = BLOCK_SIZE(header);
    // if original block is big enough, then we may want to return original block immediately
    // however we should check block size first, if @param:size is much smaller than block size
    // then we should split original block
    if (ori_size >= request_size) {
        if (ori_size - request_size >= 24) split_block(header, request_size);
        return ptr;
    } else {
        void* ne_block = mm_malloc(size);
        memcpy(ne_block, ptr, ori_size - MIN_UNIT);
        mm_free(ptr);
        return ne_block;
    }
}

/**
 * extend heap with a block at least @param:size Bytes
 * heap will extend at least MIN_CHUNK(4K) Bytes
 * @returns header of new block 
*/
static void* extend_heap(size_t size) {
    if (size < MIN_CHUNK) size = MIN_CHUNK;
    // size round up to align 8 Bytes
    size = ALIGN(size);
    void* p;
    if ((p = mem_sbrk(size)) == (void*)-1) return NULL;
    // extend heap will request a new block at the top of heap
    // thus we need to exterminate old and create new epilogue block
    void* header = p - MIN_UNIT;
    // clear current block's allocation bits
    *(UI*)header &= ~1;
    // rebuild header and footer
    REBUILD_HF(header, size);
    // rebuild epilogue block
    PACK(header + size, 0, 0, 1);
    // try to coalese new block with front block 
    header = coalesce(header);
    link_to_list(header);
    // clear epilogue block's pre block allocation bit
    size = BLOCK_SIZE(header);
    *(UI*)(header + size) &= ~2;
    return header;
}

/**
 * coalesce current free block with physical pre and succ free blocks
 * @return new header
*/
static void* coalesce(void* header) {
    UI size = BLOCK_SIZE(header);
    void* ne = header + size;
    // if next block is free block
    if (!(*(UI*)ne & 0x1)) {
        size += BLOCK_SIZE(ne);
        detach_off(ne);
    }
    // if pre block is free block
    if (!((*(UI*)header >> 1) & 0x1)) {
        UI pre_size =  BLOCK_SIZE(header - MIN_UNIT);
        void* pre = header - pre_size;
        size += pre_size;
        detach_off(pre);
        header = pre;
    }
    // clear current block's allocation bit
    *(UI*)header &= ~1;
    REBUILD_HF(header, size);
    // clear next block's pre block allocation bit
    *(UI*)(header + size) &= ~2;
    return header;
}

/**
 * allocate a free block
 * physically, a free block may much larger than @param: size Bytes, free block may be splitted
 * allocate_block returns a pointer which points to the first byte in free block
*/
static void* allocate_block(void* header, size_t size) {
    *(UI*)header |= 1;
    detach_off(header);
    UI ori_size = BLOCK_SIZE(header);
    // if remaining space is larger than 24 Bytes(minimum cost of free block)
    // then we should split the block
    if (ori_size - size >= 24) split_block(header, size);
    // set next block's pre block allocation bit
    else *(UI*)(header + ori_size) |= 2;
    return header + MIN_UNIT;
}

/**
 * split original block with the first block has size @param: size
 * the first block will be considered as allocated block
 * the second block will be initialized as free block
 * after splitting a free block, split_block will call coalesce
 * to merge the second free block and the next free block(if present)
*/
static void split_block(void* header, UI size) {
    // rebuild first block's header
    UI ori_size = BLOCK_SIZE(header);
    NEW_SIZE(header, size);
    UI ne_size = ori_size - size;
    void* ne = header + size;
    *(UI*)(ne) |= 0x2;
    *(UI*)(ne) &= ~0x1;
    // rebuild next block's header and footer
    REBUILD_HF(ne, ne_size);
    ne = coalesce(ne);
    link_to_list(ne);
    // clear next block's pre block allocation bit
    *(UI*)(ne + ne_size) &= ~0x2;
}


/**
 * add free block with @param: header to free list
*/
static void link_to_list(void* header) {
    int size = BLOCK_SIZE(header);
    // find suitable list
    int idx = high_bit(size);
    *(ULL*)(header + MIN_UNIT) = NULL_ADD;
    // link free block to segregated list
    *(ULL*)(header + MIN_UNIT + ADD_LEN) = list[idx];
    if (list[idx] != NULL_ADD) *(ULL*)(list[idx] + MIN_UNIT) = (ULL)header;
    // link current block to list
    list[idx] = (ULL)header;
}

/**
 * detach current free block from segregated free list
*/
static void detach_off(void* header) {
    UI size = BLOCK_SIZE(header);
    int idx = high_bit(size);
    ULL pre = *(ULL*)(header + MIN_UNIT);
    ULL ne = *(ULL*)(header + MIN_UNIT + ADD_LEN);
    if (pre == NULL_ADD && ne == NULL_ADD) list[idx] = NULL_ADD;
    else {
        if (ne != NULL_ADD) {
            *(ULL*)(ne + MIN_UNIT) = pre;
            if (pre == NULL_ADD) list[idx] = ne;
        }
        if (pre != NULL_ADD) *(ULL*)(pre + MIN_UNIT + ADD_LEN) = ne;
    }
}

/* bit wise trick */

/* round down to log @param:size(2 based) */
static int high_bit(UI val) {
    int i, bit = 0;
    for (i = 4; i >= 0; i--) {
        if (val & b[i]) {
            val >>= s[i];
            bit |= s[i];
        }
    }
    return bit;
}