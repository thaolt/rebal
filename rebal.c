#include "rebal.h"

/* -------------------- Helpers (no libc) -------------------- */

static void zero_bytes(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; ++i) b[i] = 0;
}

static size_t align_up(size_t x, size_t a) {
    size_t m = a - 1;
    return (x + m) & ~m;
}

static inline rebal_t *alloc_from_buf(void *buf) {
    return (rebal_t *)buf;
}
static inline void *ptr_from_off(void *base, rebal_offset_t off) {
    if (off == 0) return NULL;
    return (void *)((uintptr_t)base + (uintptr_t)off);
}

static inline rebal_offset_t off_from_ptr(void *base, void *p) {
    if (p == NULL) return 0;
    return (rebal_offset_t)((uintptr_t)p - (uintptr_t)base);
}

/* Convenience: header pointer from allocator and offset */
static inline rebal_block_header_t *hdr(rebal_t *a, rebal_offset_t off) {
    if (off == 0) return NULL;
    return (rebal_block_header_t *)((uintptr_t)a + (uintptr_t)off);
}
static inline rebal_offset_t off_of(rebal_t *a, rebal_block_header_t *b) {
    if (!b) return 0;
    return (rebal_offset_t)((uintptr_t)b - (uintptr_t)a);
}

/* -------------------- Allocator Init -------------------- */

#define MIN_OVERHEAD (sizeof(rebal_t) + sizeof(rebal_block_header_t))

int rebal_init(void *buffer, size_t buffer_size) {
    if (buffer == NULL) return -1;
    if (buffer_size < MIN_OVERHEAD) return -2;

    rebal_t *a = alloc_from_buf(buffer);
    zero_bytes(a, sizeof(rebal_t));

    a->magic = REBAL_MAGIC;
    a->capacity = (uint32_t)buffer_size;
    a->free_root = 0;
    a->first_block = 0;

    uintptr_t base = (uintptr_t)buffer;
    uintptr_t block_start = base + sizeof(rebal_t);
    size_t offset_into_buf = (size_t)(block_start - base);
    size_t aligned_offset = (size_t)align_up(offset_into_buf, REBAL_MIN_ALIGN);
    block_start = base + aligned_offset;

    if ((uintptr_t)buffer + buffer_size <= block_start + sizeof(rebal_block_header_t)) {
        return -3;
    }

    rebal_block_header_t *b = (rebal_block_header_t *)block_start;
    zero_bytes((void *)b, sizeof(rebal_block_header_t));

    uint32_t block_total_size = (uint32_t)(((uintptr_t)buffer + buffer_size) - block_start);
    b->size = block_total_size;
    b->is_free = 1;
    b->color = 0; /* black by default when inserted to RB as root */
    b->left_off = b->right_off = b->parent_off = 0;
    b->prev_phys_off = 0;
    b->next_phys_off = 0;

    rebal_offset_t boff = (rebal_offset_t)(block_start - base);
    a->first_block = boff;
    a->free_root = boff;
    /* ensure root is black - it already is (color=0) */

    return 0;
}

/* -------------------- Red-Black Tree Operations -------------------- */

/* color constants */
#define REBAL_RED 1
#define REBAL_BLACK 0

/* helpers to access root quickly */
static inline rebal_block_header_t *rb_root(rebal_t *a) {
    return hdr(a, a->free_root);
}
static inline void rb_set_root(rebal_t *a, rebal_block_header_t *r) {
    a->free_root = off_of(a, r);
}

/* Left rotate at node x
 *
 *    x                 y
 *     \               / \
 *      y    -->      x   yr
 *     / \           / \
 *    yl yr         xl yl
 */
static void rb_left_rotate(rebal_t *a, rebal_block_header_t *x) {
    rebal_block_header_t *y = hdr(a, x->right_off);
    if (!y) return;

    x->right_off = y->left_off;
    if (y->left_off) hdr(a, y->left_off)->parent_off = off_of(a, x);

    y->parent_off = x->parent_off;

    if (x->parent_off == 0) {
        /* x was root */
        rb_set_root(a, y);
    } else {
        rebal_block_header_t *xp = hdr(a, x->parent_off);
        if (xp->left_off == off_of(a, x)) xp->left_off = off_of(a, y);
        else xp->right_off = off_of(a, y);
    }

    y->left_off = off_of(a, x);
    x->parent_off = off_of(a, y);
}

