/*
 * File: allocator.c
 * Author: David Xue
 * ----------------------
 * See readme.txt for details on allocator design and implementation. 
 * Functions are organized as follows:
 *      (1) Block Manipulation Functions 
 *              (e.g. get_hdr_size, get_curr_alloc, etc.)
 *              Functions that manipulate information within the blocks. Tries 
 *              to abstract details beyond a block base pointer.
 *      (2) Segregated List Functions 
 *              (e.g. first_find, get_bucket_num, etc.)
 *              Functions that maintain the segregated free lists. 
 *      (3) Allocator Functions 
 *              (e.g. mymalloc, myfree, myrealloc)
 *              Core allocator functions and some helpers.
 *      (4) Testing Functions 
 *              (e.g. validate_heap)
 *              Print functions that help validate the heap.
 *
 * Best Aggregate Statistics: 80% (utilization) 106% (throughput) 
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "allocator.h"
#include "segment.h"
#include "limits.h"

// Heap blocks are required to be aligned to 8-byte boundary
#define ALIGNMENT     8
#define PTR_SIZE      4
#define HDR_SIZE      4
#define FTR_SIZE      4
#define HDR_FTR_SIZE  8
#define MIN_BLK_SZ    12

// Initial Number of Pages and Lists
#define INIT_NPAGES   3
#define NBUCKETS      30

// Free is defined as 0, Allocated is defined as 1, 
#define FREE        0
#define ALLOC       1

// Multipliers and cutoff to work with...
#define REALLOC_MULT    1
#define BUCKET_CUTOFF   5
#define BEST_FIT_CUTOFF 15

// A\B Testing
#define BEST1_FIRST0    0

/* Private Global Variables */
static void **free_list[NBUCKETS];  //segregated free lists
static void *heap_start;            //start address of the heap segment




/**** **** ****         Block Manipulation Functions         **** **** ****/



/* Block Function: get_hdr_addr
 * ----------------------------
 * Get the address of the header when passed the address
 * of a block.
 */
static inline void *get_hdr_addr(void *bp)
{
    return (char *)(bp) - HDR_SIZE;
}

/* Block Helper Function: get
 * --------------------------
 * Helper function to manipulate the bits of an address.  
 */
static inline unsigned int get(void *p) 
{
    return *(unsigned int *)(p);
}

/* Block Function: get_size, set_size
 * ----------------------------------
 * Getters and setters for size of the block (stored in the 
 * header) when passed the address of a header or a footer. 
 */
static inline unsigned int get_size(void *p)
{
    return get(p) >> 2;
}

static inline void set_size(void *p, unsigned int size)
{
    *(unsigned int *)p = ((get(p) & 0x3) | (size << 2));
}

/* Block Function: get_hdr_addr, set_hdr_size
 * ------------------------------------------
 * Getters and setters for size of the block (stored in the 
 * header) when passed the base address of a block. 
 */
static inline unsigned int get_hdr_size(void *bp)
{
    return get_size(get_hdr_addr(bp));
}

static inline void set_hdr_size(void *bp, int size)
{
    set_size(get_hdr_addr(bp), size);
}

/* Block Function: get_curr_alloc, set_curr_alloc
 * ----------------------------------------------
 * Getters and setters for bit that stores if the current
 * block is allocated or free. Passed the base address of 
 * a block. 
 */
 static inline int get_curr_alloc(void *bp)
{
    return get(get_hdr_addr(bp)) & 0x1;
}

static inline void set_curr_alloc(void *bp, int curr_alloc) 
{
    void *hdr_addr = get_hdr_addr(bp);
    *(unsigned int *)hdr_addr = ((get(hdr_addr) & ~0x1) | curr_alloc);
}

/* Block Function: get_prev_alloc, set_prev_alloc
 * ----------------------------------------------
 * Getters and setters for bit that stores if the previous
 * block is allocated or free. Passed the base address of 
 * a block. 
 */
static inline int get_prev_alloc(void *bp)
{
    void *hdr_addr = get_hdr_addr(bp);
    return (get(hdr_addr) & 0x2) >> 1;
}

static inline void set_prev_alloc(void *bp, int prev_alloc)
{
    void *hdr_addr = get_hdr_addr(bp); 
    *(unsigned int *)hdr_addr = ((get(hdr_addr) & ~0x2) | (prev_alloc << 1));
}

