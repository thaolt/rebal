#include "rebal.h"

/* -------------------- Helpers (no libc) -------------------- */


void *rebal_memset(void *dst, int v, size_t n) {
    unsigned char *p = dst;
    while (n--) *p++ = (unsigned char)v;
    return dst;
}

void *rebal_memcpy(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}


/* In freestanding/WASM builds, provide memset/memcpy for the linker.
 * In hosted builds, defining these would clash with libc (UB).
 *
 * Mark them noinline so the compiler cannot optimize the loop body
 * back into a recursive call to memset/memcpy. */
#ifdef BUILDING_WASM
__attribute__((used, noinline))
void *memset(void *dst, int v, size_t n) {
    return rebal_memset(dst, v, n);
}

__attribute__((used, noinline))
void *memcpy(void *dest, const void *src, size_t n) {
    return rebal_memcpy(dest, src, n);
}
#endif

static size_t align_up(size_t x, size_t a) {
    size_t m = a - 1;
    return (x + m) & ~m;
}

/* Safe addition with overflow detection */
static int safe_add_size_t(size_t a, size_t b, size_t *result) {
    if (a > SIZE_MAX - b) {
        return 0; /* overflow */
    }
    *result = a + b;
    return 1;
}

static inline rebal_t *alloc_from_buf(void *buf) {
    return (rebal_t *)buf;
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

/**
 * Validate allocator integrity and check for corruption.
 */
static int validate_allocator(rebal_t *a) {
    if (!a) return REBAL_ERROR_NULL_BUFFER;
    if (a->magic != REBAL_MAGIC) return REBAL_ERROR_CORRUPTED;
    return REBAL_SUCCESS;
}

/**
 * Validate block header integrity.
 */
static int validate_block(rebal_t *a, rebal_block_header_t *b) {
    if (!b) return REBAL_ERROR_INVALID_POINTER;

    /* Check if block is within allocator bounds */
    uintptr_t base = (uintptr_t)a;
    uintptr_t block_addr = (uintptr_t)b;

    if (block_addr < base || block_addr >= base + a->capacity) {
        return REBAL_ERROR_INVALID_POINTER;
    }

    /* Check size is reasonable */
    if (b->size < sizeof(rebal_block_header_t) || b->size > a->capacity) {
        return REBAL_ERROR_CORRUPTED;
    }
    
    /* Check that block doesn't overflow buffer bounds */
    uintptr_t block_end = block_addr + b->size;
    uintptr_t buf_end = base + a->capacity;
    if (block_end > buf_end || block_end < block_addr) {
        return REBAL_ERROR_CORRUPTED; /* Overflow or out of bounds */
    }

    /* Check block magic: allocated blocks carry REBAL_BLOCK_MAGIC, free blocks 0 */
    if (b->is_free) {
        if (b->magic != 0) return REBAL_ERROR_CORRUPTED;
    } else {
        if (b->magic != REBAL_BLOCK_MAGIC) return REBAL_ERROR_INVALID_POINTER;
    }

    return REBAL_SUCCESS;
}

int rebal_init(void *buffer, size_t buffer_size) {
    if (buffer == NULL) return REBAL_ERROR_NULL_BUFFER;
    if (buffer_size < MIN_OVERHEAD) return REBAL_ERROR_BUFFER_TOO_SMALL;
    /* offset_t and capacity are 32-bit — reject buffers larger than 4 GB */
    if (buffer_size > REBAL_MAX_CAPACITY) return REBAL_ERROR_BUFFER_TOO_LARGE;
    /* Buffer must be aligned to REBAL_MIN_ALIGN so headers and payloads are aligned */
    if (((uintptr_t)buffer & (REBAL_MIN_ALIGN - 1)) != 0) return REBAL_ERROR_INVALID_ALIGNMENT;

    rebal_t *a = alloc_from_buf(buffer);
    rebal_memset(a, 0, sizeof(rebal_t));

    a->magic = REBAL_MAGIC;
    a->capacity = (uint32_t)buffer_size;
    a->free_root = 0;
    a->first_block = 0;

    uintptr_t base = (uintptr_t)buffer;
    uintptr_t block_start = base + sizeof(rebal_t);
    /* Align block start to REBAL_MIN_ALIGN */
    block_start = (uintptr_t)align_up(block_start, REBAL_MIN_ALIGN);

    if ((uintptr_t)buffer + buffer_size <= block_start + sizeof(rebal_block_header_t)) {
        return REBAL_ERROR_INVALID_ALIGNMENT;
    }

    rebal_block_header_t *b = (rebal_block_header_t *)block_start;
    rebal_memset((void *)b, 0, sizeof(rebal_block_header_t));

    /* Calculate block size with overflow check */
    uintptr_t block_end = (uintptr_t)buffer + buffer_size;
    if (block_end < block_start) {
        return REBAL_ERROR_BUFFER_TOO_LARGE; /* Overflow in calculation */
    }
    uint32_t block_total_size = (uint32_t)(block_end - block_start);
    b->size = block_total_size;
    b->is_free = 1;
    b->color = 0; /* black by default when inserted to RB as root */
    b->left_off = b->right_off = b->parent_off = 0;
    b->prev_phys_off = 0;
    b->next_phys_off = 0;
    b->magic = 0; /* free block */

    rebal_offset_t boff = (rebal_offset_t)(block_start - base);
    a->first_block = boff;
    a->free_root = boff;
    /* ensure root is black - it already is (color=0) */

    return REBAL_SUCCESS;
}

/* Forward declaration — rb_root is defined in the RB tree section below */
static inline rebal_block_header_t *rb_root(rebal_t *a);

/* Count nodes in the RB free tree (recursive). Returns -1 on corruption. */
static int rb_count_depth(rebal_t *a, rebal_block_header_t *n, int depth) {
    if (!n) return 0;
    if (depth > 1024) return -1; /* guard against corrupted cycles */
    int left = rb_count_depth(a, hdr(a, n->left_off), depth + 1);
    if (left < 0) return -1;
    int right = rb_count_depth(a, hdr(a, n->right_off), depth + 1);
    if (right < 0) return -1;
    return left + right + 1;
}

static int rb_count(rebal_t *a, rebal_block_header_t *n) {
    return rb_count_depth(a, n, 0);
}

int rebal_validate(rebal_t *a) {
    int rc = validate_allocator(a);
    if (rc != REBAL_SUCCESS) return rc;

    /* Walk physical blocks: validate each, check adjacency and link consistency.
     * Cap iterations to detect cycles caused by corruption. */
    size_t free_count = 0;
    size_t iter = 0;
    size_t max_blocks = a->capacity / sizeof(rebal_block_header_t) + 1;

    rebal_block_header_t *b = hdr(a, a->first_block);
    while (b) {
        if (++iter > max_blocks) return REBAL_ERROR_CORRUPTED;

        rc = validate_block(a, b);
        if (rc != REBAL_SUCCESS) return rc;

        if (b->is_free) free_count++;

        /* Check adjacency: next physical block should be at b + b->size */
        if (b->next_phys_off) {
            rebal_offset_t expected = off_of(a, b) + (rebal_offset_t)b->size;
            if (b->next_phys_off != expected) return REBAL_ERROR_CORRUPTED;

            rebal_block_header_t *nxt = hdr(a, b->next_phys_off);
            if (!nxt) return REBAL_ERROR_CORRUPTED;
            /* Back-link must agree */
            if (nxt->prev_phys_off != off_of(a, b)) return REBAL_ERROR_CORRUPTED;
        } else {
            /* Last block should end at the buffer boundary */
            uintptr_t block_end = (uintptr_t)b + b->size;
            uintptr_t buf_end = (uintptr_t)a + a->capacity;
            if (block_end != buf_end) return REBAL_ERROR_CORRUPTED;
        }

        if (b->next_phys_off == 0) break;
        b = hdr(a, b->next_phys_off);
    }

    /* Verify that the number of free blocks in the physical list matches
     * the number of nodes in the RB free tree. */
    int tree_count = rb_count(a, rb_root(a));
    if (tree_count < 0) return REBAL_ERROR_CORRUPTED;
    if ((size_t)tree_count != free_count) return REBAL_ERROR_CORRUPTED;

    return REBAL_SUCCESS;
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
    /* Place z using the same comparison as the search: size first, then offset */
    int go_left;
    if (z->size < y->size) go_left = 1;
    else if (z->size > y->size) go_left = 0;
    else go_left = (off_of(a, z) < off_of(a, y));
    if (go_left) y->left_off = off_of(a, z);
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

/* Delete fixup. x may be NULL (when the physically-removed node had no
 * children); x_parent and x_is_left track the parent and side in that case
 * so we can navigate the tree without dereferencing a NULL pointer.
 * x_is_left is only used for the first iteration; after that, x becomes
 * non-NULL (it takes the place of xp) and the side can be determined
 * normally from the parent's child pointers.
 */
static void rb_delete_fixup(rebal_t *a, rebal_block_header_t *x,
                            rebal_block_header_t *x_parent, int x_is_left) {
    while (x != rb_root(a) && (x == NULL || x->color == REBAL_BLACK)) {
        rebal_block_header_t *xp = (x != NULL) ? hdr(a, x->parent_off) : x_parent;
        if (!xp) break;

        int is_left;
        if (x != NULL) {
            is_left = (xp->left_off == off_of(a, x));
        } else {
            is_left = x_is_left;
            /* After first iteration, x becomes non-NULL, so this is only
             * used once. Reset to avoid stale use. */
            x_is_left = 0;
        }

        if (is_left) {
            rebal_block_header_t *w = hdr(a, xp->right_off);
            if (w && w->color == REBAL_RED) {
                w->color = REBAL_BLACK;
                xp->color = REBAL_RED;
                rb_left_rotate(a, xp);
                w = hdr(a, xp->right_off);
            }
            uint8_t wl = (w && w->left_off) ? hdr(a, w->left_off)->color : REBAL_BLACK;
            uint8_t wr = (w && w->right_off) ? hdr(a, w->right_off)->color : REBAL_BLACK;
            if (w == NULL || (wl == REBAL_BLACK && wr == REBAL_BLACK)) {
                if (w) w->color = REBAL_RED;
                x = xp;
                x_parent = hdr(a, x->parent_off);
            } else {
                if (wr == REBAL_BLACK) {
                    if (w->left_off) hdr(a, w->left_off)->color = REBAL_BLACK;
                    w->color = REBAL_RED;
                    rb_right_rotate(a, w);
                    w = hdr(a, xp->right_off);
                }
                w->color = xp->color;
                xp->color = REBAL_BLACK;
                if (w->right_off) hdr(a, w->right_off)->color = REBAL_BLACK;
                rb_left_rotate(a, xp);
                x = rb_root(a);
            }
        } else {
            rebal_block_header_t *w = hdr(a, xp->left_off);
            if (w && w->color == REBAL_RED) {
                w->color = REBAL_BLACK;
                xp->color = REBAL_RED;
                rb_right_rotate(a, xp);
                w = hdr(a, xp->left_off);
            }
            uint8_t wl = (w && w->left_off) ? hdr(a, w->left_off)->color : REBAL_BLACK;
            uint8_t wr = (w && w->right_off) ? hdr(a, w->right_off)->color : REBAL_BLACK;
            if (w == NULL || (wl == REBAL_BLACK && wr == REBAL_BLACK)) {
                if (w) w->color = REBAL_RED;
                x = xp;
                x_parent = hdr(a, x->parent_off);
            } else {
                if (wl == REBAL_BLACK) {
                    if (w->right_off) hdr(a, w->right_off)->color = REBAL_BLACK;
                    w->color = REBAL_RED;
                    rb_left_rotate(a, w);
                    w = hdr(a, xp->left_off);
                }
                w->color = xp->color;
                xp->color = REBAL_BLACK;
                if (w->left_off) hdr(a, w->left_off)->color = REBAL_BLACK;
                rb_right_rotate(a, xp);
                x = rb_root(a);
            }
        }
    }
    if (x) x->color = REBAL_BLACK;
}

/* Delete node z from RB tree and fixup.
 * Tracks x_parent and x_is_left explicitly so the fixup works correctly
 * even when the spliced-out node's child x is NULL (no sentinel needed).
 */
static void rb_delete(rebal_t *a, rebal_block_header_t *z) {
    rebal_block_header_t *y = z;
    rebal_block_header_t *x = NULL;
    rebal_block_header_t *x_parent = NULL;
    int x_is_left = 0;
    uint8_t y_original_color = y->color;

    if (z->left_off == 0) {
        x = hdr(a, z->right_off);
        x_parent = hdr(a, z->parent_off);
        /* Record which side z was on before transplant zeroes the pointer */
        if (x_parent) x_is_left = (x_parent->left_off == off_of(a, z));
        rb_transplant(a, z, x);
    } else if (z->right_off == 0) {
        x = hdr(a, z->left_off);
        x_parent = hdr(a, z->parent_off);
        if (x_parent) x_is_left = (x_parent->left_off == off_of(a, z));
        rb_transplant(a, z, x);
    } else {
        y = rb_minimum(a, hdr(a, z->right_off));
        y_original_color = y->color;
        x = hdr(a, y->right_off);
        if (y->parent_off == off_of(a, z)) {
            x_parent = y;
            /* y replaces z, and x (NULL) is y's right child */
            x_is_left = 0; /* x is the right child of y */
            if (x) x->parent_off = off_of(a, y);
        } else {
            x_parent = hdr(a, y->parent_off);
            /* x takes y's place; y was the left child of its parent (minimum) */
            x_is_left = 1;
            rb_transplant(a, y, x);
            y->right_off = z->right_off;
            if (y->right_off) hdr(a, y->right_off)->parent_off = off_of(a, y);
        }
        rb_transplant(a, z, y);
        y->left_off = z->left_off;
        if (y->left_off) hdr(a, y->left_off)->parent_off = off_of(a, y);
        y->color = z->color;
    }

    if (y_original_color == REBAL_BLACK) {
        rb_delete_fixup(a, x, x_parent, x_is_left);
    }
}

/* Find and return the best-fit free block (smallest node >= size).
 * Since tree is ordered by size (and tie-break by offset), do standard search.
 * When multiple blocks have the same size, we return the first one found,
 * which is deterministic based on tree structure. This is correct for best-fit
 * since any block of the same size is equally good.
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

    /* Check for overflow before casting */
    if (needed > UINT32_MAX) {
        return b; /* Can't split if needed exceeds 32-bit range */
    }
    
    uint32_t remaining = b->size - (uint32_t)needed;
    b->size = (uint32_t)needed;

    /* new block starts after b */
    uintptr_t nb_addr = (uintptr_t)b + (uintptr_t)needed;
    /* Ensure the new block is properly aligned */
    if (nb_addr & (REBAL_MIN_ALIGN - 1)) {
        /* This should not happen if needed is properly aligned,
         * but if it does, we can't split safely */
        b->size += remaining; /* Restore original size */
        return b;
    }
    
    rebal_block_header_t *nb = (rebal_block_header_t *)nb_addr;
    rebal_memset(nb, 0, sizeof(rebal_block_header_t));
    nb->size = remaining;
    nb->is_free = 1;
    nb->color = REBAL_BLACK; /* default; will be inserted into RB which sets color */
    nb->magic = 0; /* free block */

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
        if (n && n->is_free && validate_block(a, n) == REBAL_SUCCESS) {
            rb_delete(a, n); /* remove neighbor from RB tree */
            /* Overflow check */
            if (b->size <= UINT32_MAX - n->size) {
                b->size += n->size;
            }
            b->next_phys_off = n->next_phys_off;
            if (n->next_phys_off) hdr(a, n->next_phys_off)->prev_phys_off = off_of(a, b);
        }
    }

    /* merge with prev if free */
    if (b->prev_phys_off) {
        rebal_block_header_t *p = hdr(a, b->prev_phys_off);
        if (p && p->is_free && validate_block(a, p) == REBAL_SUCCESS) {
            rb_delete(a, p);
            /* Overflow check */
            if (p->size <= UINT32_MAX - b->size) {
                p->size += b->size;
            }
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
    if (!a) return NULL;
    if (size == 0) return NULL;
    if (size > REBAL_MAX_ALLOC_SIZE) return NULL;
    
    /* Validate allocator state */
    if (validate_allocator(a) != REBAL_SUCCESS) return NULL;

    size_t total_size;
    if (!safe_add_size_t(size, sizeof(rebal_block_header_t), &total_size)) {
        return NULL; /* overflow */
    }
    
    size_t needed = align_up(total_size, REBAL_MIN_ALIGN);

    rebal_block_header_t *b = rb_find_best(a, needed);
    if (!b) return NULL;

    /* remove selected free block from RB tree */
    rb_delete(a, b);

    /* if large enough, split and insert remainder inside split_block */
    b = split_block(a, b, needed);

    b->is_free = 0;
    b->magic = REBAL_BLOCK_MAGIC;
    /* color/children/parent fields are irrelevant for allocated blocks */

    /* return pointer to payload (after header) */
    return (void *)((uintptr_t)b + sizeof(rebal_block_header_t));
}

/* rebal_free: free a previously allocated pointer */
void rebal_free(rebal_t *a, void *ptr) {
    if (!a || !ptr) return;
    
    /* Validate allocator state */
    if (validate_allocator(a) != REBAL_SUCCESS) return;

    rebal_block_header_t *b = (rebal_block_header_t *)((uintptr_t)ptr - sizeof(rebal_block_header_t));
    
    /* Validate block */
    if (validate_block(a, b) != REBAL_SUCCESS) return;
    
    if (b->is_free) return; /* double free guard */

    b->is_free = 1;
    b->magic = 0; /* clear magic — block is now free */

    /* coalesce with neighbors; coalesce() removes neighbors from RB tree */
    rebal_block_header_t *nb = coalesce(a, b);

    /* insert coalesced block into RB tree */
    rb_insert(a, nb);
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
    
    if (size > REBAL_MAX_ALLOC_SIZE) return NULL;
    
    /* Validate allocator state */
    if (validate_allocator(a) != REBAL_SUCCESS) return NULL;

    /* Get the block header */
    rebal_block_header_t *b = (rebal_block_header_t *)((uintptr_t)ptr - sizeof(rebal_block_header_t));
    
    /* Validate block */
    if (validate_block(a, b) != REBAL_SUCCESS) return NULL;
    
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
        if (new_block_size > UINT32_MAX) {
            return NULL; /* Overflow check */
        }
        size_t remaining = b->size - new_block_size;

        /* Only split if we can create a new free block with minimum size */
        if (remaining >= sizeof(rebal_block_header_t) + REBAL_MIN_ALIGN) {
            /* Create a new free block after the resized block */
            uintptr_t new_free_addr = (uintptr_t)b + new_block_size;
            /* Ensure the new block is properly aligned */
            if (new_free_addr & (REBAL_MIN_ALIGN - 1)) {
                /* This should not happen if new_block_size is properly aligned,
                 * but if it does, we can't split safely */
                return ptr; /* Don't split, just return original pointer */
            }
            
            rebal_block_header_t *new_free = (rebal_block_header_t *)new_free_addr;
            rebal_memset(new_free, 0, sizeof(rebal_block_header_t));
            
            /* Set up the new free block */
            new_free->size = (uint32_t)remaining;
            new_free->is_free = 1;
            new_free->prev_phys_off = off_of(a, b);
            new_free->next_phys_off = b->next_phys_off;
            new_free->magic = 0;

            /* Update the original block's size and next pointer */
            b->size = (uint32_t)new_block_size;
            b->next_phys_off = off_of(a, new_free);

            /* Update the next block's previous pointer */
            if (new_free->next_phys_off) {
                hdr(a, new_free->next_phys_off)->prev_phys_off = off_of(a, new_free);
            }

            /* Coalesce first (removes neighbors from tree, merges sizes),
             * then insert the result — inserting before coalescing would
             * leave a node in the tree with a stale size key. */
            rebal_block_header_t *coalesced = coalesce(a, new_free);
            rb_insert(a, coalesced);
        }
        
        return ptr;
    }

    /* If we get here, we need to grow the block */

    /* Check if the next block is free and large enough */
    if (b->next_phys_off) {
        rebal_block_header_t *next = hdr(a, b->next_phys_off);
        size_t needed = new_size - old_size;

        if (next && next->is_free && validate_block(a, next) == REBAL_SUCCESS && (next->size >= needed)) {
            /* Remove next from free tree */
            rb_delete(a, next);

            /* Calculate new size if we take what we need from next */
            size_t remaining = next->size - needed;

            if (remaining >= sizeof(rebal_block_header_t) + REBAL_MIN_ALIGN) {
                /* Split the next block. Save next->next_phys_off before
                 * memset, because new_next may overlap next's header when
                 * needed < sizeof(header). */
                rebal_offset_t saved_next_off = next->next_phys_off;
                uintptr_t new_next_addr = (uintptr_t)next + needed;
                /* Ensure the new block is properly aligned */
                if (new_next_addr & (REBAL_MIN_ALIGN - 1)) {
                    /* This should not happen if needed is properly aligned,
                     * but if it does, we can't split safely. Take whole block instead. */
                } else {
                    rebal_block_header_t *new_next = (rebal_block_header_t *)new_next_addr;
                    rebal_memset(new_next, 0, sizeof(rebal_block_header_t));

                    new_next->size = (uint32_t)remaining;
                    new_next->is_free = 1;
                    new_next->prev_phys_off = off_of(a, b);
                    new_next->next_phys_off = saved_next_off;
                    new_next->magic = 0;

                    /* Update the original block's size and next pointer */
                    if (needed > UINT32_MAX || b->size > UINT32_MAX - (uint32_t)needed) {
                        return ptr; /* Overflow check */
                    }
                    b->size += (uint32_t)needed;
                    b->next_phys_off = off_of(a, new_next);

                    /* Update the next block's previous pointer */
                    if (new_next->next_phys_off) {
                        hdr(a, new_next->next_phys_off)->prev_phys_off = off_of(a, new_next);
                    }

                    /* Coalesce first, then insert — see shrink path comment */
                    rebal_block_header_t *coalesced = coalesce(a, new_next);
                    rb_insert(a, coalesced);
                    
                    return ptr; /* Successfully split and expanded */
                }
            }
            
            /* Take the whole next block (either not enough space to split, or alignment failed) */
            rebal_offset_t saved_next_off = next->next_phys_off;
            b->size += next->size;
            b->next_phys_off = saved_next_off;

            /* Update the next block's previous pointer */
            if (saved_next_off) {
                hdr(a, saved_next_off)->prev_phys_off = off_of(a, b);
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


/* -------------------- Statistics API -------------------- */

int rebal_get_stats(rebal_t *a, size_t *total_free, size_t *total_allocated, 
                    size_t *free_blocks) {
    if (!a) return REBAL_ERROR_NULL_BUFFER;
    if (validate_allocator(a) != REBAL_SUCCESS) return REBAL_ERROR_CORRUPTED;
    
    size_t free_bytes = 0;
    size_t allocated_bytes = 0;
    size_t free_count = 0;
    size_t iter = 0;
    size_t max_blocks = a->capacity / sizeof(rebal_block_header_t) + 1;

    rebal_block_header_t *b = hdr(a, a->first_block);
    while (b) {
        if (++iter > max_blocks) return REBAL_ERROR_CORRUPTED;
        if (validate_block(a, b) != REBAL_SUCCESS) {
            return REBAL_ERROR_CORRUPTED;
        }

        if (b->is_free) {
            free_bytes += (b->size - sizeof(rebal_block_header_t));
            free_count++;
        } else {
            allocated_bytes += (b->size - sizeof(rebal_block_header_t));
        }

        if (b->next_phys_off == 0) break;
        b = hdr(a, b->next_phys_off);
    }
    
    if (total_free) *total_free = free_bytes;
    if (total_allocated) *total_allocated = allocated_bytes;
    if (free_blocks) *free_blocks = free_count;
    
    return REBAL_SUCCESS;
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
