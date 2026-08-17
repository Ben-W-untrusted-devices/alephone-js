#ifndef _OGL_EMSCRIPTEN_COMPAT_
#define _OGL_EMSCRIPTEN_COMPAT_

/*
	Web port (see ../../WEB_PORT_PLAN.md, M6b) -- not part of the upstream
	Aleph One sources.

	Emscripten's GL headers declare the full legacy desktop API, so all of
	this engine's GL code *compiles* unchanged; only a handful of entry
	points are missing at link time. -sLEGACY_GL_EMULATION=1 supplies almost
	all of them (the whole ARB shader-object API, the client-array family,
	the fixed-function matrix/fog/alpha-test state, glPushAttrib/glPopAttrib,
	glClipPlane, the EXT FBO calls). This header covers the small remainder
	that the emulation does not implement, and that can be satisfied without
	changing behavior.

	Everything here is a macro rename onto a static inline of our own rather
	than a redefinition, because the real names are already *declared* by
	Emscripten's <GL/gl.h> / <GL/glu.h> -- defining them directly would
	conflict with those declarations.

	Entry points that need a real behavioral decision rather than a drop-in
	substitute (display lists, glLogicOp, glPolygonStipple) are handled at
	their call sites instead, so the reasoning stays next to the code it
	affects.
*/

#if defined(__EMSCRIPTEN__) && defined(HAVE_OPENGL)

#include <algorithm>

// This codebase uses the desktop-GL spelling with a lowercase "sRGB";
// Emscripten's <GL/glext.h> only defines the uppercase form (same value,
// 0x8DB9). Note that WebGL controls sRGB encoding through the framebuffer's
// format rather than an enable bit, so enabling this is expected to be a
// no-op there -- it only needs to compile, and the engine gates every use
// on its own Using_sRGB preference.
#ifndef GL_FRAMEBUFFER_sRGB
#define GL_FRAMEBUFFER_sRGB GL_FRAMEBUFFER_SRGB
#endif

// GLES has no double-precision state queries. Every caller in this codebase
// wants a matrix it is about to do CPU-side maths with, and the underlying
// GL state is float regardless, so widening glGetFloatv loses nothing.
static inline void A1_glGetDoublev(GLenum pname, GLdouble* params)
{
	GLfloat fparams[16];
	glGetFloatv(pname, fparams);

	// 16 covers the matrix queries this is used for (GL_PROJECTION_MATRIX,
	// GL_MODELVIEW_MATRIX); anything smaller simply ignores the tail.
	for (int i = 0; i < 16; i++)
		params[i] = fparams[i];
}
#define glGetDoublev A1_glGetDoublev

// GLU does not exist under Emscripten. gluScaleImage is the only GLU
// function this engine uses (OGL_Textures.cpp, ImageLoader_Shared.cpp), and
// both callers pass GL_RGBA/GL_UNSIGNED_BYTE, so a plain bilinear RGBA8
// resample is a faithful replacement. Returns 0 on success like the real
// one; anything other than RGBA8 is refused rather than silently mangled.
static inline GLint A1_gluScaleImage(GLenum format,
                                     GLsizei wIn, GLsizei hIn, GLenum typeIn, const void* dataIn,
                                     GLsizei wOut, GLsizei hOut, GLenum typeOut, GLvoid* dataOut)
{
	if (format != GL_RGBA || typeIn != GL_UNSIGNED_BYTE || typeOut != GL_UNSIGNED_BYTE)
		return GL_INVALID_ENUM;
	if (wIn <= 0 || hIn <= 0 || wOut <= 0 || hOut <= 0 || !dataIn || !dataOut)
		return GL_INVALID_VALUE;

	const unsigned char* src = static_cast<const unsigned char*>(dataIn);
	unsigned char* dst = static_cast<unsigned char*>(dataOut);

	// Map each output texel to a source position, sampling the 2x2
	// neighbourhood around it. Using (out + 0.5) keeps the sample grid
	// centred, which matters when downscaling by large factors.
	for (GLsizei y = 0; y < hOut; y++)
	{
		const float sy = (hOut > 1) ? ((y + 0.5f) * hIn / hOut - 0.5f) : 0.0f;
		const int y0 = std::max(0, std::min(hIn - 1, static_cast<int>(sy)));
		const int y1 = std::max(0, std::min(hIn - 1, y0 + 1));
		const float fy = std::max(0.0f, sy - static_cast<float>(y0));

		for (GLsizei x = 0; x < wOut; x++)
		{
			const float sx = (wOut > 1) ? ((x + 0.5f) * wIn / wOut - 0.5f) : 0.0f;
			const int x0 = std::max(0, std::min(wIn - 1, static_cast<int>(sx)));
			const int x1 = std::max(0, std::min(wIn - 1, x0 + 1));
			const float fx = std::max(0.0f, sx - static_cast<float>(x0));

			const unsigned char* p00 = src + 4 * (static_cast<size_t>(y0) * wIn + x0);
			const unsigned char* p01 = src + 4 * (static_cast<size_t>(y0) * wIn + x1);
			const unsigned char* p10 = src + 4 * (static_cast<size_t>(y1) * wIn + x0);
			const unsigned char* p11 = src + 4 * (static_cast<size_t>(y1) * wIn + x1);
			unsigned char* out = dst + 4 * (static_cast<size_t>(y) * wOut + x);

			for (int c = 0; c < 4; c++)
			{
				const float top = p00[c] + (p01[c] - p00[c]) * fx;
				const float bot = p10[c] + (p11[c] - p10[c]) * fx;
				out[c] = static_cast<unsigned char>(top + (bot - top) * fy + 0.5f);
			}
		}
	}

	return 0;
}
#define gluScaleImage A1_gluScaleImage