/* Right rotate at node x
 *
 *      x              y
 *     / \            / \
 *    y  xr   -->    yl  x
 *   / \                / \
 *  yl yr              yr xr
 */
static void rb_right_rotate(rebal_t *a, rebal_block_header_t *x) {
    rebal_block_header_t *y = hdr(a, x->left_off);
    if (!y) return;

    x->left_off = y->right_off;
    if (y->right_off) hdr(a, y->right_off)->parent_off = off_of(a, x);

    y->parent_off = x->parent_off;

    if (x->parent_off == 0) {
        rb_set_root(a, y);
    } else {
        rebal_block_header_t *xp = hdr(a, x->parent_off);
        if (xp->left_off == off_of(a, x)) xp->left_off = off_of(a, y);
        else xp->right_off = off_of(a, y);
    }

    y->right_off = off_of(a, x);
    x->parent_off = off_of(a, y);
}

/* Standard RB insert fixup
 * Assumes node->color == RED and node is inserted as a leaf.
 */
static void rb_insert_fixup(rebal_t *a, rebal_block_header_t *node) {
    while (node->parent_off != 0 && hdr(a, node->parent_off)->color == REBAL_RED) {
        rebal_block_header_t *parent = hdr(a, node->parent_off);
        rebal_block_header_t *g = hdr(a, parent->parent_off);
        if (!g) break;

        if (parent == hdr(a, g->left_off)) {
            rebal_block_header_t *uncle = hdr(a, g->right_off);
            if (uncle && uncle->color == REBAL_RED) {
                /* case 1 */
                parent->color = REBAL_BLACK;
                uncle->color = REBAL_BLACK;
                g->color = REBAL_RED;
                node = g;
            } else {
                if (node == hdr(a, parent->right_off)) {
                    /* case 2: convert to case 3 */
                    node = parent;
                    rb_left_rotate(a, node);
                    parent = hdr(a, node->parent_off);
                    g = hdr(a, parent->parent_off);
                }
                /* case 3 */
                parent->color = REBAL_BLACK;
                if (g) {
                    g->color = REBAL_RED;
                    rb_right_rotate(a, g);
                }
            }
        } else {
            /* parent is right child */
            rebal_block_header_t *uncle = hdr(a, g->left_off);
            if (uncle && uncle->color == REBAL_RED) {
                parent->color = REBAL_BLACK;
                uncle->color = REBAL_BLACK;
                g->color = REBAL_RED;
                node = g;
            } else {
                if (node == hdr(a, parent->left_off)) {
                    node = parent;
                    rb_right_rotate(a, node);
                    parent = hdr(a, node->parent_off);
                    g = hdr(a, parent->parent_off);
                }
                parent->color = REBAL_BLACK;
                if (g) {
                    g->color = REBAL_RED;
                    rb_left_rotate(a, g);
                }
            }
        }
    }
    /* ensure root is black */
    rebal_block_header_t *r = rb_root(a);
    if (r) r->color = REBAL_BLACK;
}

/* RB insertion by size key. If same size, tie-break by address (offset) to keep deterministic order. */
static void rb_insert(rebal_t *a, rebal_block_header_t *z) {
    z->left_off = z->right_off = z->parent_off = 0;
    z->color = REBAL_RED; /* new node red */

    if (a->free_root == 0) {
        /* empty tree */
        a->free_root = off_of(a, z);
        z->color = REBAL_BLACK;
        return;
    }

    rebal_block_header_t *y = NULL;
    rebal_block_header_t *x = rb_root(a);

    /* find insert location */
    while (x) {
        y = x;
        if (z->size < x->size) x = hdr(a, x->left_off);
        else if (z->size > x->size) x = hdr(a, x->right_off);
        else {
            /* tie-break by offset (address) to ensure deterministic ordering */
            if (off_of(a, z) < off_of(a, x)) x = hdr(a, x->left_off);
            else x = hdr(a, x->right_off);
        }
    }

    z->parent_off = (y ? off_of(a, y) : 0);
    if (off_of(a, z) < off_of(a, y)) y->left_off = off_of(a, z);
    else y->right_off = off_of(a, z);

    rb_insert_fixup(a, z);
}

