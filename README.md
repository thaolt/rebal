sarena
===

Simple arena allocator with Red-Black balanced free tree. I use it mainly in my WASM modules.

Features:
 * No external heap allocation inside allocator
 * Uses offsets relative to buffer base
 * Best-fit search (find smallest free block >= needed)
 * Splitting on allocation
 * Coalescing on free
 * Red-Black tree for free blocks to guarantee O(log n) search/inserts/removes
 * Memory backed by a user-provided buffer (no real heap needed).
 * No libc dependant.

Limits:
 * Not thread-safe, use different a arena per thread if needed

Notes:
 * Offsets are 32-bit; change 'offset_t' to uint64_t if buffer > 4GB.
 * I do not write this, it is all ChatGPT generated. Use it as long as you like
 and as your own risk.

### C usage example

```c
#include <stdio.h>
#include "sarena.h"

int main(void) {
    /* small buffer for testing */
    static uint8_t buffer[2048];

    int rc = sarena_init(buffer, sizeof(buffer));
    if (rc != 0) {
        printf("allocator_init failed: %d\n", rc);
        return 1;
    }

    sarena_t *A = (sarena_t *)buffer;

    printf("Allocator initialized: capacity=%u free_root=%u first_block=%u\n",
           A->capacity, (unsigned)A->free_root, (unsigned)A->first_block);

    void *p1 = sarena_alloc(A, 64);
    // ...
```

### WASM usage example

```c
#include "sarena.h"

static sarena_t *_arena = NULL;

void vltd_minit(uintptr_t base, uint32_t mem_sz) {
    int rc = sarena_init((void*) base, mem_sz - base);
    _arena = (sarena_t *) base;
}
void *vltd_malloc(uint32_t size) {
    return sarena_alloc(_arena, size);
}
void *vltd_calloc(uint32_t nmemb, uint32_t size) {
    void *ptr = sarena_alloc(_arena, nmemb * size);
    _vltdr_zero(ptr, nmemb * size);
    return ptr;
}
void vltd_free(void *data){
    sarena_free(_arena, data);
}
```

Compile with clang:

```sh
clang --target=wasm32 \
    -nostdlib \
    -DBUILDING_WASM \
    -Wl,--no-entry \
    -Wl,--export=__heap_base \
    -Wl,--export=vltd_minit \
    -Wl,--export=vltd_malloc \
    -Wl,--export=vltd_calloc \
    -Wl,--export=vltd_free \
    (...) \
    -Wl,--allow-undefined \
    -Wl,--strip-all \
    -Wl,--lto-O3 \
    -Wl,-O3 \
    -o libvltd.wasm \
    -Os -fvisibility=hidden (...) sarena.c
```

Use it in JS:

```js
// ...
const mod = wasm_module;
const mem = mod.exports.memory;
const mem_heap_base = mod.exports.__heap_base.value;

mod.exports.vltd_minit(mem_heap_base, mem.buffer.byteLength);

const vltd_malloc = mod.exports.vltd_malloc;
const vltd_calloc = mod.exports.vltd_calloc;
const vltd_free = mod.exports.vltd_free;
// ...
// Allocate memory for the input barcode string
const barcode_ptr = vltd_malloc(barcode_str.length + 1);

// Allocate memory for the output buffer (4096 bytes should be enough)
const output_size = 4096;
const output_ptr = vltd_malloc(output_size);

// Write barcode string to memory
const barcode_bytes = new TextEncoder().encode(barcode_str);
const barcode_view = new Uint8Array(mem.buffer, barcode_ptr, barcode_bytes.length);
barcode_view.set(barcode_bytes);

// Call the wasm function
const return_length = vltdecode(output_ptr, output_size, barcode_ptr, barcode_bytes.length);
```
