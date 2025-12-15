rebal: Red-black allocator
===

A simple allocator with a Red-Black balanced free tree. Primarily designed for use in WebAssembly (WASM) modules.

Features:
 * No external heap allocation inside allocator
 * Uses offsets relative to buffer base
 * Best-fit search (find smallest free block >= needed)
 * Splitting on allocation
 * Coalescing on free
 * Red-Black tree for free blocks to guarantee O(log n) search/inserts/removes
 * Memory backed by a user-provided buffer (no real heap needed).
 * No libc dependent.

Limits:
 * Not thread-safe; use a separate arena per thread if needed

Notes:
 * Offsets are 32-bit; change 'offset_t' to uint64_t if buffer > 4GB.

## Origin story

This implementation was generated with the assistance of Github Copilot (GPT 5.1 model). Use it at your own risk.

Initially, I asked Copilot to generate a simple arena allocator for my WASM modules. I did not know the original source of the generated code, but it worked well for my use cases.

While researching how to build WASM modules in C, I came across the [VMIR WASM Runtime](https://github.com/andoma/vmir) repository. Its `tlsf` module looks very similar to the code produced by Copilot.

The `tlsf` (*Two-Level Segregated Fit*) memory allocator included in VMIR is, in turn, a public-domain implementation originating from [http://tlsf.baisoku.org](https://web.archive.org/web/20160322014215/https://tlsf.baisoku.org/), based on the documentation at [http://rtportal.upv.es/rtmalloc/allocators/tlsf/index.shtml](https://web.archive.org/web/20070629065219/http://rtportal.upv.es/rtmalloc/allocators/tlsf/index.shtml).

Although, the `rebal` allocator is a bit different from `tlsf` allocator now. I still want to give credit where credit is due. Some of the external links may no longer be available; access via the Wayback Machine may be required.

## Examples

### C usage example

```c
#include <stdio.h>
#include "rebal.h"

int main(void) {
    /* small buffer for testing */
    static uint8_t buffer[2048];

    int rc = rebal_init(buffer, sizeof(buffer));
    if (rc != 0) {
        printf("allocator_init failed: %d\n", rc);
        return 1;
    }

    rebal_t *A = (rebal_t *)buffer;

    printf("Allocator initialized: capacity=%u free_root=%u first_block=%u\n",
           A->capacity, (unsigned)A->free_root, (unsigned)A->first_block);

    void *p1 = rebal_alloc(A, 64);
    // ...
```

### WASM Usage example

```c
#include "rebal.h"

static rebal_t *_default_arena = NULL;

void vltd_minit(uintptr_t base, uint32_t mem_sz) {
    int rc = rebal_init((void*) base, mem_sz - base);
    _default_arena = (rebal_t *) base;
}
void *vltd_malloc(uint32_t size) {
    return rebal_alloc(_default_arena, size);
}
void *vltd_calloc(uint32_t nmemb, uint32_t size) {
    void *ptr = rebal_alloc(_default_arena, nmemb * size);
    _vltdr_zero(ptr, nmemb * size);
    return ptr;
}
void vltd_free(void *data){
    rebal_free(_default_arena, data);
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
    -Os -fvisibility=hidden (...) rebal.c
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
