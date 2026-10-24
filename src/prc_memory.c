/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nanoPRC is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nanoPRC is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nanoPRC. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>
#include "../include/prc_context.h"
#include <stdio.h>

#if PRC_DEBUG_MEMORY
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
static void *
prc_debug_current_callsite(void)
{
    return _ReturnAddress();
}
#elif defined(__GNUC__) || defined(__clang__)
static void *
prc_debug_current_callsite(void)
{
    return __builtin_return_address(0);
}
#else
static void *
prc_debug_current_callsite(void)
{
    return NULL;
}
#endif
#endif

/* Enable this define to turn on memory guard debugging (detect buffer overruns) */
#define PRC_MEMORY_GUARDS 1

/* Track a specific pointer to detect when it gets corrupted */
static void *g_tracked_pointer = NULL;
static size_t g_tracked_size = 0;

#if PRC_DEBUG_MEMORY
typedef enum prc_debug_pointer_state_e {
    PRC_DEBUG_PTR_NOT_FOUND = 0,
    PRC_DEBUG_PTR_LIVE = 1,
    PRC_DEBUG_PTR_FREED = 2
} prc_debug_pointer_state;

static prc_debug_pointer_state
prc_debug_get_latest_pointer_state(prc_context *ctx, void *p, size_t *latest_index)
{
    size_t i;

    if (latest_index != NULL)
    {
        *latest_index = 0;
    }

    for (i = ctx->current_memory_index; i > 0; i--)
    {
        size_t idx = i - 1;
        if (ctx->debug_memory[idx].data != p)
        {
            continue;
        }

        if (latest_index != NULL)
        {
            *latest_index = idx;
        }
        return (ctx->debug_memory[idx].is_free == 0) ? PRC_DEBUG_PTR_LIVE : PRC_DEBUG_PTR_FREED;
    }

    return PRC_DEBUG_PTR_NOT_FOUND;
}

static void
prc_debug_dump_pointer_history(prc_context *ctx, void *p, size_t max_matches)
{
    size_t i;
    size_t printed = 0;

    printf("    Pointer history for %p (newest first):\n", p);
    for (i = ctx->current_memory_index; i > 0; i--)
    {
        size_t idx = i - 1;
        if (ctx->debug_memory[idx].data != p)
        {
            continue;
        }

        printf("      idx=%zu alloc_index=%zu is_free=%u\n",
            idx,
            ctx->debug_memory[idx].index,
            (unsigned int)ctx->debug_memory[idx].is_free);
        printed++;
        if (printed >= max_matches)
        {
            break;
        }
    }

    if (printed == 0)
    {
        printf("      <no matching entries>\n");
    }
}
#endif

#ifdef PRC_MEMORY_GUARDS

/* Memory guard pattern for detecting buffer overruns */
#define PRC_MEMORY_GUARD_SIZE 32  /* Increased from 16 to catch more overruns */
#define PRC_MEMORY_GUARD_PATTERN 0xDEADBEEF
#define PRC_MEMORY_FREED_PATTERN 0xDDDDDDDD

typedef struct prc_guarded_block_s {
    size_t size;
    uint32_t front_guard[PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t)];
    /* User data follows */
    /* uint32_t back_guard[PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t)] follows user data */
} prc_guarded_block;

static void prc_init_guards(prc_guarded_block *block, size_t user_size)
{
    uint32_t *back_guard;
    size_t i;

    block->size = user_size;

    /* Initialize front guard */
    for (i = 0; i < PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t); i++)
        block->front_guard[i] = PRC_MEMORY_GUARD_PATTERN;

    /* Initialize back guard (after user data) */
    back_guard = (uint32_t *)((unsigned char *)(block + 1) + user_size);
    for (i = 0; i < PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t); i++)
        back_guard[i] = PRC_MEMORY_GUARD_PATTERN;
}

