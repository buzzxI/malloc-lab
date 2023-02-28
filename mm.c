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

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
/* max list size 2^{24}(16MB) */
#define LIST_SIZE 25
/* minimum request block size */
#define MIN_CHUNK 1 << 12
/* minimum free block size(header[4 Bytes] + pre[8 Bytes] + succ[8 Bytes] + footer[4 Bytes]) */
#define MIN_BLOCK 24
/* illegal address */
#define NULL_ADD -1

typedef unsigned long long ULL;
typedef unsigned int UI;

/* segregated list */
static ULL list[LIST_SIZE];
/* points to the first block of heap */
static char* heapp;
/* arrays used by high_bit */
const static int b[] = {0x2, 0xc, 0xf0, 0xff00, 0xffff0000};
const static int s[] = {1, 2, 4, 8, 16};
/* helper functions */
static int high_bit(int size);
static UI block_size(void* header);
static void* extend_heap(size_t size);
static int pack(void* header, UI block_size, int pre, int cur);
static int power_of_two(int val);
static void* free_header(void* bp);
static void* allocated_header(void* bp);
static void* get_footer(void* bp);
static void* coalesce(void* header);
static void* allocate_block(void* header, size_t size);
static void split_block(void* header, UI block_size);
static UI round_up_size(UI size);
static UI min(UI a, UI b);

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
    pack(heapp + 4, 0, 0, 1);
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
    size = round_up_size(size);
    int idx;
    for (idx = high_bit(size); idx < LIST_SIZE; idx++) {
        ULL header;
        for (header = list[idx]; header != NULL_ADD; header = *(ULL*)(header + MIN_UNIT + ADD_LEN)) {
            if (block_size((UI*)header) >= size) return allocate_block((void*)header, size);
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
    // mark next block's pre block bit with 0
    *(UI*)(header + block_size(header)) &= ~2;
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
        void* ne_block = mm_malloc(request_size);
        size = min(ori_size - 4, size);
        memcpy(ne_block, ptr, size);
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
    // legalize @param:size(size should be aligned by 8 Bytes and at least MIN_BLOCK(24) Bytes)
    if (size < MIN_CHUNK) size = MIN_CHUNK;
    // size round up to align 8 Bytes
    round_up_size(size);
    void* p;
    if ((p = mem_sbrk(size)) == (void*)-1) return NULL;
    // extend heap will request a new block at the top of heap
    // thus we need to exterminate old and create new epilogue block
    void* header = p - MIN_UNIT;
    // if pre block is allocated then pre bit should be 1
    // if pre block is not allocated, pre bit will be set when pre block is allocated
    // however we cannot coalesce block if pre block is free
    pack(header, size, 1, 0);
    // link free block to segregated list
    *(ULL*)p = NULL_ADD;
    int idx = high_bit(size);
    p += ADD_LEN;
    *(ULL*)p = list[idx];
    if (list[idx] != NULL_ADD) *(ULL*)(list[idx] + MIN_UNIT) = (ULL)header;
    p += ADD_LEN;
    // build footer
    pack(get_footer(p), size, 1, 0);
    // rebuild epilogue block
    pack(header + size, 0, 0, 1);
    return header;
}
/**
 * coalesce current free block with physical pre and succ free blocks
*/
static void* coalesce(void* header) {
    UI size = block_size(header);
    void* ne = header + size;
    // if next block is free block
    if (!(*(UI*)ne & 1)) {
        size += block_size(ne);
        // rebuild next block's free list
        // before: npre <-> ne <-> nne
        // after: npre <-> nne
        // npre and nne can be NULL
        ULL npre = *(ULL*)(ne + MIN_UNIT);
        ULL nne = *(ULL*)(ne + MIN_UNIT + ADD_LEN);
        if (npre != NULL_ADD) *(ULL*)(npre + MIN_UNIT + ADD_LEN) = nne;
        if (nne != NULL_ADD) *(ULL*)(nne + MIN_UNIT) = npre; 
    }
    // if pre block is free block
    if (!((*(UI*)header >> 1) & 1)) {
        int pre_size =  block_size(header - MIN_UNIT);
        void* pre = header - pre_size;
        size += pre_size;
        // rebuild pre block free list
        // before: ppre <-> pre <-> pne
        // after: ppre <-> pne
        // ppre and pne can be NULL
        ULL ppre = *(ULL*)(pre + MIN_UNIT);
        ULL pne = *(ULL*)(pre + MIN_UNIT + ADD_LEN);
        if (ppre != NULL_ADD) *(ULL*)(ppre + MIN_UNIT + ADD_LEN) = pne;
        if (pne != NULL_ADD) *(ULL*)(pne + MIN_UNIT) = ppre;
        header = pre;
    }
    pack(header, size, 1, 0);
    int idx = high_bit(size);
    *(ULL*)(header + MIN_UNIT) = NULL_ADD;
    *(ULL*)(header + MIN_UNIT + ADD_LEN) = list[idx];
    if (list[idx] != NULL_ADD) *(ULL*)(list[idx] + MIN_UNIT) = (ULL)header;
    pack(get_footer(header + MIN_UNIT + ADD_LEN + ADD_LEN), size, 1, 0);
    return header + MIN_UNIT + ADD_LEN + ADD_LEN;
}
/**
 * allocate a free block
 * physically, a free block may much larger than @param: size Bytes, free block may be splitted
 * allocate_block returns a pointer which points to the first byte in free block
*/
static void* allocate_block(void* header, size_t size) {
    *(UI*)header |= 1;
    // detach current free block from segregated list
    // next block in segregated list
    ULL ne = *(ULL*)(header + MIN_UNIT + ADD_LEN);
    if (ne != NULL_ADD) *(ULL*)(ne + MIN_UNIT) = *(ULL*)(header + MIN_UNIT);
    // previous block in segregated list
    ULL pre = *(ULL*)(header + MIN_UNIT);
    if (pre != NULL_ADD) *(ULL*)(pre + MIN_UNIT + ADD_LEN) = *(ULL*)(header + MIN_UNIT + ADD_LEN);
    // if remaining space is larger than 24 Bytes(minimum cost of free block), then we should split the block
    UI ori_size = block_size(header);
    if (ori_size - size >= 24) split_block(header, size);
    else {
        // physical next block
        ne = (ULL)(header + ori_size);
        if (block_size((void*)ne) != 0) *(UI*)ne |= 1 << 1;
    }
    return header + MIN_UNIT + ADD_LEN + ADD_LEN;
}

/**
 * split original block with the first block has size @param: block_size
 * the first block will be considered as allocated block
 * the second block will be initialized as free block
 * after splitting a free block, split_block will call coalesce
 * to merge the second free block and the next free block(if present) 
*/
static void split_block(void* header, UI size) {
    UI ori_size = block_size(header);
    // clear original block size
    *(UI*)header &= 0x7;
    // rebuild first block header
    *(UI*)header |= size;
    void* ne = header + size;
    UI remain_size = ori_size - size;
    // build second block header
    pack(ne, remain_size, 1, 0);
    // merge the second block and the next free block(phyiscally)
    coalesce(ne + MIN_UNIT + ADD_LEN + ADD_LEN);
    remain_size = block_size(ne);
    // find suitable list
    int idx = high_bit(remain_size);
    *(ULL*)(ne + MIN_UNIT) = NULL_ADD;
    // add second free block to segregated list
    *(ULL*)(ne + MIN_UNIT + ADD_LEN) = list[idx];
    if (list[idx] != NULL_ADD) *(ULL*)(list + MIN_UNIT) = (ULL)ne;
    // build second block footer
    pack(get_footer(ne + MIN_UNIT + ADD_LEN + ADD_LEN), remain_size, 1, 0);
    *(UI*)(ne + remain_size) |= 1 << 1;
}

static UI round_up_size(UI size) {
    return ((size + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;
}

/* get block size */
static UI block_size(void* header) {
    return *(UI*)header & ~0x7;
}

/* pack the header with specific value */
static int pack(void* header, UI block_size, int pre, int cur) {
    if (block_size & 0x7) {
        fprintf(stderr, "illegal block size");
        return -1;
    }
    *(UI*)header = block_size | (pre & 1) << 1 | cur & 1;
    return 0;
}

// a free block has a struct of: 
// header[4 Bytes] + pre[8 Bytes] + succ[8 Bytes] + block + padding + footer[4 Bytes]
static void* free_header(void* bp) {
    return bp - 2 * ADD_LEN - MIN_UNIT;
}
// an allocated block has a struct of:
// header[4 Bytes] + data + padding
static void* allocated_header(void* bp) {
    return bp - MIN_UNIT;
}
// only free block has footer
static void* get_footer(void* bp) {
    return bp + block_size(bp - 2 * ADD_LEN - MIN_UNIT) - 2 * ADD_LEN - MIN_UNIT - MIN_UNIT;
}

/* bit wise trick */

/* round down to log @param:size(2 based) */
static int high_bit(int val) {
    int i, bit = 0;
    for (i = 4; i >= 0; i--) {
        if (val & b[i]) {
            val >>= s[i];
            bit |= s[i];
        }
    }
    return bit;
}

/* determine if @param:val is power of 2 */
static int power_of_two(int val) {
    return !val && !(val & (val - 1));
}

static UI min(UI a, UI b) {
    return a <= b ? a : b;
}