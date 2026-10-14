#ifndef NV2A_COMBINER_HLSL_H
#define NV2A_COMBINER_HLSL_H
#include <stddef.h>
#include "nv2a_combiner.h"
/* Emit the exact register arithmetic, with constants supplied through b0.
 * Texture fetch and interpolation are supplied by the calling pixel shader. */
int nv2a_rc_hlsl(const Nv2aCombiner *rc, char *buffer, size_t capacity);
typedef struct { float factor0[8][4], factor1[8][4], final0[4], final1[4]; } Nv2aRcGpuConstants;
void nv2a_rc_gpu_constants(const Nv2aCombiner *rc, Nv2aRcGpuConstants *out);
#endif
