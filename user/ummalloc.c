#include "kernel/types.h"
#include "user/user.h"
#include "ummalloc.h"
#include <stddef.h>

#define W_SIZE    4
#define D_SIZE    8
#define E_SIZE    (1<<12)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc)    ((size) | (alloc))
#define GET(p)        (*(unsigned int *)(p))
#define PUT(p, val)    (*(unsigned int *)(p) = val)
#define GET_SIZE(p)        (GET(p) & ~0x7)
#define GET_ALLOC(p)    (GET(p) & 0x1)

#define HD(bp)    ((char* )(bp) - W_SIZE)
#define FT(bp)    ((char* )(bp) + GET_SIZE(HD(bp)) - D_SIZE)

#define NEXT_PLACE(bp)    ((char *)(bp) + GET_SIZE(((char *)(bp) - W_SIZE)))
#define PREV_PLACE(bp)    ((char *)(bp) - GET_SIZE(((char *)(bp) - D_SIZE)))

static void *heap_head;

static void *coalesce(void *ptr) {
    uint prev_alloc = GET_ALLOC(FT(PREV_PLACE(ptr)));
    uint next_alloc = GET_ALLOC(HD(NEXT_PLACE(ptr)));
    uint size = GET_SIZE(HD(ptr));
    if (prev_alloc && next_alloc) {
        return ptr;
    } else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HD(NEXT_PLACE(ptr)));
        PUT(HD(ptr), PACK(size, 0));
        PUT(FT(ptr), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(FT(PREV_PLACE(ptr)));
        PUT(FT(ptr), PACK(size, 0));
        PUT(HD(PREV_PLACE(ptr)), PACK(size, 0));
        ptr = PREV_PLACE(ptr);
    } else if (!prev_alloc && !next_alloc) {
        size += GET_SIZE(HD(PREV_PLACE(ptr)))
                + GET_SIZE(FT(NEXT_PLACE(ptr)));
        PUT(HD(PREV_PLACE(ptr)), PACK(size, 0));
        PUT(FT(NEXT_PLACE(ptr)), PACK(size, 0));
        ptr = PREV_PLACE(ptr);
    }
    return ptr;
}

static void *extend_heap(uint words) {
    char *bp;
    uint size;
    size = (words % 2) ? (words + 1) * W_SIZE : words * W_SIZE;
    if ((long) (bp = sbrk(size)) == -1)
        return NULL;
    PUT(HD(bp), PACK(size, 0));
    PUT(FT(bp), PACK(size, 0));
    PUT(HD(NEXT_PLACE(bp)), PACK(0, 1));
    return coalesce(bp);
}

int mm_init(void) {
    if ((heap_head = sbrk(4 * W_SIZE)) == (void *) -1)
        return -1;
    PUT(heap_head, 0);
    PUT(heap_head + (1 * W_SIZE), PACK(D_SIZE, 1));
    PUT(heap_head + (2 * W_SIZE), PACK(D_SIZE, 1));
    PUT(heap_head + (3 * W_SIZE), PACK(0, 1));
    heap_head += (2 * W_SIZE);
    if (extend_heap(E_SIZE / W_SIZE) == NULL) {
        return -1;
    }
    return 0;
}

static void place(void *bp, uint a_size) {
    uint size = GET_SIZE(HD(bp));
    if (size - a_size >= 2 * W_SIZE) {
        PUT(HD(bp), PACK(a_size, 1));
        PUT(FT(bp), PACK(a_size, 1));
        bp = NEXT_PLACE(bp);
        PUT(HD(bp), PACK(size - a_size, 0));
        PUT(FT(bp), PACK(size - a_size, 0));
    } else {
        PUT(HD(bp), PACK(size, 1));
        PUT(FT(bp), PACK(size, 1));
    }
}

static void *find_first(uint a_size) {
    void *p;
    for (p = heap_head; GET_SIZE(HD(p)) > 0; p = NEXT_PLACE(p)) {
        if (!GET_ALLOC(HD(p)) && (a_size <= GET_SIZE(HD(p))))
            return p;
    }
    return NULL;
}

void *mm_malloc(uint size) {
    uint a_size;        /* Adjusted block size */
    uint extend_size;    /* Amount to extend heap if no fit */
    char *bp;
    if (size == 0)
        return NULL;
    if (size <= D_SIZE)
        a_size = 2 * D_SIZE;
    else
        a_size = D_SIZE * ((size + (D_SIZE) + (D_SIZE - 1)) / D_SIZE);
    if ((bp = find_first(a_size)) != NULL) {
        place(bp, a_size);
        return bp;
    }
    extend_size = MAX(a_size, E_SIZE);
    if ((bp = extend_heap(extend_size / W_SIZE)) == NULL)
        return NULL;
    place(bp, a_size);
    return bp;
}

void mm_free(void *ptr) {
    uint size = GET_SIZE(HD(ptr));
    PUT(HD(ptr), PACK(size, 0));
    PUT(FT(ptr), PACK(size, 0));
    coalesce(ptr);
}

void *mm_realloc(void *ptr, uint size) {
    void *old_ptr = ptr;
    void *new_ptr;
    void *next_ptr;
    uint a_size;
    uint extend_size;
    uint size_sum;
    uint block_size;
    if (ptr == NULL) {
        return mm_malloc(size);
    } else if (size == 0) {
        mm_free(ptr);
        return NULL;
    }
    if (size <= D_SIZE)
        a_size = 2 * D_SIZE;
    else
        a_size = D_SIZE * ((size + (D_SIZE) + (D_SIZE - 1)) / D_SIZE);
    block_size = GET_SIZE(HD(ptr));
    if (a_size == block_size) {
        return ptr;
    } else if (a_size < block_size) {
        place(ptr, a_size);
        return ptr;
    } else {
        next_ptr = NEXT_PLACE(ptr);
        size_sum = GET_SIZE(HD(next_ptr)) + block_size;
        if (!GET_ALLOC(HD(next_ptr)) && size_sum >= a_size) {
            PUT(HD(ptr), PACK(size_sum, 0));
            place(ptr, a_size);
            return ptr;
        } else {
            new_ptr = find_first(a_size);
            if (new_ptr == NULL) {
                extend_size = MAX(a_size, E_SIZE);
                if ((new_ptr = extend_heap(extend_size / W_SIZE)) == NULL)
                    return NULL;
            }
            place(new_ptr, a_size);
            memcpy(new_ptr, old_ptr, block_size - 2 * W_SIZE);
            mm_free(old_ptr);
            return new_ptr;
        }
    }

}

