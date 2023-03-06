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
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
/* max list size 2^{24}(16MB) */
#define LIST_SIZE 25
/* minimum request block size */
#define MIN_CHUNK 1 << 12
/* illegal address */
#define NULL_ADD 0

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
static UI block_size(void* header);
static void* extend_heap(size_t size);
static void pack(void* header, UI block_size, int pre, int cur);
static void new_size(void* header, UI size);
static void rebuild_hf(void* header, UI size);
static void* get_footer(void* header);
static void* coalesce(void* header);
static void detach_off(void* header);
static void* allocate_block(void* header, size_t size);
static void split_block(void* header, UI block_size);
static UI round_up_size(UI size);
static void link_to_list(void* header);
static int legal(void* add);
int a_hits = 0;
int f_hits = 1;

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
    pack(heapp, 0, 0, 1);
    // mark epilogue block
    pack(heapp + 4, 0, 1, 1);
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
    if (a_hits == 101) {
        int num = 10;
    }
    a_hits++;
    if (size == 0) return NULL;
    // every allocated block has a header with 4 Bytes
    size += MIN_UNIT;
    size = round_up_size(size);
    int idx;
    for (idx = high_bit(size); idx < LIST_SIZE; idx++) {
        ULL header;
        for (header = list[idx]; legal(header); header = *(ULL*)(header + MIN_UNIT + ADD_LEN)) {
            UI tmp_size = block_size((void*)header);
            if (tmp_size >= size) {
                return allocate_block((void*)header, size);
            }
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
    if (f_hits == 1965) {
        int num = 10;
    }
    f_hits++;
    printf("block size: %d\n", block_size(ptr - MIN_UNIT));
    // coalesce block immediately
    void* header = coalesce(ptr - MIN_UNIT);
    // link free block to segregated free list
    link_to_list(header);
    // clear next block's pre block allocation bit
    UI size = block_size(header);
    *(UI*)(header + size) &= ~2;
    //printf("free: %p ~ %p\n", header, header + size);
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
    request_size = round_up_size(request_size);
    void* header = ptr - MIN_UNIT;
    UI ori_size = block_size(header);
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
    size = round_up_size(size);
    void* p;
    if ((p = mem_sbrk(size)) == (void*)-1) return NULL;
    // extend heap will request a new block at the top of heap
    // thus we need to exterminate old and create new epilogue block
    void* header = p - MIN_UNIT;
    // clear current block's allocation bits
    *(UI*)header &= ~1;
    // rebuild header and footer
    rebuild_hf(header, size);
    // rebuild epilogue block
    pack(header + size, 0, 0, 1);
    end = header + size;
    // try to coalese new block with front block 
    header = coalesce(header);
    link_to_list(header);
    // clear epilogue block's pre block allocation bit
    size = block_size(header);
    *(UI*)(header + size) &= ~2;
    return header;
}
/**
 * coalesce current free block with physical pre and succ free blocks
 * @return new header
*/
static void* coalesce(void* header) {
    UI size = block_size(header);
    void* ne = header + size;
    // if next block is free block
    if (!(*(UI*)ne & 1)) {
        size += block_size(ne);
        detach_off(ne);
    }
    // if pre block is free block
    //if (!(*(UI*)header & 0x2)) {
    if (!((*(UI*)header >> 1) & 1)) {
        UI pre_size =  block_size(header - MIN_UNIT);
        void* pre = header - pre_size;
        size += pre_size;
        detach_off(pre);
        header = pre;
    }
    // clear current block's allocation bit
    *(UI*)header &= ~1;
    rebuild_hf(header, size);
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
    UI ori_size = block_size(header);
    // if remaining space is larger than 24 Bytes(minimum cost of free block)
    // then we should split the block
    if (ori_size - size >= 24) {
        //printf("allocate: %p ~ %p\n", header, header + size);
        split_block(header, size);
    }
    else {
        //printf("allocate: %p ~ %p\n", header, header + ori_size);
        // set next block's pre block allocation bit
        *(UI*)(header + ori_size) |= 2;
    }
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
    UI ori_size = block_size(header);
    new_size(header, size);
    void* ne = header + size;
    UI ne_size = ori_size - size;
    // rebuild second block's header and footer
    pack(ne, ne_size, 1, 0);
    pack(get_footer(ne), ne_size, 1, 0);
    ne = coalesce(ne);
    link_to_list(ne);
    // clear next block's pre block allocation bit
    *(UI*)(ne + ne_size) &= ~2;
}


/**
 * add free block with @param: header to free list
*/
static void link_to_list(void* header) {
    int size = block_size(header);
    // find suitable list
    int idx = high_bit(size);
    *(ULL*)(header + MIN_UNIT) = NULL_ADD;
    // link free block to segregated list
    *(ULL*)(header + MIN_UNIT + ADD_LEN) = list[idx];
    if (list[idx] != NULL_ADD) {
        *(ULL*)(list[idx] + MIN_UNIT) = (ULL)header;
    }
    // link current block to list
    list[idx] = (ULL)header;
}

/**
 * detach current free block from segregated free list
*/
static void detach_off(void* header) {
    UI size = block_size(header);
    int idx = high_bit(size);
    ULL pre = *(ULL*)(header + MIN_UNIT);
    ULL ne = *(ULL*)(header + MIN_UNIT + ADD_LEN);
    if (pre == NULL_ADD && ne == NULL_ADD) {
        list[idx] = NULL_ADD;
    } else {
        if (ne != NULL_ADD) {
            *(ULL*)(ne + MIN_UNIT) = pre;
            if (pre == NULL_ADD) {
                list[idx] = ne;
            }
        }
        if (pre != NULL_ADD) {
            *(ULL*)(pre + MIN_UNIT + ADD_LEN) = ne;
        }
    }
}

static UI round_up_size(UI size) {
    return ((size + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;
}

/* get block size */
static UI block_size(void* header) {
    return *(UI*)header & ~0x7;
}

/**
 * assign free block new size
 * typically new size is larger than old one
 * only size changes, control bit remains
*/
static void new_size(void* header, UI size) {
    *(UI*)header &= 0x7;
    *(UI*)header |= size;
}

/**
 * use @param: size to rebuild header and footer
 * @param:size will not modify block's control bits
 * rebuild_hf will make sure new header and footer are identical
*/
static void rebuild_hf(void* header, UI size) {
    new_size(header, size);
    // rebuild footer
    pack(get_footer(header), size, *(UI*)header & 0x2, *(UI*)header & 0x1);
}

/* pack the header with specific value */
static void pack(void* header, UI block_size, int pre, int cur) {
    *(UI*)header = block_size | (pre & 1) << 1 | (cur & 1);
}

// only free block has footer
static void* get_footer(void* header) {
    return header + block_size(header) - MIN_UNIT;
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

static int legal(void* add) {
    return add <= end && add >= heapp;
}