static int prc_check_guards(const char *location, void *user_ptr)
{
    prc_guarded_block *block;
    uint32_t *back_guard;
    size_t i;
    int corrupted = 0;

    if (user_ptr == NULL)
        return 0;

    block = ((prc_guarded_block *)user_ptr) - 1;
    back_guard = (uint32_t *)((unsigned char *)user_ptr + block->size);

    /* Check front guard */
    for (i = 0; i < PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t); i++)
    {
        if (block->front_guard[i] != PRC_MEMORY_GUARD_PATTERN)
        {
            printf("*** HEAP CORRUPTION DETECTED at %s ***\n", location);
            printf("    Front guard corrupted at offset %zu (ptr=%p, size=%zu)\n",
                i * sizeof(uint32_t), user_ptr, block->size);
            printf("    Expected: 0x%08X, Got: 0x%08X\n",
                PRC_MEMORY_GUARD_PATTERN, block->front_guard[i]);
            corrupted = 1;
        }
    }

    /* Check back guard */
    for (i = 0; i < PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t); i++)
    {
        if (back_guard[i] != PRC_MEMORY_GUARD_PATTERN)
        {
            printf("*** HEAP CORRUPTION DETECTED at %s ***\n", location);
            printf("    Back guard corrupted at offset %zu (ptr=%p, size=%zu)\n",
                i * sizeof(uint32_t), user_ptr, block->size);
            printf("    Expected: 0x%08X, Got: 0x%08X\n",
                PRC_MEMORY_GUARD_PATTERN, back_guard[i]);
            printf("    This indicates a buffer OVERRUN of %zu bytes!\n",
                i * sizeof(uint32_t));

            /* Dump the corrupted data */
            printf("    Corrupted data (showing 32 bytes from corruption point):\n    ");
            unsigned char *corrupt_ptr = (unsigned char *)&back_guard[i];
            for (size_t j = 0; j < 32 && j < 64; j++)
            {
                printf("%02X ", corrupt_ptr[j]);
                if ((j + 1) % 16 == 0) printf("\n    ");
            }
            printf("\n");

            corrupted = 1;
        }
    }

    if (corrupted)
    {
        /* Trigger breakpoint to catch the issue */
#ifdef _WIN32
        __debugbreak();
#else
        __builtin_trap();
#endif
    }

    return corrupted;
}

/* Check ALL allocated blocks for corruption - expensive but thorough */
static void prc_check_all_guards(prc_context *ctx, const char *location)
{
#if PRC_DEBUG_MEMORY
    size_t i;
    printf("[Guard Check] Checking all %zu allocations at %s\n",
        ctx->current_memory_index, location);

    for (i = 0; i < ctx->current_memory_index; i++)
    {
        if (ctx->debug_memory[i].is_free == 0)
        {
            prc_check_guards(location, ctx->debug_memory[i].data);
        }
    }
#endif
}

#endif /* PRC_MEMORY_GUARDS */

void *
prc_malloc(prc_context *ctx, size_t size)
{
    void *p;

    if (size == 0)
        return NULL;

#ifdef PRC_MEMORY_GUARDS
    {
        prc_guarded_block *block;

        /* Allocate extra space for guards */
        block = (prc_guarded_block *)ctx->hooks.malloc(ctx->hooks.opaque,
            sizeof(prc_guarded_block) + size + PRC_MEMORY_GUARD_SIZE);

        if (block == NULL)
            return NULL;

        prc_init_guards(block, size);
        p = (void *)(block + 1);
    }
#else
    p = ctx->hooks.malloc(ctx->hooks.opaque, size);
#endif

#if PRC_DEBUG_MEMORY
    if (ctx->current_memory_index >= ctx->debug_memory_size)
    {
        if (ctx->debug_memory_table_full_warned == 0)
        {
            printf("*** PRC_DEBUG_MEMORY TABLE FULL ***\n");
            printf("    Capacity reached: %zu entries\n", ctx->debug_memory_size);
            printf("    Further allocations will continue UNTRACKED.\n");
            ctx->debug_memory_table_full_warned = 1;
        }
        ctx->debug_memory_untracked_alloc_count++;
        return p;
    }
    if (p != NULL)
    {
        ctx->debug_memory[ctx->current_memory_index].data = p;
        ctx->debug_memory[ctx->current_memory_index].index = ctx->current_memory_index;
        ctx->debug_memory[ctx->current_memory_index].is_free = 0;
        ctx->debug_memory[ctx->current_memory_index].alloc_callsite = prc_debug_current_callsite();
        ctx->debug_memory[ctx->current_memory_index].free_callsite = NULL;
        ctx->current_memory_index++;
    }
#endif
    return p;
}