/* Block Function: get_next_block, get_prev_block
 * ----------------------------------------------
 * Finds the address of the neighboring block in memory 
 * (next and previous, respectively). Passed the base address 
 * of the current block.  
 * NOTE: Get get_prev_block works only if the previous block 
 * is free, since only free blocks have headers. 
 */
static inline void *get_next_block(void *bp)
{
    return (char *)(bp) + get_hdr_size(bp) + HDR_SIZE;
}

static inline void *get_prev_block(void *bp) 
{
    void *ftr_addr = (char *)bp - HDR_FTR_SIZE; 
    return (char *)bp - HDR_SIZE - get_size(ftr_addr);
}

/* Block Function: get_next, set_next
 * ----------------------------------
 * Getters and setters for pointers to the next blocks
 * in the free list for a given free block. Passed the base 
 * address of a free block. 
 */
static inline void *get_next(void *bp)
{
    return *(void **)bp;
}

static inline void set_next(void *bp, void *next_bp)
{
    *(void **)bp = next_bp; 
}

/* Block Function: get_prev, set_prev
 * ----------------------------------
 * Getters and setters for pointers to the previous blocks
 * in the free list for a given free block. Passed the base 
 * address of a free block. 
 */
static inline void *get_prev(void *bp)
{
    return *(void **)((char *)bp + sizeof(void *));
}

static inline void set_prev(void *bp, void *prev_bp)
{
    *(void **)((char *)bp + sizeof(void *)) = prev_bp;
}

/* Block Function: write_header
 * ----------------------------
 * Writes a footer for a block. Writes the size and the 
 * allocation status of the current and previous blocks. Passed
 * the base address of a block.
 */
static inline void write_header(void *bp, int size, int curr_alloc, int prev_alloc) 
{
    set_hdr_size(bp, size);
    set_curr_alloc(bp, curr_alloc);
    set_prev_alloc(bp, prev_alloc);
}

/* Block Function: get_ftr_addr
 * ----------------------------
 * Get the address of the footer when passed the address
 * of a block.
 */
static inline void *get_ftr_addr(void *bp)
{
    return (char *)(bp) + get_hdr_size(bp) - FTR_SIZE;
}

/* Block Function: write_footer
 * ----------------------------
 * Writes a footer for a block by copying its header. Passed
 * the base address of a block. 
 * NOTE: Only copies whatever information is in the header. 
 */
static inline void write_footer(void *bp) 
{
    memcpy(get_ftr_addr(bp), get_hdr_addr(bp), FTR_SIZE);
}


/* Block Helper: roundup
 * ---------------------
 * Rounds up size to nearest multiple of given power of 2
 * does this by adding mult-1 to sz, then masking off the
 * the bottom bits, result is power of mult. 
 * NOTE: mult has to be power of 2 for this trick to work!
 */
static inline size_t roundup(size_t sz, int mult)
{
   return (sz + mult-1) & ~(mult-1);
}

/* Block Function: adjust_block_size
 * ---------------------------------
 * Adjusts the size of a request to find the associated size 
 * of a block given size and alignment constraints. The adjusted
 * size will always be at least the size of the request. The 
 * adjusted size cannot be smaller than 12 bytes, otherwise 
 * adjustedsz is a size of 12 + (8 * n), where n is a natural number. 
 */
static inline size_t adjust_block_size(size_t requestedsz)
{
    if (requestedsz <= MIN_BLK_SZ)  return MIN_BLK_SZ;
    else                            return roundup(requestedsz - 4, ALIGNMENT) + 4;
}

/**** **** ****         Segregated Free List Functions      **** **** ****/



/* Seglist Helper: get_bucket_num
 * ------------------------------
 * Uses the size of the block to determine bucket placement.
 * Returns a bucket number from 0 to 28.
 */
static inline int get_bucket_num(size_t size)
{    
    /*int leading_zeros = __builtin_clz(size);
    int bucket_num = ((NBUCKETS - 4 - leading_zeros) << 1) - 1;  
    int sig_bit_pos = 32 - leading_zeros - 1; 
    if (((size << 1) & (1 << sig_bit_pos)) != 0) {
        bucket_num += 1;
    }
    if (bucket_num > NBUCKETS - 1) bucket_num = NBUCKETS - 1;
    return bucket_num; */

    return NBUCKETS - __builtin_clz(size) - 2;  //number of leading 0's
}

