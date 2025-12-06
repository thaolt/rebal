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
 * No libc dependent.

Limits:
 * Not thread-safe, use different a arena per thread if needed

Notes:
 * Offsets are 32-bit; change 'offset_t' to uint64_t if buffer > 4GB.
 * I do not write this, it is all ChatGPT generated. Use it as long as you like
 and as your own risk.

### WASM Usage example

