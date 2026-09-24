#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TM_PARALLEL_MAX_WORKERS 16

// worker is 0..TM_PARALLEL_MAX_WORKERS-1 and unique among the threads of one call
typedef void (*tmParallelFn)(int32_t index, int32_t worker, void *ctx);

// Runs fn for index 0..count-1 on the offline render pool and returns when all are done.
// Falls back to the calling thread if the pool is busy with another instance.
void tmParallelFor(int32_t count, tmParallelFn fn, void *ctx);

#ifdef __cplusplus
}
#endif
