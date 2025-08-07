#pragma once
#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>

#define NUM_BLOCKS 10
#define BLOCK_SIZE 1200

#ifdef CONFIG_NOCACHE_MEMORY
#define MEM_SLAB_CACHE_ATTR __nocache
#else
#define MEM_SLAB_CACHE_ATTR
#endif /* CONFIG_NOCACHE_MEMORY */

static char MEM_SLAB_CACHE_ATTR
	__aligned(WB_UP(32)) _k_mem_slab_buf_tx_0_mem_slab[(NUM_BLOCKS)*WB_UP(BLOCK_SIZE)];

static STRUCT_SECTION_ITERABLE(k_mem_slab, tx_0_mem_slab) = Z_MEM_SLAB_INITIALIZER(
	tx_0_mem_slab, _k_mem_slab_buf_tx_0_mem_slab, WB_UP(BLOCK_SIZE), NUM_BLOCKS);