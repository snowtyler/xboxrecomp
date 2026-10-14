#ifndef NV2A_VSH_HLSL_H
#define NV2A_VSH_HLSL_H
#include <stddef.h>
#include "nv2a_vsh_interp.h"
/* Pure translation of the kernel interpreter's encoding. Constant-writing
 * programs require ordered shared state and are rejected for GPU execution. */
int nv2a_vsh_hlsl(const Nv2aVshSnapshot *program,char *buffer,size_t capacity);
/* Conservative union of every input reference emitted before END. Invalid
 * snapshots request all attributes; constants and register sources add none. */
uint32_t nv2a_vsh_input_mask(const Nv2aVshSnapshot *program);
#endif