/* Transplant u with v in tree (u may be root). v can be NULL. */
static void rb_transplant(rebal_t *a, rebal_block_header_t *u, rebal_block_header_t *v) {
    if (u->parent_off == 0) {
        a->free_root = off_of(a, v);
    } else {
        rebal_block_header_t *p = hdr(a, u->parent_off);
        if (p->left_off == off_of(a, u)) p->left_off = off_of(a, v);
        else p->right_off = off_of(a, v);
    }
    if (v) v->parent_off = u->parent_off;
}

/* Find minimum node under subtree rooted at n */
static rebal_block_header_t *rb_minimum(rebal_t *a, rebal_block_header_t *n) {
    while (n && n->left_off) n = hdr(a, n->left_off);
    return n;
}

/* Delete node z from RB tree and fixup.
 * This follows the CLRS algorithm; we must be careful with offsets and NULLs.
 */
static void rb_delete(rebal_t *a, rebal_block_header_t *z) {
    rebal_block_header_t *y = z;
    uint8_t y_original_color = y->color;
    rebal_block_header_t *x = NULL;

    if (z->left_off == 0) {
        x = hdr(a, z->right_off);
        rb_transplant(a, z, x);
    } else if (z->right_off == 0) {
        x = hdr(a, z->left_off);
        rb_transplant(a, z, x);
    } else {
        y = rb_minimum(a, hdr(a, z->right_off));
        y_original_color = y->color;
        x = hdr(a, y->right_off);

        if (y->parent_off == off_of(a, z)) {
            if (x) x->parent_off = off_of(a, y);
        } else {
            rb_transplant(a, y, x);
            y->right_off = z->right_off;
            if (y->right_off) hdr(a, y->right_off)->parent_off = off_of(a, y);
        }
        rb_transplant(a, z, y);
        y->left_off = z->left_off;
        if (y->left_off) hdr(a, y->left_off)->parent_off = off_of(a, y);
        y->color = z->color;
    }

    /* Fixup if original color was BLACK */
    if (y_original_color == REBAL_BLACK) {
        /* x may be NULL (represented by x == NULL pointer).
         * We'll implement fixup relying on x and its parent pointers (parent available via z's parent chain).
         */
        rebal_block_header_t *xi = x;
        while ((xi == NULL || xi->color == REBAL_BLACK) && xi != rb_root(a)) {
            rebal_block_header_t *xp = NULL;
            if (xi) xp = hdr(a, xi->parent_off);
            else {
                /* xi is NULL; its parent is the node that used to point to xi.
                 * Hard to find directly: We can recover parent from y/transplant logic, but to avoid complexity,
                 * we will compute parent as follows:
                 *   - if z had parent, after transplant operations, the parent of x is set appropriately.
                 *   - But when xi==NULL, finding xp needs to use the last known parent (y->parent_off or z->parent_off).
                 * To simplify and be robust, handle xi==NULL by using z->parent_off or y->parent_off.
                 */
                xp = hdr(a, y->parent_off); /* may be NULL */
            }

            if (!xp) break; /* reached root */
            if (xp->left_off == off_of(a, xi)) {
                rebal_block_header_t *w = hdr(a, xp->right_off);
                if (w && w->color == REBAL_RED) {
                    w->color = REBAL_BLACK;
                    xp->color = REBAL_RED;
                    rb_left_rotate(a, xp);
                    w = hdr(a, xp->right_off);
                }
                if ((w == NULL) ||
                    ((w->left_off? hdr(a, w->left_off)->color : REBAL_BLACK) == REBAL_BLACK &&
                     (w->right_off? hdr(a, w->right_off)->color : REBAL_BLACK) == REBAL_BLACK)) {
                    if (w) w->color = REBAL_RED;
                    xi = xp;
                } else {
                    if ((w->right_off? hdr(a, w->right_off)->color : REBAL_BLACK) == REBAL_BLACK) {
                        if (w->left_off) hdr(a, w->left_off)->color = REBAL_BLACK;
                        w->color = REBAL_RED;
                        rb_right_rotate(a, w);
                        w = hdr(a, xp->right_off);
                    }
                    if (w) w->color = xp->color;
                    xp->color = REBAL_BLACK;
                    if (w && w->right_off) hdr(a, w->right_off)->color = REBAL_BLACK;
                    rb_left_rotate(a, xp);
                    xi = rb_root(a);
                }
            } else {
                /* symmetric */
                rebal_block_header_t *w = hdr(a, xp->left_off);
                if (w && w->color == REBAL_RED) {
                    w->color = REBAL_BLACK;
                    xp->color = REBAL_RED;
                    rb_right_rotate(a, xp);
                    w = hdr(a, xp->left_off);
                }
                if ((w == NULL) ||
                    ((w->left_off? hdr(a, w->left_off)->color : REBAL_BLACK) == REBAL_BLACK &&
                     (w->right_off? hdr(a, w->right_off)->color : REBAL_BLACK) == REBAL_BLACK)) {
                    if (w) w->color = REBAL_RED;
                    xi = xp;
                } else {
                    if ((w->left_off? hdr(a, w->left_off)->color : REBAL_BLACK) == REBAL_BLACK) {
                        if (w->right_off) hdr(a, w->right_off)->color = REBAL_BLACK;
                        w->color = REBAL_RED;
                        rb_left_rotate(a, w);
                        w = hdr(a, xp->left_off);
                    }
                    if (w) w->color = xp->color;
                    xp->color = REBAL_BLACK;
                    if (w && w->left_off) hdr(a, w->left_off)->color = REBAL_BLACK;
                    rb_right_rotate(a, xp);
                    xi = rb_root(a);
                }
            }
        }
        if (xi) xi->color = REBAL_BLACK;
    }
}