void *
prc_calloc(prc_context *ctx, size_t count, size_t size)
{
    void *p;
    size_t total_size;

    if (count == 0 || size == 0)
        return NULL;

    if (count > SIZE_MAX / size)
        return NULL;

    total_size = size * count;

#ifdef PRC_MEMORY_GUARDS
    {
        prc_guarded_block *block;

        /* Allocate extra space for guards */
        block = (prc_guarded_block *)ctx->hooks.malloc(ctx->hooks.opaque,
            sizeof(prc_guarded_block) + total_size + PRC_MEMORY_GUARD_SIZE);

        if (block == NULL)
            return NULL;

        prc_init_guards(block, total_size);
        p = (void *)(block + 1);
        memset(p, 0, total_size);
    }
#else
    p = ctx->hooks.malloc(ctx->hooks.opaque, total_size);
    if (p == NULL)
        return NULL;
    memset(p, 0, total_size);
#endif

#if PRC_DEBUG_MEMORY
    if (ctx->current_memory_index >= ctx->debug_memory_size)
    {
        if (ctx->debug_memory_table_full_warned == 0)
        {
            printf("*** PRC_DEBUG_MEMORY TABLE FULL ***\n");
            printf("    Capacity reached: %zu entries\n", ctx->debug_memory_size);
            printf("    Further allocations will continue UNTRACKED.\n");
            ctx->debug_memory_table_full_warned = 1;
        }
        ctx->debug_memory_untracked_alloc_count++;
        return p;
    }
    ctx->debug_memory[ctx->current_memory_index].data = p;
    ctx->debug_memory[ctx->current_memory_index].index = ctx->current_memory_index;
    ctx->debug_memory[ctx->current_memory_index].is_free = 0;
    ctx->debug_memory[ctx->current_memory_index].alloc_callsite = prc_debug_current_callsite();
    ctx->debug_memory[ctx->current_memory_index].free_callsite = NULL;
    ctx->current_memory_index++;
#endif
    return p;
}