// The other GLU function this engine uses. GLES3/WebGL2 has a native
// equivalent that is strictly better than GLU's CPU downsampling: upload
// level 0 and let the driver build the chain. Same call signature so the two
// call sites in OGL_Textures.cpp need no change.
static inline GLint A1_gluBuild2DMipmaps(GLenum target, GLint internalFormat,
                                         GLsizei width, GLsizei height,
                                         GLenum format, GLenum type, const void* data)
{
	glTexImage2D(target, 0, internalFormat, width, height, 0, format, type, data);
	glGenerateMipmap(target);
	return 0;
}
#define gluBuild2DMipmaps A1_gluBuild2DMipmaps

// ARB-suffixed multitexture/compression aliases. Emscripten provides the
// core names (glActiveTexture and glCompressedTexImage2D from the WebGL
// bindings, glClientActiveTexture from the legacy emulation) but not these
// historical spellings, which is all this engine uses.
#define glActiveTextureARB glActiveTexture
#define glClientActiveTextureARB glClientActiveTexture
#define glCompressedTexImage2DARB glCompressedTexImage2D

// glMultiTexCoord4f has no counterpart at all -- it is immediate-mode
// per-vertex state, and the emulation only handles texture coordinates fed
// through client arrays. Its two call sites (RenderRasterize_Shader.cpp)
// both push a constant tangent vector for bump mapping, which is already
// unavailable here for a separate reason: the bump shader also needs
// gl_NormalMatrix, one of the two legacy built-ins the emulation does not
// rewrite. So this is a no-op rather than an approximation -- surfaces
// render without tangent data, and bump mapping stays on the deferred list
// with its gl_NormalMatrix sibling.
static inline void A1_glMultiTexCoord4f(GLenum, GLfloat, GLfloat, GLfloat, GLfloat) {}
#define glMultiTexCoord4fARB A1_glMultiTexCoord4f

// --- ARB_shader_objects -> core GL2 -------------------------------------
//
// This engine drives shaders entirely through the historical
// ARB_shader_objects spelling (OGL_Shader.cpp/.h). Emscripten implements
// only the core GL2 names, so map them. Most are exact renames; the ARB
// status/info-log enums even share their values with the core ones
// (GL_OBJECT_COMPILE_STATUS_ARB == GL_COMPILE_STATUS == 0x8B81, etc.), so
// they pass straight through.
#define glCreateShaderObjectARB glCreateShader
#define glShaderSourceARB glShaderSource
#define glCompileShaderARB glCompileShader
#define glCreateProgramObjectARB glCreateProgram
#define glAttachObjectARB glAttachShader
#define glLinkProgramARB glLinkProgram
#define glUseProgramObjectARB glUseProgram
#define glGetUniformLocationARB glGetUniformLocation
#define glUniform1iARB glUniform1i
#define glUniform1fARB glUniform1f
#define glUniformMatrix4fvARB glUniformMatrix4fv

// The two ARB calls that took any object handle, where core GL splits the
// entry point by object type. Rather than edit each call site (and diverge
// from the native build), dispatch on what the handle actually is --
// glIsProgram is core GL2 and answers exactly that question.
static inline void A1_glDeleteObject(GLuint obj)
{
	if (glIsProgram(obj))
		glDeleteProgram(obj);
	else
		glDeleteShader(obj);
}
#define glDeleteObjectARB A1_glDeleteObject

static inline void A1_glGetObjectParameteriv(GLuint obj, GLenum pname, GLint* params)
{
	if (glIsProgram(obj))
		glGetProgramiv(obj, pname, params);
	else
		glGetShaderiv(obj, pname, params);
}
#define glGetObjectParameterivARB A1_glGetObjectParameteriv

static inline void A1_glGetInfoLog(GLuint obj, GLsizei maxLength, GLsizei* length, GLchar* infoLog)
{
	if (glIsProgram(obj))
		glGetProgramInfoLog(obj, maxLength, length, infoLog);
	else
		glGetShaderInfoLog(obj, maxLength, length, infoLog);
}
#define glGetInfoLogARB A1_glGetInfoLog

