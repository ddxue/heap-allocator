NAME(s) 
<Be sure to include real name & sunet username for you and partner (if any)>
David Xue <dxue@stanford.edu> 
SUNet Username: dxue

--------------------------------------------------------------------------------------------

DESIGN 
<Give an overview of your allocator implementation (data structures/algorithms)>

Block Design:
    Below is the structure of a single free block and the header of the next block. The 
    footer is an exact copy of the header. The structure of an allocated block is the 
    same, but there is no footer and the pointers are not used. 

    The size field is the size of the payload in an allocated block. Note that the size 
    field includes the footer because an allocated block does not use a footer. 
        Minimum Block Size = 12 bits (next & prev pointers and footer = 12 bits) 
        Valid Block Sizes = 8 * i + 4, where i is an integer greater than or equal to 1

            ------------------------------------------------- 
            |                       |   Prev     |  Curr    |   Header Layout
            |    Size (30 bits)     |   Alloc    |  Alloc   |   (4 Bytes - 32 Bits)
            |                       |   (1 Bit)  |  (1 Bit) |   
bp ------>  -------------------------------------------------  -  -  -  8 Byte Alignment
   /|\      |                                               |
    |       |                 Next Pointer                  |   
    |       |                  (4 Bytes)                    |
    |       -------------------------------------------------
    |       |                                               |
    |       |                 Prev Pointer                  |
    |       |                  (4 Bytes)                    |
    |       -------------------------------------------------  -  -  -  8 Byte Alignment
  Size      |                                               |
(payload in |                                               |
an allocated|                     ...                       |
  block)    |                                               |
    |       |                                               |
    |       |                                               |
    |       ------------------------------------------------- -  -  -   8 Byte Alignment
    |       |                       |   Prev     |  Curr    |   Footer Layout
    |       |    Size (30 bits)     |   Alloc    |  Alloc   |   (4 Bytes - 32 Bits)
   \|/      |                       |   (1 Bit)  |  (1 Bit) |   
   ---      -------------------------------------------------
            |                                               |
            |               Next Block Header               |
            |                                               |
            ------------------------------------------------- -  -  -   8 Byte Alignment

Managing Free Blocks: Segregated Lists
    Free blocks were managed in an array of segregated lists (doubly linked lists) of 
    30 buckets. Each bucket corresponded to a specific size grouping (the range of each 
    subsequent bucket is a power of two larger).

Searching for Free Blocks: First Fit
    Searching for free blocks was performed using first fit. The search would start at the 
    corresponding size-grouped bucket, search down the list, and continue onto the next
    bucket if no fits were found. 

Overview of mymalloc, myfree, myrealloc: 
    mymalloc: 
        Employs first fit to search for free block. If not found, extends the 
        heap and formats as new free block. Then decides to split the free block 
        or allocate the whole free block. 
    myfree: 
        A coalescing strategy is employed for the myfree function.   
    myrealloc: 
        Checks if it is possible to reuse the block (if so, reuses). 
        Then checks if it is possible to coalesce with the next block
        (if so, coalesces with next block and reuses pointer). Otherwise, 
        malloc a new block and free the old pointer.

--------------------------------------------------------------------------------------------

RATIONALE 
<Provide rationale for why you made your design choices>

Block Design
    A given block header, as 9.9 B&0 suggests for coalescing, needs to store:
        (1) whether the current block is allocated (1 bit),
        (2) whether the previous block is allocated (1 bit), for coalescing purposes,
        (3) size of the block (remaining 30 bits)
        I formatted the header to accomodate this information. 
    And as B&O suggests, footers are not necesary in allocated blocks (squeezing a 
    few additional bits for payload). Pointers allow the maintenance of an implicit 
    free list. This led to larger minimum blocks, unfortunately.

Segregated Free Lists & First Fit
    Best fit was tried, but it was not as effective as first fit with segregated 
    lists (which approximates best first), particularly since deciding the bucket
    number can use a built-in gcc function (builtin_clz). Best fit was not worth
    the utilization due to the high cost of throughput and marginal benefit. It 
    turns out the first fit with segregated lists is very closer to best fit
    utilization, with much better throughput. In short, segregated lists were a 
    win-win.

Doubly Linked List
    I decided to use a doubly linked list for convenience in inserting and 
    removing free blocks. Due to alignment issues and the need for a footer for
    coalescing, I might as well have a previous pointer in addition to a next 
    pointer. 

--------------------------------------------------------------------------------------------

OPTIMIZATION 
<Describe how you optimized-- tools/strategies, improvements, etc>