/* Seglist Helper: first_fit
 * -------------------------
 * Searches for the first free block that is at least as large
 * as the target_size. Starts at the front of a corresponding 
 * bucket list, and continues down 
 */
void *first_fit(size_t target_size)
{
    // Searches through increasing buckets
    int bucket = get_bucket_num(target_size);
    for (int i = bucket; i < NBUCKETS; i++) {
        // Searches down the bucket list for a large enough block
        int n_blocks_examined = 0;
        for (void *curr = free_list[i]; curr != NULL; curr = get_next(curr)) {
            // Exit from this bucket early if not promising...
            if (n_blocks_examined == BUCKET_CUTOFF) break;
            n_blocks_examined++; 

            int curr_size = get_hdr_size(curr);
            if (curr_size >= target_size) return curr; //found a large enough block
        }
    }
    return NULL;    //no free blocks large enough found in any buckets
}


/* Seglist Helper: best_fit
 * -------------------------
 * Searches for the best block that is at least as large
 * as the target_size. Starts at the front of a corresponding 
 * bucket list, and continues down. If none found, continues
 * to the next bucket.
 */
void *best_fit(size_t target_size)
{
    int bucket = get_bucket_num(target_size);
    for (int i = bucket; i < NBUCKETS; i++) {
        // Searches down the bucket list for a large enough block
        int n_blocks_examined = 0;

        int smallest_diff = INT_MAX;
        void *best_fit_blk = NULL;
        for (void *curr = free_list[i]; curr != NULL; curr = get_next(curr)) {
            // Exit from this bucket early if not promising...
            if (n_blocks_examined == BEST_FIT_CUTOFF) break;
            n_blocks_examined++; 

            int curr_size = get_hdr_size(curr);
            int curr_diff = curr_size - target_size; 

            if (curr_diff >= 0 && curr_diff < smallest_diff) {
                smallest_diff = curr_diff; 
                best_fit_blk = curr; 
            }
        }
        if (best_fit_blk != NULL) return best_fit_blk;
    }
    return NULL;    //no free blocks large enough found in any buckets}
}

/* Seglist Function: insert_free_list
 * ----------------------------------
 * Inserts a free block at the front of its corresponding bucket
 * list (FILO). Rearranges pointers as necessary. 
 */
static inline void insert_free_list(void* free_block)
{   
    // Find the corresponding bucket and the first block (if any) of that bucket
    size_t size = get_hdr_size(free_block);
    int bucket_num = get_bucket_num(size);
    void *next_block = free_list[bucket_num];  

    // Set the next and prev pointers of the new block
    set_next(free_block, next_block);
    set_prev(free_block, &free_list[bucket_num]);

    // If list was non-empty, update its previous pointer
    if (next_block != NULL) set_prev(next_block, free_block);

    // Have the front of the free list point to the new block
    free_list[bucket_num] = free_block;    
}

/* Seglist Function: remove_free_list
 * ----------------------------------
 * Removes a free block from its current list and updates the pointers
 * of the previous and next blocks in the list to point to one another.
 */
static inline void remove_free_list(void *free_block)
{
    // Previous and next (if any) blocks in the free list
    void *prev_block = get_prev(free_block);
    void *next_block = get_next(free_block);

    // Have the next pointer of the previous block point to 
    // the next block (NULL if end of list). 
    set_next(prev_block, next_block);

    // If the free block is not at the end of the list, set 
    // previous pointer of the next block point to the previous block. 
    if (next_block != NULL) set_prev(next_block, prev_block);
}

/* Seglist Function: update_bucket
 * ----------------------------------
 * Switches a free block from its current bucket if it belong to 
 * a diffrent bucket by removing it from the current bucket list and 
 * re-inserting it into the correct bucket. 
 */
static inline void update_bucket(void *free_block, size_t old_size, size_t new_size)
{
    if (get_bucket_num(old_size) != get_bucket_num(new_size)) {
        remove_free_list(free_block);
        insert_free_list(free_block);
    }
}

/**** **** ****         Allocator Functions      **** **** ****/


/* Function: myinit
 * ----------------
 * Initalizes the heap segment to INIT_NPAGES pages, resets the array 
 * values of the segregated list, and creates a single contiguous 
 * free block. Formats with an epilogue header and inserts the free block 
 * into the free list. 
 */