/* Find and return the best-fit free block (smallest node >= size).
 * Since tree is ordered by size (and tie-break by offset), do standard search.
 */
static rebal_block_header_t *rb_find_best(rebal_t *a, size_t size) {
    rebal_block_header_t *cur = rb_root(a);
    rebal_block_header_t *best = NULL;
    while (cur) {
        if (cur->size >= size) {
            best = cur;
            cur = hdr(a, cur->left_off);
        } else {
            cur = hdr(a, cur->right_off);
        }
    }
    return best;
}

/* -------------------- Block Splitting & Coalescing -------------------- */

/* Split a free block 'b' into allocation of 'needed' bytes and a new free remainder,
 * if there's enough space to form a remainder (header + minimal payload).
 * Return pointer to the block used for allocation (still b).
 *
 * NOTE: needed must include header size and alignment (i.e., the block's total size requested).
 */
static rebal_block_header_t *split_block(rebal_t *a, rebal_block_header_t *b, size_t needed) {
    if (b->size < needed + sizeof(rebal_block_header_t) + REBAL_MIN_ALIGN) {
        /* Not enough space to create a new free block */
        return b;
    }

    uint32_t remaining = b->size - (uint32_t)needed;
    b->size = (uint32_t)needed;

    /* new block starts after b */
    rebal_block_header_t *nb = (rebal_block_header_t *)((uintptr_t)b + (uintptr_t)needed);
    zero_bytes(nb, sizeof(rebal_block_header_t));
    nb->size = remaining;
    nb->is_free = 1;
    nb->color = REBAL_BLACK; /* default; will be inserted into RB which sets color */

    /* physical links */
    nb->next_phys_off = b->next_phys_off;
    nb->prev_phys_off = off_of(a, b);
    if (b->next_phys_off) hdr(a, b->next_phys_off)->prev_phys_off = off_of(a, nb);
    b->next_phys_off = off_of(a, nb);

    /* insert new free remainder into RB tree */
    rb_insert(a, nb);

    return b;
}

/* Coalesce free block b with adjacent free neighbors, removing coalesced neighbors from tree.
 * Returns pointer to the coalesced block (which might be b or previous neighbor).
 */
