#ifndef _OGL_HEADERS_
#define _OGL_HEADERS_

/*

	Copyright (C) 2009 by Gregory Smith
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

	Uniform header for all Aleph One OpenGL users
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_OPENGL

#ifdef __EMSCRIPTEN__

// Web port (see ../../WEB_PORT_PLAN.md, M6b): Emscripten ships
// desktop-style GL headers that declare the full legacy API, which is what
// lets this engine's fixed-function code compile unchanged against
// -sLEGACY_GL_EMULATION=1. Include those directly rather than going through
// SDL_opengl.h, and pick up our shims for the few entry points the
// emulation doesn't implement.
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glu.h>

#include "OGL_Emscripten_Compat.h"

#elif defined(__WIN32__)


#define GLEW_STATIC 1
#include <GL/glew.h>

#else

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include <SDL2/SDL_opengl.h>

#if defined (__APPLE__) && defined(__MACH__)
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

#endif

// Web port (see ../../WEB_PORT_PLAN.md, M6b): GLES/WebGL has no GL_DOUBLE
// vertex format, but this engine keeps its intermediate vertex/texcoord
// arrays in GLdouble and hands them straight to glVertexPointer. Desktop
// drivers convert those to float on upload anyway, so nothing that reaches
// the GPU is lost by storing float here -- only the CPU-side interpolation
// in between runs at single precision. Used by the (file-local, separately
// declared) ExtendedVertexData structs in OGL_Render.cpp and
// RenderRasterize_Shader.cpp and by their glVertexPointer/glTexCoordPointer
// calls, so the storage type and the enum passed to GL can never drift
// apart.
#ifdef __EMSCRIPTEN__
typedef GLfloat A1_VertexScalar;
#define A1_VERTEX_SCALAR_ENUM GL_FLOAT
#else
typedef GLdouble A1_VertexScalar;
#define A1_VERTEX_SCALAR_ENUM GL_DOUBLE
#endif

#endif

#endif