bool myinit()
{
    // Initialize the Heap
    int npages = INIT_NPAGES; 
    heap_start = init_heap_segment(npages);
    if (heap_start == NULL) return false;       //unable to allocated segment

    // Reset Array Values of Segregated List
    memset(free_list, 0, sizeof(void **) * NBUCKETS);
    
    // Create Single Contiguous Free Block
    void* free_block = (char *)heap_start + ALIGNMENT; 
    write_header(free_block, (npages * PAGE_SIZE) - ALIGNMENT - HDR_SIZE, FREE, ALLOC);
    write_footer(free_block);

    // Insert into the free list
    insert_free_list(free_block);

    // Create Epilogue Header
    void *epilogue_hdr = get_next_block(free_block);
    write_header(epilogue_hdr, 0 , ALLOC, FREE);
    
    return true;
}

/* Block Function: split_block
 * ---------------------
 * Updates the fields in block to split it into two parts: 
 * a malloc'd block and a free block with the corresponding sizes.
 */
static inline void *split_block(void *block, size_t malloc_bytes, size_t free_bytes)
{
    /*// Set up the malloc'd block size and status
    set_hdr_size(block, malloc_bytes);
    set_curr_alloc(block, ALLOC);

    // Write Free Block 
    void *free_block = get_next_block(block);
    write_header(free_block, free_bytes, FREE, ALLOC);
    write_footer(free_block);
    insert_free_list(free_block);*/
    
    // Write Free Block 
    set_hdr_size(block, free_bytes);
    set_curr_alloc(block, FREE);
    write_footer(block);
    insert_free_list(block);

    // Set up the malloc'd block size and status    
    void *malloc_block = get_next_block(block);
    write_header(malloc_block, malloc_bytes, ALLOC, FREE);

    // Update Next Block
    void *next_block = get_next_block(malloc_block);
    set_prev_alloc(next_block, ALLOC);

    return malloc_block;
}


/* Function: mymalloc 
 * ------------------
 * Attempts to search for the first free block with enough size. 
 * If unsuccessful, requests additional pages and formats as free block. 
 * Then decides to allocate the entire page or split the page (adding 
 * the appropriate epilogue header). Returns malloc'd block.  
 */
void *mymalloc(size_t requestedsz)
{
    if (requestedsz == 0) return NULL;  //ignore spurious requests

    // Find first block with correct size
    size_t adjustedsz = adjust_block_size(requestedsz);
    
    // A/B Test whether to use first fit or best fit
    void *block; 
    if (BEST1_FIRST0 == 1) {
        block = best_fit(adjustedsz);
    } else {
        block = first_fit(adjustedsz);
    }

    // Request additional pages if no block found
    if (block == NULL) { // Requests new page(s) and extends heap
        int nbytes = roundup(adjustedsz, PAGE_SIZE);         //number of total bytes

        // Attempt to Extend Heap
        block = extend_heap_segment(nbytes / PAGE_SIZE);
        if (block == NULL) return NULL;

        // Format new page as a free block
        if (get_prev_alloc(block) == FREE) {
            // Merge with previous block
            void* prev_block = get_prev_block(block);
            size_t prev_size = get_hdr_size(prev_block); 
            size_t totalsz = prev_size + nbytes; 
            set_hdr_size(prev_block, totalsz);
            write_footer(prev_block);
            update_bucket(prev_block, prev_size, totalsz);        
            block = prev_block;
        } else {
            // Update old epilogue header
            set_hdr_size(block, nbytes - HDR_SIZE);
            set_curr_alloc(block, FREE);
            write_footer(block);
            insert_free_list(block);
        }

        // Write New Epilogue Header
        void *epilogue_hdr = get_next_block(block); 
        write_header(epilogue_hdr, 0 , ALLOC, FREE);
    }

    // Decide whole block allocation OR split the page
    int totalsz = get_hdr_size(block);
    int free_bytes = totalsz - adjustedsz - HDR_SIZE;    //bytes left for a free block
    if (free_bytes < MIN_BLK_SZ) { 
        // Whole Block Allocation
        set_curr_alloc(block, ALLOC);   //update Malloc'd Header
        set_prev_alloc(get_next_block(block), ALLOC);
        remove_free_list(block);        //remove from free list
    } else {
        // Split Block - Free and Malloc'd
        remove_free_list(block);        //remove from free list
        block = split_block(block, adjustedsz, free_bytes);
    }

    return block; 
}