static rebal_block_header_t *coalesce(rebal_t *a, rebal_block_header_t *b) {
    /* merge with next if free */
    if (b->next_phys_off) {
        rebal_block_header_t *n = hdr(a, b->next_phys_off);
        if (n->is_free) {
            rb_delete(a, n); /* remove neighbor from RB tree */
            b->size += n->size;
            b->next_phys_off = n->next_phys_off;
            if (n->next_phys_off) hdr(a, n->next_phys_off)->prev_phys_off = off_of(a, b);
        }
    }

    /* merge with prev if free */
    if (b->prev_phys_off) {
        rebal_block_header_t *p = hdr(a, b->prev_phys_off);
        if (p->is_free) {
            rb_delete(a, p);
            p->size += b->size;
            p->next_phys_off = b->next_phys_off;
            if (b->next_phys_off) hdr(a, b->next_phys_off)->prev_phys_off = off_of(a, p);
            b = p;
        }
    }

    return b;
}

/* -------------------- Allocation / Free API -------------------- */

/* rebal_alloc: allocate payload of 'size' bytes from allocator 'a' */
void *rebal_alloc(rebal_t *a, size_t size) {
    if (!a || size == 0) return NULL;

    size_t needed = align_up(size + sizeof(rebal_block_header_t), REBAL_MIN_ALIGN);

    rebal_block_header_t *b = rb_find_best(a, needed);
    if (!b) return NULL;

    /* remove selected free block from RB tree */
    rb_delete(a, b);

    /* if large enough, split and insert remainder inside split_block */
    b = split_block(a, b, needed);

    b->is_free = 0;
    /* color/children/parent fields are irrelevant for allocated blocks */

    /* return pointer to payload (after header) */
    return (void *)((uintptr_t)b + sizeof(rebal_block_header_t));
}

/* rebal_free: free a previously allocated pointer */
void rebal_free(rebal_t *a, void *ptr) {
    if (!a || !ptr) return;

    rebal_block_header_t *b = (rebal_block_header_t *)((uintptr_t)ptr - sizeof(rebal_block_header_t));
    if (b->is_free) return; /* double free guard */

    b->is_free = 1;

    /* coalesce with neighbors; coalesce() removes neighbors from RB tree */
    rebal_block_header_t *nb = coalesce(a, b);

    /* insert coalesced block into RB tree */
    rb_insert(a, nb);
}

