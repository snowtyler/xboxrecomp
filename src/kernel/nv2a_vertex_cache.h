#ifndef NV2A_VERTEX_CACHE_H
#define NV2A_VERTEX_CACHE_H
#include <stdint.h>
#include <string.h>
#define NV2A_VERTEX_CACHE_SIZE 65536u
typedef struct { uint32_t key, ordinal, epoch; } Nv2aVertexCacheEntry;
typedef struct { Nv2aVertexCacheEntry entries[NV2A_VERTEX_CACHE_SIZE]; uint32_t epoch; } Nv2aVertexCache;
static void nv2a_vertex_cache_begin(Nv2aVertexCache *cache)
{
    if (++cache->epoch == 0) {
        memset(cache->entries, 0, sizeof cache->entries);
        cache->epoch = 1;
    }
}
/* Return a previous output's ordinal on a hit. The first occurrence records
 * the caller's ordinal. Open addressing preserves full-width guest indices. */
static int nv2a_vertex_cache_lookup(Nv2aVertexCache *cache, uint32_t key,
                                   uint32_t ordinal, uint32_t *previous)
{
    uint32_t i = key * 2654435761u & (NV2A_VERTEX_CACHE_SIZE - 1u), n;
    for (n = 0; n < NV2A_VERTEX_CACHE_SIZE; ++n) {
        Nv2aVertexCacheEntry *entry = &cache->entries[i];
        if (entry->epoch != cache->epoch) {
            entry->epoch = cache->epoch; entry->key = key; entry->ordinal = ordinal;
            return 0;
        }
        if (entry->key == key) { *previous = entry->ordinal; return 1; }
        i = (i + 1u) & (NV2A_VERTEX_CACHE_SIZE - 1u);
    }
    return 0; /* Full table: keep rendering through the uncached path. */
}
#endif