/* Function: coalesce
 * ------------------
 * Frees a malloc'd block and checks the neighboring blocks
 * to check for possible coalescing. There are four cases:
 * 
 * Case 1: (AFA) Previous Allocated and Next Allocated
 *      No merging/coalescing.
 *      Free the current block.
 *      Set the prev_alloc of the next block to FREE.
 *      Insert current block into free list. 
 *
 * Case 2: (AFF) Previous Allocated and Next Free 
 *      Merge with the next block. 
 *      Free the current block.
 *      Update new size in current block.
 *      Remove next block from free list.
 *      Insert current block into free list.
 *
 * Case 3: (FFA) Previous Free and Next Allocated 
 *      Merge with the previous block.
 *      Update new size in previous block.
 *      Update the bucket for the previous block. 
 *      Set the prev_alloc of the next block to FREE.
 *
 * Case 4: (FFF) Previous Free and Next Free 
 *      Merge with the previous and next blocks.
 *      Update new size in previous block.
 *      Update the bucket for the previous block. 
 *      Removes next block from free list. 
 * 
 * Returns pointer to coalesced free block. 
 */
static inline void *coalesce(void *curr_block)
{
    void *result = NULL; 
    void *next_block = get_next_block(curr_block);

    bool prev_alloc = (get_prev_alloc(curr_block) == ALLOC);
    bool next_alloc = (get_curr_alloc(next_block) == ALLOC);

    size_t curr_size = get_hdr_size(curr_block);
    size_t next_size = get_hdr_size(next_block);

    if (prev_alloc && next_alloc) {             /* Case 1: Do Nothing */
        set_curr_alloc(curr_block, FREE);
        write_footer(curr_block);
        set_prev_alloc(next_block, FREE);
        insert_free_list(curr_block);
        result = curr_block;
    } else if (prev_alloc && !next_alloc) {     /* Case 2: Merge with Next */
        size_t new_size = curr_size + next_size + HDR_SIZE;
        set_hdr_size(curr_block, new_size);
        set_curr_alloc(curr_block, FREE);
        write_footer(curr_block);
        insert_free_list(curr_block);
        remove_free_list(next_block);       //remove the next block from list
        result = curr_block;
    } else if (!prev_alloc && next_alloc) {     /* Case 3: Merge with Prev */
        void* prev_block = get_prev_block(curr_block);
        size_t prev_size = get_hdr_size(prev_block); 
        size_t new_size = prev_size + curr_size + HDR_SIZE; 
        set_hdr_size(prev_block, new_size);
        write_footer(prev_block);
        update_bucket(prev_block, prev_size, new_size);        
        set_prev_alloc(next_block, FREE);
        result = prev_block;
    } else if (!prev_alloc && !next_alloc) {    /* Case 4: Merge with Both */
        void *prev_block = get_prev_block(curr_block);
        size_t prev_size = get_hdr_size(prev_block); 
        size_t new_size = prev_size + curr_size + next_size + 2 * HDR_SIZE; 
        set_hdr_size(prev_block, new_size);
        write_footer(prev_block);
        update_bucket(prev_block, prev_size, new_size);
        remove_free_list(next_block);           //remove the next block from list
        result = prev_block;
    }

    return result;
}


/* Function: myfree 
 * ----------------
 * Frees the malloc'd pointer and attempts to coalesce with 
 * neighboring blocks. 
 */
void myfree(void *ptr)
{
    if (ptr == NULL) return;
    coalesce(ptr);
}

/* Function: myrealloc 
 * -------------------
 * Reallocates the oldptr by checking if it is possible to reuse 
 * the block (if so, return oldptr). Next checks if it is possible 
 * to coalesce with the next block (if so, coalesces with next 
 * block and reuses pointer). Otherwise, malloc a new block
 * and free the old pointer. 
 */
