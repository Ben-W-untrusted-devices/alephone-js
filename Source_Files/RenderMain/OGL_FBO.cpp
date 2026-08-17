/*

	Copyright (C) 2015 and beyond by Jeremiah Morris
	and the "Aleph One" developers.
 
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	This license is contained in the file "COPYING",
	which is included with this source code; it is available online at
	http://www.gnu.org/licenses/gpl.html
	
	Framebuffer Object utilities
*/

#include "cseries.h"
#include "OGL_FBO.h"

#ifdef HAVE_OPENGL

#include "OGL_Setup.h"
#include "OGL_Render.h"
#include "OGL_Textures.h"

std::vector<FBO *> FBO::active_chain;

// Web port (see ../../WEB_PORT_PLAN.md, M6b): WebGL 1.0 has no
// GL_FRAMEBUFFER_SRGB capability, so enabling or disabling it raises
// INVALID_ENUM. That is harmless in itself, but it happens several times per
// frame and would leave a stale error sitting in glGetError() for every
// diagnostic downstream of it to trip over. Route the toggles through one
// place that does nothing here, rather than editing each call site.
static inline void A1_SetFramebufferSRGB(bool enable)
{
#ifdef __EMSCRIPTEN__
	(void)enable;
#else
	if (enable)
		glEnable(GL_FRAMEBUFFER_SRGB_EXT);
	else
		glDisable(GL_FRAMEBUFFER_SRGB_EXT);
#endif
}

FBO::FBO(GLuint w, GLuint h, bool srgb) : _h(h), _w(w), _srgb(srgb) {
	glGenFramebuffersEXT(1, &_fbo);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, _fbo);
	
	glGenRenderbuffersEXT(1, &_depthBuffer);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, _depthBuffer);
	// Web port (see ../../WEB_PORT_PLAN.md, M6b): the context here is WebGL 1.0,
	// which takes exactly the *opposite* sizing convention to desktop GL for
	// these two calls, and rejects the desktop spellings outright:
	//
	//   - renderbuffer storage must be a *sized* format. Unsized
	//     GL_DEPTH_COMPONENT is not in WebGL 1.0's list (RGBA4, RGB565,
	//     RGB5_A1, DEPTH_COMPONENT16, STENCIL_INDEX8, DEPTH_STENCIL), so it
	//     raises INVALID_ENUM and the renderbuffer is left with no storage.
	//   - glTexImage2D's internalformat must be *unsized*, and must equal the
	//     format argument. GL_RGB8 is a WebGL 2 spelling; GL_SRGB needs the
	//     EXT_sRGB extension. Either one raises INVALID_ENUM and the texture
	//     is left with no storage.
	//
	// Either failure alone leaves the framebuffer incomplete, which is what
	// tripped the assertion below on the first in-browser run. Both attachments
	// still end up 8-bit-per-channel colour and >=16-bit depth; the only real
	// loss is sRGB-correct blending, which WebGL 1.0 cannot express here.
#ifdef __EMSCRIPTEN__
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT16, _w, _h);
#else
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT, _w, _h);
#endif
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, _depthBuffer);

	glGenTextures(1, &texID);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, texID);
#ifdef __EMSCRIPTEN__
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB, _w, _h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
#else
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, srgb ? GL_SRGB : GL_RGB8, _w, _h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
#endif
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, TxtrTypeInfoList[OGL_Txtr_HUD].NearFilter);
#ifdef __EMSCRIPTEN__
	// GL_TEXTURE_RECTANGLE_ARB is GL_TEXTURE_2D here (see
	// OGL_Emscripten_Compat.h), so unlike a real rectangle target -- which
	// rejects mipmap minification filters with INVALID_ENUM and so silently
	// keeps GL_LINEAR -- a mipmapped HUD filter preference would actually take
	// effect, and leave this (mipmap-less) attachment texture incomplete.
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// Likewise, the render target is the window size and so routinely
	// non-power-of-two; WebGL 1.0 only samples NPOT textures with CLAMP wrap.
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, TxtrTypeInfoList[OGL_Txtr_HUD].FarFilter);
#endif
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, texID, 0);
#ifdef __EMSCRIPTEN__
	// Web port: report *which* completeness failure happened rather than only
	// asserting, since there is no debugger to inspect the status in.
	{
		GLenum fbo_status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
		if (fbo_status != GL_FRAMEBUFFER_COMPLETE_EXT)
			fprintf(stderr, "[gl] FBO %ux%u incomplete: status=0x%x (last error 0x%x)\n",
			        _w, _h, fbo_status, glGetError());
	}
#endif
	assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