After having a working heap allocator, I turned on -03 optimization (after -02) to 
increase throughput significantly (from 25% to 76%) as all my inlined functions are
actually inlined now.
    Overall utilization = 72%
    Overall throughput = 76% (+51%)

Adding segregated lists increased throughput from 76% to 91% and utilization from 72% 
to 74%, since first fit now approximates best fit. 
    Overall utilization = 74% (+2%)
    Overall throughput = 83% (+7%)

I then split realloc into three cases:
    (1) reuse old block
    (2) merge with next block and reuse old block
    (3) malloc new block and free old block
These changes improved the utilization of scripts that called realloc (e.g. 
realloc-pattern-2 from 23% to 54%). 
    Overall utilization = 76% (+4%)
    Overall throughput = 91% (+8%)

But the most surprising improvements only came after I changed the design of split block 
from allocating the first part then and leaving the second part free to having the 
first part free and the second part allocated (I tried both). It increased realloc pattern
performance significantly.
    Overall utilization = 80% (+4%) 
    Overall throughput = 102% (+11%)

I ran callgrind to find the bottleneck in the first fit double loop, and used a cutoff to
stop searching down a bucket and move onto the next bucket early. This sacrificed some
utilization for throughput.
    Overall utilization = 80% 
    Overall throughput = 106% (+4%)

Failures: 
    (1) I tried manipulating the size of the realloc in the third case (i.e. malloc
    new block and free old block) to malloc a larger block to anticipate future 
    reallocs. Didn't raise performance.
    (2) Tried starting off with extra initial pages (figuring less need to extend). 
    Didn't really help (lowered coalesce-pattern utilization significantly).
    (3) I couldn't figure out how to increase utilization for coalesce from 66%. It 
    allocates three pages... and fragmentation can't keep it down to two. 
    (4) For the segregated lists, I  tried other bucket groupings besides powers of 
    two (better refinement, e.g. 16 to 23 and 23 to 31 instead of 16 to 31) to use 
    more buckets, since in most cases only the first half of the buckets are used. 
    But throughput decreased significantly instead of increasing. The built-in gcc 
    function is very efficient, and penalized moving away from straightforward powers 
    of two in bucket groupings. 
    (5) I tried A/B testing with first fit and best fit, and found that first fit 
    was optimal.

This allocator has weakest on utilization on two scripts:
    reassemble-trace: 25% U, 106% T
    merge-pattern: 37% U, 246 T

reassemble-trace allocates several small blocks, then frees a few blocks while 
allocating blocks that are just slightly larger than the ones just free'd. This 
means that a new page is extended, instead of being able to use the just free'd 
blocks, creating significant fragmentation. I could not see how to avoid this 
fragmentation without harming performance on other scripts.

merge-pattern fares poorly because of my large minimum block size of 12. It 
allocates many, many small blocks of 2-3 bits, then frees all of them, and 
allocates many slightly larger blocks, and frees all of those, etc. The problem
is that the original blocks allocated have so much extra space; only one-sixth 
or one quarter is used. I cannot see how to avoid this; the pointers and 
footers are needed. 

--------------------------------------------------------------------------------------------

EVALUATION 
<Give strengths/weaknesses of your final version>

Weaknesses: 

    This allocator is vulnerable to internal fragmentation when several small blocks 
    under the minimum block size are allocated (such as in merge-pattern). The large 
    minimum block size can damange performance on scripts similar to merge-pattern.

    Also, significant fragmentation occurs when smaller recently free'd blocks are never 
    reused and future allocations are always larger (e.g. scripts  similar to  
    reassemble-trace). 

Strengthes:

    This allocator has strong performance when there is an opportunity for coalescing or merging. 
    Throughput is maximized when realloc is not called too often (particularly randomly, 
    when it forces a new malloc and a free). The key is that utilization is typically high
    when free blocks can be coalesced and reused. 

    Realloc has particularly high utilization when the new size is smaller then the old size.

    When there is a wide range of free block sizes, it leads to higher throughput due to 
    faster searching for blocks (each list is short). Also, on block sizes larger than the 
    minimum this allocator has generally high utilization performance.  

--------------------------------------------------------------------------------------------

REFERENCES
<If any external resources (books, websites, people) were influential in shaping your design, 
they should be properly cited here>

I used many ideas (segregated lists, header formatting, boundary tags, coalescing) 
from 9.9 of Computer Systems by B&0. I did not directly get any code from that 
book (besides the GET macro to manipulate the bits in the header), but essentially my
allocator is an optimized version of their allocator.