void
prc_free(prc_context *ctx, void *p)
{
    if (p == NULL)
        return;

#ifdef PRC_MEMORY_GUARDS
    {
        prc_guarded_block *block;

        /* Check tracked pointer before any free */
        if (g_tracked_pointer != NULL)
        {
            prc_check_guards("prc_free (tracked check)", g_tracked_pointer);
        }

        /* Add pointer tracking to detect double-free */
        block = ((prc_guarded_block *)p) - 1;

        /* A front guard that no longer holds the live pattern means the
           header cannot be trusted, size included, so the back guard is
           not looked for. Freed blocks carry 0xDDDDDDDD there; anything
           else is corruption. This used to reject any block over 4 GiB
           as "likely garbage", which aborted a valid 8 GiB allocation the
           compressed encoder makes for a 146-million-corner mesh. */
        if (block->front_guard[0] != PRC_MEMORY_GUARD_PATTERN &&
            block->front_guard[0] != PRC_MEMORY_FREED_PATTERN)
        {
            printf("*** HEAP CORRUPTION DETECTED at prc_free ***\n");
            printf("    Pointer: %p\n", p);
            printf("    Front guard: 0x%08X (expected 0x%08X)\n",
                block->front_guard[0], PRC_MEMORY_GUARD_PATTERN);
#ifdef _WIN32
            __debugbreak();
#else
            __builtin_trap();
#endif
            return;
        }

        /* Check if this looks like already-freed memory BEFORE guard check */
        if (block->front_guard[0] == PRC_MEMORY_FREED_PATTERN)
        {
            printf("*** DOUBLE-FREE DETECTED ***\n");
            printf("    Pointer: %p\n", p);
            printf("    Front guard: 0x%08X (expected 0xDEADBEEF)\n", block->front_guard[0]);
            printf("    Size: %zu (likely garbage)\n", block->size);
            printf("    This pointer was already freed!\n");
#ifdef _WIN32
            __debugbreak();
#else
            __builtin_trap();
#endif
            return; /* Don't attempt to free again */
        }

        /* Check guards before freeing */
        prc_check_guards("prc_free", p);

#if PRC_DEBUG_MEMORY
        {
            size_t latest_index = 0;
            prc_debug_pointer_state state = prc_debug_get_latest_pointer_state(ctx, p, &latest_index);

            if (state == PRC_DEBUG_PTR_LIVE)
            {
                ctx->debug_memory[latest_index].is_free = 1;
                ctx->debug_memory[latest_index].free_callsite = prc_debug_current_callsite();
            }
            else if (state == PRC_DEBUG_PTR_FREED)
            {
                printf("*** DOUBLE-FREE DETECTED (PRC_DEBUG_MEMORY) ***\n");
                printf("    Pointer %p at latest debug index %zu already freed!\n", p, latest_index);
                printf("    Latest entry alloc_callsite=%p free_callsite=%p this_free_callsite=%p\n",
                    ctx->debug_memory[latest_index].alloc_callsite,
                    ctx->debug_memory[latest_index].free_callsite,
                    prc_debug_current_callsite());
                prc_debug_dump_pointer_history(ctx, p, 12);
#ifdef _WIN32
                __debugbreak();
#else
                __builtin_trap();
#endif
                return;
            }
            else
            {
                printf("*** FREE OF UNTRACKED POINTER (PRC_DEBUG_MEMORY) ***\n");
                printf("    Pointer %p was not found in debug allocation table.\n", p);
            }
        }
#endif
        /* Mark the block freed, so that freeing it again is recognised by
           the check above in any build. Nothing wrote this marker before:
           the check relied on the MSVC debug heap's own 0xDD fill of freed
           memory, so in a Release build a second free went undetected. */
        block->front_guard[0] = PRC_MEMORY_FREED_PATTERN;
        ctx->hooks.free(ctx->hooks.opaque, block);

        if (p == g_tracked_pointer)
        {
            printf("[TRACKING] Freed vertex_to_original: ptr=%p\n", p);
            g_tracked_pointer = NULL;
        }
    }
#else
#if PRC_DEBUG_MEMORY
    {
        size_t i;
        for (i = ctx->current_memory_index; i > 0; i--)
        {
            size_t idx = i - 1;
            if (ctx->debug_memory[idx].data == p)
            {
                if (ctx->debug_memory[idx].is_free == 0)
                    ctx->debug_memory[idx].is_free = 1;
                break;
            }
        }
    }
#endif
    ctx->hooks.free(ctx->hooks.opaque, p);
#endif
}