/* Simple memcpy implementation for WASM */
static void *rebal_memcpy(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

/**
 * Reallocate memory to a new size.
 * If ptr is NULL, equivalent to rebal_alloc(a, size).
 * If size is 0, equivalent to rebal_free(a, ptr) and returns NULL.
 * If the allocation fails, the original block is left unchanged.
 */
void *rebal_realloc(rebal_t *a, void *ptr, size_t size) {
    /* Handle special cases */
    if (!ptr) {
        return rebal_alloc(a, size);
    }
    if (size == 0) {
        rebal_free(a, ptr);
        return NULL;
    }
    if (!a) {
        return NULL;
    }

    /* Get the block header */
    rebal_block_header_t *b = (rebal_block_header_t *)((uintptr_t)ptr - sizeof(rebal_block_header_t));
    if (b->is_free) {
        return NULL; /* Block is already free */
    }

    size_t old_size = b->size - sizeof(rebal_block_header_t);
    size_t new_size = align_up(size + sizeof(rebal_block_header_t), REBAL_MIN_ALIGN) - sizeof(rebal_block_header_t);

    /* If the size is the same, return the original pointer */
    if (old_size == size) {
        return ptr;
    }

    /* If shrinking the block */
    if (size < old_size) {
        /* Calculate the size of the new block and potential split */
        size_t new_block_size = new_size + sizeof(rebal_block_header_t);
        size_t remaining = b->size - new_block_size;

        /* Only split if we can create a new free block with minimum size */
        if (remaining >= sizeof(rebal_block_header_t) + REBAL_MIN_ALIGN) {
            /* Create a new free block after the resized block */
            rebal_block_header_t *new_free = (rebal_block_header_t *)((uintptr_t)b + new_block_size);
            zero_bytes(new_free, sizeof(rebal_block_header_t));
            
            /* Set up the new free block */
            new_free->size = (uint32_t)remaining;
            new_free->is_free = 1;
            new_free->prev_phys_off = off_of(a, b);
            new_free->next_phys_off = b->next_phys_off;
            
            /* Update the original block's size and next pointer */
            b->size = (uint32_t)new_block_size;
            b->next_phys_off = off_of(a, new_free);
            
            /* Update the next block's previous pointer */
            if (new_free->next_phys_off) {
                hdr(a, new_free->next_phys_off)->prev_phys_off = off_of(a, new_free);
            }
            
            /* Insert the new free block into the free tree */
            rb_insert(a, new_free);
            
            /* Try to coalesce the new free block with its neighbors */
            coalesce(a, new_free);
        }
        
        return ptr;
    }

    /* If we get here, we need to grow the block */
    
    /* Check if the next block is free and large enough */
    if (b->next_phys_off) {
        rebal_block_header_t *next = hdr(a, b->next_phys_off);
        size_t needed = new_size - old_size;
        
        if (next->is_free && (next->size >= needed)) {
            /* Remove next from free tree */
            rb_delete(a, next);
            
            /* Calculate new size if we take what we need from next */
            size_t remaining = next->size - needed;
            
            if (remaining >= sizeof(rebal_block_header_t) + REBAL_MIN_ALIGN) {
                /* Split the next block */
                rebal_block_header_t *new_next = (rebal_block_header_t *)((uintptr_t)next + needed);
                zero_bytes(new_next, sizeof(rebal_block_header_t));
                
                new_next->size = (uint32_t)remaining;
                new_next->is_free = 1;
                new_next->prev_phys_off = off_of(a, b);
                new_next->next_phys_off = next->next_phys_off;
                
                /* Update the original block's size and next pointer */
                b->size += (uint32_t)needed;
                b->next_phys_off = off_of(a, new_next);
                
                /* Update the next block's previous pointer */
                if (new_next->next_phys_off) {
                    hdr(a, new_next->next_phys_off)->prev_phys_off = off_of(a, new_next);
                }
                
                /* Insert the new free block into the free tree */
                rb_insert(a, new_next);
                
                /* Try to coalesce the new free block with its neighbors */
                coalesce(a, new_next);
            } else {
                /* Take the whole next block */
                b->size += next->size;
                b->next_phys_off = next->next_phys_off;
                
                /* Update the next block's previous pointer */
                if (next->next_phys_off) {
                    hdr(a, next->next_phys_off)->prev_phys_off = off_of(a, b);
                }
            }
            
            return ptr;
        }
    }
    
    /* If we can't expand in place, allocate a new block and copy the data */
    void *new_ptr = rebal_alloc(a, size);
    if (!new_ptr) {
        return NULL;
    }
    
    /* Copy the data from the old block to the new one */
    size_t copy_size = (old_size < size) ? old_size : size;
    rebal_memcpy(new_ptr, ptr, copy_size);
    
    /* Free the old block */
    rebal_free(a, ptr);
    
    return new_ptr;
}


/* -------------------- Debug / Dump Helpers -------------------- */

#ifdef REBAL_DEBUG

/* Print a physical list of blocks (for debug) */
void dump_physical(rebal_t *a) {
    printf("Physical blocks:\n");
    rebal_block_header_t *b = hdr(a, a->first_block);
    while (b) {
        printf("  off=%u size=%u %s prev=%u next=%u\n",
               off_of(a, b), b->size, (b->is_free ? "FREE" : "ALLOC"),
               (unsigned)b->prev_phys_off, (unsigned)b->next_phys_off);
        if (b->next_phys_off == 0) break;
        b = hdr(a, b->next_phys_off);
    }
}

/* In-order traversal of RB tree to print node sizes and offsets */
void rb_inorder_print(rebal_t *a, rebal_block_header_t *n, int depth) {
    if (!n) return;
    if (n->left_off) rb_inorder_print(a, hdr(a, n->left_off), depth + 1);
    for (int i=0;i<depth;i++) printf("  ");
    printf("node off=%u size=%u color=%s\n", off_of(a, n), n->size, (n->color==REBAL_RED?"R":"B"));
    if (n->right_off) rb_inorder_print(a, hdr(a, n->right_off), depth + 1);
}

void dump_free_tree(rebal_t *a) {
    printf("Free tree (in-order):\n");
    rebal_block_header_t *r = rb_root(a);
    if (!r) { printf("  (empty)\n"); return; }
    rb_inorder_print(a, r, 0);
}

#endif // REBAL_DEBUG