void *myrealloc(void *oldptr, size_t newsz)
{   
    // If oldptr == NULL, equivalent to mymalloc(newsz)
    if (oldptr == NULL) {
        return mymalloc(newsz);
    }
    // If newsz == 0, and oldptr != NULL, free(oldptr)
    if (newsz == 0) {
        free(oldptr);
        return NULL;
    }

    size_t oldsz = get_hdr_size(oldptr);
    if (adjust_block_size(newsz) < oldsz) { //try to reuse block
        return oldptr;
    } else { //try to see if merging with next free block is worthwhile
        void *next_block = get_next_block(oldptr); 
        if (get_curr_alloc(next_block) == FREE) {
            size_t combinedsz = oldsz + get_hdr_size(next_block) + HDR_SIZE;
            if (adjust_block_size(newsz) < combinedsz) {
                set_prev_alloc(get_next_block(next_block), ALLOC);
                set_hdr_size(oldptr, combinedsz);
                write_footer(oldptr);
                remove_free_list(next_block);       //remove the next block from list
                return oldptr;
            }
        }
    }

    // Malloc a new block
    void *newptr = mymalloc(newsz * REALLOC_MULT);
    if (newptr == NULL) return NULL; 
    memcpy(newptr, oldptr, oldsz < newsz ? oldsz: newsz);
    myfree(oldptr);
    return newptr;
}

/**** **** ****         Testing Functions      **** **** ****/

void print_bucket_count()
{
    /*printf("{");
    for (int i = 0; i < NBUCKETS; i++) {
        int count = 0;        
        for (void *curr_free = free_list[i]; curr_free != NULL; curr_free = get_next(curr_free)) {
            if (curr_free != NULL) {
                count++;
            } 
        }

        printf("%d", count);
        if (i != NBUCKETS - 1) printf(", ");
    }
    printf("}\n");*/
}

void print_free_lists()
{
    /*for (int i = 0; i < NBUCKETS; i++) {
        void *curr_free = free_list[i]; 
        int block_count = 0;

        if (curr_free != NULL) {
            while (curr_free != NULL) {   
                int size = get_hdr_size(curr_free);
                int curr_alloc = get_curr_alloc(curr_free);
                //int prev_alloc = get_prev_alloc(curr_free);

                char *curr_str;

                if (curr_alloc == FREE) curr_str = "Free";
                else curr_str = "Allocated";

                printf("\n Free [%d] #%d (%s) - Size: %d Bytes - %#08x", i, block_count, curr_str, size, (unsigned int) curr_free);

                block_count++;
                curr_free = get_next(curr_free);
            }

            printf("\n----------------------------------------------\n");
        }
    }

    printf("\n\n---------------------------------------------------------------------------\n");
    printf("\n---------------------------------------------------------------------------\n\n");*/
}

void print_entire_heap()
{
    /*void *curr_block = (char *)heap_start + HDR_FTR_SIZE; 
    int block_counter = 0;
    printf("Number of Pages: %d\n", heap_segment_size() / PAGE_SIZE);
    while (true) {
        int size = get_hdr_size(curr_block);
        int curr_alloc = get_curr_alloc(curr_block);
        int prev_alloc = get_prev_alloc(curr_block);

        char *curr_str;
        char *prev_str; 

        if (curr_alloc == FREE) curr_str = "Free";
        else curr_str = "Allocated";

        if (prev_alloc == FREE) prev_str = "Free";
        else prev_str = "Allocated";

        printf("\nBlock #%d (%s): Size: %d Bytes - Previous (%s)", block_counter, curr_str, size, prev_str);

        // Examine Bytes
        //int num_bytes = (size + HDR_SIZE) / 4;
        // int count = 0;
        //for (int i = 0; i < num_bytes; i++) {
        //    void *current_byte = (char *)curr_block - HDR_SIZE + (i * 4);
        //    printf("%#08x ", get(current_byte));                           
        //    if (count == 7)  {
        //        printf("\n");
        //        count = 0;  
        //    }                                                      
        //    count++;        
        //}

        //printf("\n----------------------------------------------\n");
        
        curr_block = get_next_block(curr_block);
        block_counter++; 

        if (size == 0) break;
    }

    //printf("\n\n---------------------------------------------------------------------------\n");
    printf("\n---------------------------------------------------------------------------\n"); */
}


/* Function: validate_heap 
 * -------------------
 * My validate heap function doesn't "validate" (sorry!) the heap so much as 
 * it has allowed me to examine the contents of the heap and manually check that
 * my segregated bucket lists, free lists, and bytes of the heap are
 * what I expect them to be when I print them out (and find out what 
 * went wrong when it does). It has proved pretty invaluable. 
 * 
 */
bool validate_heap()
{ 
    // Check the get_bucket_num function for various sizes.
    /*for (int i = 12; i <= 10000000; i++) {
        printf("Size: %d -> Bucket: %d\n", i, get_bucket_num(i));
    }*/

    //print_bucket_count();
    //print_free_lists();
    //print_entire_heap();

    return true;
}