void *
prc_realloc(prc_context *ctx, void *p, size_t size)
{
    void *q;

    if (size == 0)
    {
        prc_free(ctx, p);
        return NULL;
    }

    if (p == NULL)
        return prc_malloc(ctx, size);

#ifdef PRC_MEMORY_GUARDS
    {
        prc_guarded_block *old_block, *new_block;

        /* Check tracked pointer before any realloc */
        if (g_tracked_pointer != NULL && g_tracked_pointer != p)
        {
            prc_check_guards("prc_realloc (tracked check)", g_tracked_pointer);
        }

        /* CRITICAL: Check guards BEFORE realloc */
        if (prc_check_guards("prc_realloc (before)", p))
        {
            printf("*** CORRUPTION DETECTED BEFORE REALLOC ***\n");
            printf("    Pointer being realloced: %p\n", p);
            printf("    Requested new size: %zu\n", size);
            printf("    This corruption happened BEFORE this realloc call!\n");
        }

        old_block = ((prc_guarded_block *)p) - 1;

        if (p == g_tracked_pointer)
        {
            printf("[TRACKING] Reallocing vertex_to_original: old_ptr=%p, old_size=%zu, new_size=%zu\n",
                p, old_block->size, size);
        }

        /* Allocate new block with guards */
        new_block = (prc_guarded_block *)ctx->hooks.realloc(ctx->hooks.opaque, old_block,
            sizeof(prc_guarded_block) + size + PRC_MEMORY_GUARD_SIZE);

        if (new_block == NULL)
            return NULL;

        q = (void *)(new_block + 1);

        /* Re-initialize guards with new size */
        prc_init_guards(new_block, size);

        if (p == g_tracked_pointer)
        {
            printf("[TRACKING] Realloced vertex_to_original: new_ptr=%p\n", q);
            g_tracked_pointer = q;
            g_tracked_size = size;
        }
    }
#else
    q = ctx->hooks.realloc(ctx->hooks.opaque, p, size);
#endif

#if PRC_DEBUG_MEMORY
    {
        size_t latest_index = 0;
        prc_debug_pointer_state state = prc_debug_get_latest_pointer_state(ctx, p, &latest_index);
        void *realloc_callsite = prc_debug_current_callsite();

        if (state == PRC_DEBUG_PTR_FREED)
        {
            printf("*** REALLOC OF FREED POINTER DETECTED (PRC_DEBUG_MEMORY) ***\n");
            printf("    Pointer %p at latest debug index %zu is already freed.\n", p, latest_index);
            printf("    Latest entry alloc_callsite=%p free_callsite=%p this_realloc_callsite=%p\n",
                ctx->debug_memory[latest_index].alloc_callsite,
                ctx->debug_memory[latest_index].free_callsite,
                realloc_callsite);
            prc_debug_dump_pointer_history(ctx, p, 12);
#ifdef _WIN32
            __debugbreak();
#else
            __builtin_trap();
#endif
            return NULL;
        }

        /* Treat realloc as: free old allocation event + new allocation event.
           This preserves ordering semantics when allocator reuses old addresses. */
        if (state == PRC_DEBUG_PTR_LIVE)
        {
            ctx->debug_memory[latest_index].is_free = 1;
            ctx->debug_memory[latest_index].free_callsite = realloc_callsite;
        }

        if (ctx->current_memory_index >= ctx->debug_memory_size)
        {
            if (ctx->debug_memory_table_full_warned == 0)
            {
                printf("*** PRC_DEBUG_MEMORY TABLE FULL ***\n");
                printf("    Capacity reached: %zu entries\n", ctx->debug_memory_size);
                printf("    Further allocations will continue UNTRACKED.\n");
                ctx->debug_memory_table_full_warned = 1;
            }
            ctx->debug_memory_untracked_alloc_count++;

            /* Best-effort fallback when table is full: reuse old slot if available. */
            if (state == PRC_DEBUG_PTR_LIVE)
            {
                ctx->debug_memory[latest_index].data = q;
                ctx->debug_memory[latest_index].is_free = 0;
                ctx->debug_memory[latest_index].alloc_callsite = realloc_callsite;
                ctx->debug_memory[latest_index].free_callsite = NULL;
            }
        }
        else
        {
            ctx->debug_memory[ctx->current_memory_index].data = q;
            ctx->debug_memory[ctx->current_memory_index].index = ctx->current_memory_index;
            ctx->debug_memory[ctx->current_memory_index].is_free = 0;
            ctx->debug_memory[ctx->current_memory_index].alloc_callsite = realloc_callsite;
            ctx->debug_memory[ctx->current_memory_index].free_callsite = NULL;
            ctx->current_memory_index++;
        }
    }
#endif
    return q;
}

/* Default methods if not set externally */
static void *
prc_malloc_default(void *opaque, size_t size)
{
    return malloc(size);
}

static void *
prc_realloc_default(void *opaque, void *old, size_t size)
{
    return realloc(old, size);
}

static void
prc_free_default(void *opaque, void *ptr)
{
    free(ptr);
}

prc_hooks prc_hooks_default =
{
    NULL,
    prc_malloc_default,
    prc_realloc_default,
    prc_free_default
};