// --- EXT_framebuffer_object -> core ------------------------------------
//
// OGL_FBO.cpp (and Movie.cpp) use the EXT spelling throughout. FBOs are
// core in GLES3/WebGL2 under the unsuffixed names, and the enum values are
// unchanged, so these are pure renames.
#define glGenFramebuffersEXT glGenFramebuffers
#define glBindFramebufferEXT glBindFramebuffer
#define glDeleteFramebuffersEXT glDeleteFramebuffers
#define glFramebufferTexture2DEXT glFramebufferTexture2D
#define glFramebufferRenderbufferEXT glFramebufferRenderbuffer
#define glCheckFramebufferStatusEXT glCheckFramebufferStatus
#define glGenRenderbuffersEXT glGenRenderbuffers
#define glBindRenderbufferEXT glBindRenderbuffer
#define glDeleteRenderbuffersEXT glDeleteRenderbuffers
#define glRenderbufferStorageEXT glRenderbufferStorage
#define glBlitFramebufferEXT glBlitFramebuffer

// --- glPushAttrib / glPopAttrib ----------------------------------------
//
// No equivalent exists in GLES, and the emulation does not provide one.
// A faithful implementation would have to capture the entire GL state
// vector, but this engine only ever pushes four masks
// (GL_ALL_ATTRIB_BITS, GL_ENABLE_BIT, GL_VIEWPORT_BIT, GL_MATRIX_MODE) and
// only perturbs a small, knowable amount of state inside those blocks --
// blending, the depth/stencil/scissor/cull toggles, texturing, the
// viewport, the matrix mode and the current colour. Save that superset
// regardless of the mask and restore it on pop: over-restoring is safe
// here because every caller re-establishes what it needs anyway, whereas
// under-restoring would leak state between passes.
//
// Deliberately does not touch GL_ALPHA_TEST/GL_FOG/GL_LIGHTING: those are
// emulated state, and round-tripping them through glIsEnabled risks
// setting a GL error that the engine's own glGetError checks would then
// misattribute.
struct A1_GLAttribState {
	GLboolean texture2D, blend, depthTest, cullFace, scissorTest, stencilTest;
	GLboolean depthMask;
	GLint viewport[4];
	GLint matrixMode;
	GLint blendSrc, blendDst;
	GLfloat color[4];
};

// 16 deep matches the real GL minimum for the attribute stack; this engine
// never nests more than two.
static A1_GLAttribState A1_attribStack[16];
static int A1_attribStackDepth = 0;

static inline void A1_glPushAttrib(GLbitfield)
{
	if (A1_attribStackDepth >= 16) return;
	A1_GLAttribState& s = A1_attribStack[A1_attribStackDepth++];

	s.texture2D = glIsEnabled(GL_TEXTURE_2D);
	s.blend = glIsEnabled(GL_BLEND);
	s.depthTest = glIsEnabled(GL_DEPTH_TEST);
	s.cullFace = glIsEnabled(GL_CULL_FACE);
	s.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
	s.stencilTest = glIsEnabled(GL_STENCIL_TEST);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &s.depthMask);
	glGetIntegerv(GL_VIEWPORT, s.viewport);
	glGetIntegerv(GL_MATRIX_MODE, &s.matrixMode);
	glGetIntegerv(GL_BLEND_SRC, &s.blendSrc);
	glGetIntegerv(GL_BLEND_DST, &s.blendDst);
	glGetFloatv(GL_CURRENT_COLOR, s.color);
}

static inline void A1_glPopAttrib(void)
{
	if (A1_attribStackDepth <= 0) return;
	const A1_GLAttribState& s = A1_attribStack[--A1_attribStackDepth];

	auto setEnabled = [](GLenum cap, GLboolean on) {
		if (on) glEnable(cap); else glDisable(cap);
	};
	setEnabled(GL_TEXTURE_2D, s.texture2D);
	setEnabled(GL_BLEND, s.blend);
	setEnabled(GL_DEPTH_TEST, s.depthTest);
	setEnabled(GL_CULL_FACE, s.cullFace);
	setEnabled(GL_SCISSOR_TEST, s.scissorTest);
	setEnabled(GL_STENCIL_TEST, s.stencilTest);
	glDepthMask(s.depthMask);
	glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
	glMatrixMode(s.matrixMode);
	glBlendFunc(s.blendSrc, s.blendDst);
	glColor4f(s.color[0], s.color[1], s.color[2], s.color[3]);
}
#define glPushAttrib A1_glPushAttrib
#define glPopAttrib A1_glPopAttrib

#endif // __EMSCRIPTEN__ && HAVE_OPENGL

#endif