void FBO::activate(bool clear, GLuint fboTarget) {
	if (!active_chain.size() || active_chain.back() != this) {
		active_chain.push_back(this);
		_fboTarget = fboTarget;
		glBindFramebufferEXT(fboTarget, _fbo);
		glPushAttrib(GL_VIEWPORT_BIT);
		glViewport(0, 0, _w, _h);
		A1_SetFramebufferSRGB(_srgb);
		if (clear)
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
}

void FBO::deactivate() {
	if (active_chain.size() && active_chain.back() == this) {
		active_chain.pop_back();
		glPopAttrib();
		
		GLuint prev_fbo = 0;
		bool prev_srgb = Using_sRGB;
		if (active_chain.size()) {
			prev_fbo = active_chain.back()->_fbo;
			prev_srgb = active_chain.back()->_srgb;
		}
		glBindFramebufferEXT(_fboTarget, prev_fbo);
		A1_SetFramebufferSRGB(prev_srgb);
	}
}

void FBO::draw() {
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, texID);
	glEnable(GL_TEXTURE_RECTANGLE_ARB);
	// Web port (see ../../WEB_PORT_PLAN.md, M6b): rectangle textures are
	// ordinary 2D textures here, which address texels in normalized [0,1]
	// rather than pixels -- so the same rect is expressed as 0..1 instead of
	// 0.._w/0.._h. The vertex extents are unchanged; only the texture
	// coordinates differ.
#ifdef __EMSCRIPTEN__
	OGL_RenderTexturedRect(0, 0, _w, _h, 0, 1, 1, 0);
#else
	OGL_RenderTexturedRect(0, 0, _w, _h, 0, _h, _w, 0);
#endif
	glDisable(GL_TEXTURE_RECTANGLE_ARB);
}

void FBO::prepare_drawing_mode(bool blend) {
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	
	glDisable(GL_DEPTH_TEST);
	if (!blend)
		glDisable(GL_BLEND);
	
	glOrtho(0, _w, _h, 0, -1, 1);
	glColor4f(1.0, 1.0, 1.0, 1.0);
}

void FBO::reset_drawing_mode() {
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

void FBO::draw_full(bool blend) {
	prepare_drawing_mode(blend);
	draw();
	reset_drawing_mode();
}

FBO::~FBO() {
	glDeleteFramebuffersEXT(1, &_fbo);
	glDeleteRenderbuffersEXT(1, &_depthBuffer);
}


void FBOSwapper::activate() {
	if (active)
		return;
	if (draw_to_first)
		first.activate(clear_on_activate);
	else
		second.activate(clear_on_activate);
	active = true;
	clear_on_activate = false;
}

void FBOSwapper::deactivate() {
	if (!active)
		return;
	if (draw_to_first)
		first.deactivate();
	else
		second.deactivate();
	active = false;
}

void FBOSwapper::swap() {
	deactivate();
	draw_to_first = !draw_to_first;
	clear_on_activate = true;
}

void FBOSwapper::draw(bool blend) {
	current_contents().draw_full(blend);
}

void FBOSwapper::filter(bool blend) {
	activate();
	draw(blend);
	swap();
}

void FBOSwapper::copy(FBO& other, bool srgb) {
	clear_on_activate = true;
	activate();
	other.draw_full(false);
	swap();
}

void FBOSwapper::blend(FBO& other, bool srgb) {
	activate();
	A1_SetFramebufferSRGB(srgb);
	other.draw_full(true);
	deactivate();
}

void FBOSwapper::blend_multisample(FBO& other) {
	swap();
	activate();
	
	// set up FBO passed in as texture #1
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, other.texID);
	glEnable(GL_TEXTURE_RECTANGLE_ARB);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	
	glClientActiveTextureARB(GL_TEXTURE1_ARB);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	// Web port (see ../../WEB_PORT_PLAN.md, M6b): normalized coordinates for
	// the same reason as FBO::draw() above. Also float rather than GL_INT --
	// GLES has no integer vertex-attribute format for this.
#ifdef __EMSCRIPTEN__
	GLfloat multi_coordinates[8] = { 0, 1, 1, 1, 1, 0, 0, 0 };
	glTexCoordPointer(2, GL_FLOAT, 0, multi_coordinates);
#else
	GLint multi_coordinates[8] = { 0, GLint(other._h), GLint(other._w), GLint(other._h), GLint(other._w), 0, 0, 0 };
	glTexCoordPointer(2, GL_INT, 0, multi_coordinates);
#endif
	glClientActiveTextureARB(GL_TEXTURE0_ARB);
	
	draw(true);
	
	// tear down multitexture stuff
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glDisable(GL_TEXTURE_RECTANGLE_ARB);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	
	glClientActiveTextureARB(GL_TEXTURE1_ARB);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glClientActiveTextureARB(GL_TEXTURE0_ARB);
	
	deactivate();
}

#endif
