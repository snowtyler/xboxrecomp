"""Compile the actual private attribute/coordinate readers without a GPU."""
import os
from pathlib import Path
import re
import subprocess
import pytest
from tools.recomp import c_fixture

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("enabled", [False, True])
def test_current_attributes_and_mirror(tmp_path, enabled):
    source = (ROOT / "src/kernel/nv2a_pb_exec.c").read_text()
    attribute = re.search(r"typedef struct \{[^{}]+\} VertexAttr;", source)[0]
    def body(name):
        start = source.index("static ", source.index(name) - 20)
        return source[start:source.index("\n}", start) + 2]
    code = r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include "nv2a_render_diagnostics.h"
#define NV_VERTEX_ATTRS 16
''' + attribute + r'''
static unsigned char memory[4096];
static void *xbox_GetMemoryOffset(void) { return memory; }
static struct { VertexAttr attr[16]; float imm_attr[16][4];
    int inline_active; unsigned inline_count; uint32_t inline_buf[64]; } s_gpu;
''' + body("static int fetch_attr(") + "\n" + body("static uint32_t wrap_coord(") + r'''
int main(void) {
    float out[4];
    for (unsigned i=0;i<16;i++) {
        s_gpu.imm_attr[i][0]=(float)(i+1);s_gpu.imm_attr[i][3]=.5f;
        int read=fetch_attr(&s_gpu.attr[i],17,out);
        assert(read==EXPECT);
        assert(out[0]==(EXPECT?(float)(i+1):0));
        assert(out[3]==(EXPECT?.5f:1));
    }
    s_gpu.attr[3].size=4; /* A declared array with no stride is not current data. */
    assert(!fetch_attr(&s_gpu.attr[3],0,out)&&out[0]==0&&out[3]==1);
    assert(wrap_coord(4,4,2)==3);
    assert(wrap_coord(7,4,2)==(EXPECT?0:3));
    assert(wrap_coord((uint32_t)-2,4,2)==(EXPECT?1:0));
    assert(wrap_coord((uint32_t)-1,4,1)==3);
    return 0;
}
'''
    compiler = c_fixture.compiler()
    if not compiler:
        pytest.skip("C compiler unavailable")
    c, exe = tmp_path / "test.c", tmp_path / "test.exe"
    c.write_text(code)
    subprocess.run(c_fixture.command(compiler, [c], exe,
        include_dirs=[ROOT / "src/kernel"], defines=[f"EXPECT={int(enabled)}"]),
        check=True, capture_output=True, cwd=tmp_path)
    env = dict(os.environ, RECOMP_NV2A_V015_RENDER=str(int(enabled)))
    subprocess.run([str(exe)], env=env, check=True)
