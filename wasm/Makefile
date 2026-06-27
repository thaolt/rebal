# wasm/Makefile
#
# Build the rebal.wasm demo with clang --target=wasm32 and wasm-ld.
# No Emscripten, no libc.

LLVM_PREFIX := /opt/homebrew/opt/llvm
LLD_PREFIX  := /opt/homebrew/opt/lld

CLANG    := $(LLVM_PREFIX)/bin/clang
WASM_LD  := $(LLD_PREFIX)/bin/wasm-ld

# WASM memory: one 64 KiB page (minimum page size). The demo arena itself
# is capped to 10 KiB inside rebal_wasm.c, so the visualizer shows a compact map.
WASM_MEMORY_SIZE := 65536
STACK_SIZE       := 2048

CFLAGS := --target=wasm32 \
          -nostdlib \
          -DBUILDING_WASM \
          -O3 \
          -I.. \
          -fvisibility=hidden \
          -fuse-ld=$(WASM_LD)

LDFLAGS := -Wl,--no-entry \
           -Wl,--export-memory \
           -Wl,--export=__heap_base \
           -Wl,--export=rebal_wasm_init \
           -Wl,--export=rebal_wasm_alloc \
           -Wl,--export=rebal_wasm_free \
           -Wl,--export=rebal_wasm_realloc \
           -Wl,--export=rebal_wasm_validate \
           -Wl,--export=rebal_wasm_get_stats_total_free \
           -Wl,--export=rebal_wasm_get_stats_total_allocated \
           -Wl,--export=rebal_wasm_get_stats_free_blocks \
           -Wl,--initial-memory=$(WASM_MEMORY_SIZE) \
           -Wl,--max-memory=$(WASM_MEMORY_SIZE) \
           -Wl,-z,stack-size=$(STACK_SIZE)

SRC := rebal_wasm.c ../rebal.c
OUT := rebal.wasm

.PHONY: all clean serve

all: $(OUT)

$(OUT): $(SRC)
	$(CLANG) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

clean:
	rm -f $(OUT)

serve:
	@echo "Serving demo at http://localhost:8080/index.html"
	python3 -m http.server 8080
