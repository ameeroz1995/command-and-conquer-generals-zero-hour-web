/*
** GeneralsX Web Port — WebGPU drawing (Settings switch "webgpu"; docs/web-port/WEBGPU_PLAN.md)
**
** When the page hands the game a GPUDevice (Module.gxGPU), the D3D8 shim draws through
** WebGPU instead of WebGL2. The render states still land in the GLES3 state tracker
** (gles3_wrapper.cpp); a draw resolves them into a pipeline, uniform slices and a texture bind
** group, recorded as a few ints. One JS call per batch replays the records into a render pass.
** WebGL2 remains the default and the fallback: a WebGL2 context still exists on a hidden
** canvas, so any GL call this port does not divert stays harmless.
**
** LICENSE: GPL-3.0
*/
#pragma once
#if defined(__EMSCRIPTEN__)
#include <cstddef>
#include <cstdint>

bool GXW_On(); // WebGPU drawing active
bool GXW_Init(); // GLES3_Init: true when the page provided a device and setup worked

// Buffers (d3d8.h Emscripten_BufferData). Create returns an id > 0. Write copies at call time
// (queue.writeBuffer), after flushing recorded draws that read the bytes it overwrites.
unsigned GXW_Buffer_Create(size_t size, bool index);
void GXW_Buffer_Write(unsigned id, size_t lo, size_t hi, const uint8_t *data);
void GXW_Buffer_Destroy(unsigned id);

// Textures (d3d8.h Upload_To_GL). dx8_format: D3DFMT_* value. Level pixels in D3D layout.
unsigned GXW_Texture_Create(unsigned w, unsigned h, unsigned levels, unsigned dx8_format);
void GXW_Texture_Write(unsigned id, unsigned level, unsigned w, unsigned h, unsigned dx8_format,
                       const void *pixels, bool build_mips);
void GXW_Texture_Destroy(unsigned id);

// Draws (d3d8.h device). Buffer ids are 0 for the client-memory (UP) variants, which pass the
// bytes instead. Indices are 16 bit unless index32.
void GXW_Draw_Indexed(unsigned prim, unsigned prim_count, unsigned fvf, unsigned stride,
                      unsigned vb, size_t vb_size, unsigned base_vertex, unsigned min_vertex,
                      unsigned num_vertices, unsigned ib, unsigned start_index,
                      const uint8_t *ib_cpu, size_t ib_size);
void GXW_Draw_Arrays(unsigned prim, unsigned prim_count, unsigned fvf, unsigned stride,
                     unsigned vb, unsigned start_vertex);
void GXW_Draw_UP(unsigned prim, unsigned prim_count, unsigned fvf, unsigned stride,
                 const void *vertices, unsigned num_vertices,
                 const void *indices, bool index32);

// Batched units: the next GXW_Draw_Indexed draws n instances, each with its own world matrix (D3D
// row-major) and stage 0 offset, or (skinned) its own bones*3 rows of `palette` with the bone number
// of each vertex in bone_buffer (GXW_Bone_Buffer).
void GXW_Set_Instances(unsigned n, const float* world_row_major, const float* uv, const float* palette,
                       unsigned bones, unsigned bone_buffer);
unsigned GXW_Bone_Buffer(const unsigned short* bones, unsigned count);

void GXW_Clear(bool color, bool zs, float r, float g, float b, float a, float z, unsigned stencil);
void GXW_Viewport(int x, int y, int w, int h, float min_z, float max_z); // top-left origin, pixels
void GXW_Present(); // end of frame: submit everything recorded
#endif
