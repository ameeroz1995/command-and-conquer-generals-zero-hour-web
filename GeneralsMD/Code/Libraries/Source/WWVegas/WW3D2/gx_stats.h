// gx_stats.h — counters for the benchmark (Main/WebBenchmark.cpp), web port.
// Every GL call that uploads data, switches programs, binds textures or draws goes through
// these wrappers (included from gles3_wrapper.h after the GL headers), so the benchmark can
// report per frame: bytes sent to the graphics chip, triangles, program switches and texture
// binds. Cost: an add per call.
#pragma once
#include <GLES3/gl3.h>

extern "C" {
extern double gx_stat_upload_bytes;	// buffer + texture data sent to the graphics chip
extern double gx_stat_triangles;		// triangles drawn (instances counted)
extern unsigned gx_stat_gl_draws;		// GL draw calls
extern unsigned gx_stat_programs;		// glUseProgram calls
extern unsigned gx_stat_tex_binds;		// glBindTexture calls
extern unsigned gx_stat_shader_compiles;	// glCompileShader calls
}

static inline unsigned gx_texel_bytes(GLenum format, GLenum type) {
	if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1) return 2;
	unsigned c = (format == GL_RGBA || format == GL_RGBA_INTEGER) ? 4 : (format == GL_RGB || format == GL_RGB_INTEGER) ? 3
		: (format == GL_RG || format == GL_LUMINANCE_ALPHA) ? 2 : 1;
	unsigned b = (type == GL_FLOAT || type == GL_UNSIGNED_INT || type == GL_INT) ? 4 : (type == GL_HALF_FLOAT || type == GL_UNSIGNED_SHORT || type == GL_SHORT) ? 2 : 1;
	return c * b;
}
static inline void gx_wrap_glBufferData(GLenum t, GLsizeiptr n, const void *d, GLenum u) { if (d) gx_stat_upload_bytes += (double)n; glBufferData(t, n, d, u); }
static inline void gx_wrap_glBufferSubData(GLenum t, GLintptr o, GLsizeiptr n, const void *d) { gx_stat_upload_bytes += (double)n; glBufferSubData(t, o, n, d); }
static inline void gx_wrap_glTexImage2D(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *p) {
	if (p) gx_stat_upload_bytes += (double)w * h * gx_texel_bytes(f, ty); glTexImage2D(t, l, i, w, h, b, f, ty, p); }
static inline void gx_wrap_glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *p) {
	gx_stat_upload_bytes += (double)w * h * gx_texel_bytes(f, ty); glTexSubImage2D(t, l, x, y, w, h, f, ty, p); }
static inline void gx_wrap_glCompressedTexImage2D(GLenum t, GLint l, GLenum i, GLsizei w, GLsizei h, GLint b, GLsizei n, const void *d) {
	if (d) gx_stat_upload_bytes += (double)n; glCompressedTexImage2D(t, l, i, w, h, b, n, d); }
static inline void gx_wrap_glCompileShader(GLuint s) { gx_stat_shader_compiles++; glCompileShader(s); }
static inline void gx_wrap_glUseProgram(GLuint p) { gx_stat_programs++; glUseProgram(p); }
static inline void gx_wrap_glBindTexture(GLenum t, GLuint x) { gx_stat_tex_binds++; glBindTexture(t, x); }
static inline double gx_tris(GLenum m, GLsizei n) { return m == GL_TRIANGLES ? n / 3.0 : (m == GL_TRIANGLE_STRIP || m == GL_TRIANGLE_FAN) ? (n > 2 ? n - 2 : 0) : 0; }
static inline void gx_wrap_glDrawElements(GLenum m, GLsizei n, GLenum ty, const void *o) { gx_stat_gl_draws++; gx_stat_triangles += gx_tris(m, n); glDrawElements(m, n, ty, o); }
static inline void gx_wrap_glDrawElementsInstanced(GLenum m, GLsizei n, GLenum ty, const void *o, GLsizei k) { gx_stat_gl_draws++; gx_stat_triangles += gx_tris(m, n) * k; glDrawElementsInstanced(m, n, ty, o, k); }
static inline void gx_wrap_glDrawArrays(GLenum m, GLint f, GLsizei n) { gx_stat_gl_draws++; gx_stat_triangles += gx_tris(m, n); glDrawArrays(m, f, n); }

#define glBufferData gx_wrap_glBufferData
#define glBufferSubData gx_wrap_glBufferSubData
#define glTexImage2D gx_wrap_glTexImage2D
#define glTexSubImage2D gx_wrap_glTexSubImage2D
#define glCompressedTexImage2D gx_wrap_glCompressedTexImage2D
#define glUseProgram gx_wrap_glUseProgram
#define glCompileShader gx_wrap_glCompileShader
#define glBindTexture gx_wrap_glBindTexture
#define glDrawElements gx_wrap_glDrawElements
#define glDrawElementsInstanced gx_wrap_glDrawElementsInstanced
#define glDrawArrays gx_wrap_glDrawArrays
