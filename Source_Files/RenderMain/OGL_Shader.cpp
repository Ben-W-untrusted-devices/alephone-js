/*
 OGL_SHADER.CPP
 
 Copyright (C) 2009 by Clemens Unterkofler and the Aleph One developers
 
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
 
 Implements OpenGL vertex/fragment shader class
 */
#include <algorithm>
#include <iostream>
#include <map>

#include "OGL_Shader.h"
#include "FileHandler.h"
#include "OGL_Setup.h"
#include "InfoTree.h"
#include "Logging.h"

#ifdef HAVE_OPENGL

// gl_clipvertex puts Radeons into software mode on Mac
#if (defined(__APPLE__) && defined(__MACH__))
static bool DisableClipVertex()
{
    const GLubyte* renderer = glGetString(GL_RENDERER);
    return (renderer && strncmp(reinterpret_cast<const char*>(renderer), "AMD", 3) == 0);
}
#else
static bool DisableClipVertex()
{
    return false;
}
#endif


static std::map<std::string, std::string> defaultVertexPrograms;
static std::map<std::string, std::string> defaultFragmentPrograms;
void initDefaultPrograms();

std::vector<Shader> Shader::_shaders;

const char* Shader::_uniform_names[NUMBER_OF_UNIFORM_LOCATIONS] = 
{
	"texture0",
	"texture1",
	"texture2",
	"texture3",
	"time",
	"pulsate",
	"wobble",
	"flare",
	"bloomScale",
	"bloomShift",
	"repeat",
	"offsetx",
	"offsety",
	"pass",
	"fogMix",
	"visibility",
    "transferFadeOut",
	"depth",
	"strictDepthMode",
	"glow",
	"landscapeInverseMatrix",
	"scalex",
	"scaley",
	"yaw",
	"pitch",
	"selfLuminosity",
	"gammaAdjust",
	"logicalWidth",
	"logicalHeight",
	"pixelWidth",
	"pixelHeight",
	"fogMode"
};

const char* Shader::_shader_names[NUMBER_OF_SHADER_TYPES] = 
{
	"error",
    "blur",
	"bloom",
	"landscape",
	"landscape_bloom",
	"landscape_infravision",
	"sprite",
	"sprite_bloom",
	"sprite_infravision",
	"invincible",
	"invincible_bloom",
	"invisible",
	"invisible_bloom",
	"wall",
	"wall_bloom",
	"wall_infravision",
	"bump",
	"bump_bloom",
	"gamma",
	"landscape_sphere",
	"landscape_sphere_bloom",
	"landscape_sphere_infravision"
};


class Shader_MML_Parser {
public:
	static void reset();
	static void parse(const InfoTree& root);
};

void Shader_MML_Parser::reset()
{
	Shader::_shaders.clear();
}

void Shader_MML_Parser::parse(const InfoTree& root)
{
	std::string name;
	if (!root.read_attr("name", name))
		return;
	
	for (int i = 0; i < Shader::NUMBER_OF_SHADER_TYPES; ++i) {
		if (name == Shader::_shader_names[i]) {
			initDefaultPrograms();
			Shader::loadAll();
			
			FileSpecifier vert, frag;
			root.read_path("vert", vert);
			root.read_path("frag", frag);
			int16 passes;
			root.read_attr("passes", passes);
			
			Shader::_shaders[i] = Shader(name, vert, frag, passes);
			break;
		}
	}
}

void reset_mml_opengl_shader()
{
	Shader_MML_Parser::reset();
}

void parse_mml_opengl_shader(const InfoTree& root)
{
	Shader_MML_Parser::parse(root);
}

void parseFile(FileSpecifier& fileSpec, std::string& s) {

	s.clear();

	if (fileSpec == FileSpecifier() || !fileSpec.Exists()) {
		return;
	}

	OpenedFile file;
	if (!fileSpec.Open(file))
	{
		fprintf(stderr, "%s not found\n", fileSpec.GetPath());
		return;
	}

	int32 length;
	file.GetLength(length);

	s.resize(length);
	file.Read(length, &s[0]);
}


#ifdef __EMSCRIPTEN__
// Web port (see ../../WEB_PORT_PLAN.md, M6b): the C++ half of the alpha-test
// emulation described in OGL_Emscripten_Compat.h. Tracks the fixed-function
// state the engine still sets, and pushes it to the bound program at draw
// time.
namespace {
	float a1_alpha_ref = A1_ALPHA_TEST_DISABLED;   // effective, -1 when off
	float a1_alpha_func_ref = A1_ALPHA_TEST_DISABLED;
	bool a1_alpha_enabled = false;
	GLuint a1_bound_program = 0;

	// The shader renderer's three planes, in the order the injected GLSL packs
	// them. 2/3/4 are RenderModelSetup()'s and deliberately unsupported.
	const GLenum a1_clip_planes[3] = { GL_CLIP_PLANE0, GL_CLIP_PLANE1, GL_CLIP_PLANE5 };
	float a1_clip_enabled[3] = { 0.0f, 0.0f, 0.0f };

	// Locations are stable for the life of a linked program, and there are
	// only ~22 of them, so cache rather than calling glGetUniformLocation --
	// a synchronous JS round trip -- on every draw.
	struct A1_ProgramUniforms { GLint alpha_ref; GLint clip_enabled; };
	std::map<GLuint, A1_ProgramUniforms> a1_program_uniforms;

	GLuint a1_pushed_program = 0;
	float a1_pushed_ref = 0.0f;
	float a1_pushed_clip[3] = { 0.0f, 0.0f, 0.0f };
	bool a1_pushed_valid = false;

	void a1_recompute_alpha_ref()
	{
		a1_alpha_ref = a1_alpha_enabled ? a1_alpha_func_ref : A1_ALPHA_TEST_DISABLED;
	}
}

void A1_SetAlphaTestEnabled(bool enabled)
{
	a1_alpha_enabled = enabled;
	a1_recompute_alpha_ref();
}

void A1_SetAlphaTestFunc(GLenum func, GLclampf ref)
{
	// This engine only ever asks for GL_GREATER, which `discard if (a <= ref)`
	// implements exactly. Anything else would need its own comparison in the
	// injected GLSL, so rather than silently approximating it, fall back to
	// not discarding and say so once.
	if (func == GL_GREATER)
	{
		a1_alpha_func_ref = ref;
	}
	else
	{
		if (func != GL_ALWAYS)
		{
			static bool warned = false;
			if (!warned)
			{
				warned = true;
				logWarning("alpha test func 0x%x is not emulated on the web port; not discarding", func);
			}
		}
		a1_alpha_func_ref = A1_ALPHA_TEST_DISABLED;
	}
	a1_recompute_alpha_ref();
}

void A1_NoteProgramBound(GLuint program)
{
	a1_bound_program = program;
}

void A1_SetClipPlaneEnabled(GLenum cap, bool enabled)
{
	for (int i = 0; i < 3; ++i)
	{
		if (a1_clip_planes[i] == cap)
		{
			a1_clip_enabled[i] = enabled ? 1.0f : 0.0f;
			return;
		}
	}
	// GL_CLIP_PLANE2/3/4: only RenderModelSetup() uses these, and 3D models
	// are out of scope for the web port. Say so once rather than clipping
	// wrongly and leaving it to be spotted by eye.
	if (enabled)
	{
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			logWarning("clip plane 0x%x is not emulated on the web port; not clipping", cap);
		}
	}
}

void A1_ResetEmulatedFixedFunctionCache()
{
	a1_program_uniforms.clear();
	a1_pushed_valid = false;
}

void A1_PushEmulatedFixedFunction()
{
	// No program bound means the emulation's own fixed-function shader is
	// drawing, and it applies both of these itself.
	if (!a1_bound_program) return;
	if (a1_pushed_valid && a1_pushed_program == a1_bound_program &&
	    a1_pushed_ref == a1_alpha_ref &&
	    a1_pushed_clip[0] == a1_clip_enabled[0] &&
	    a1_pushed_clip[1] == a1_clip_enabled[1] &&
	    a1_pushed_clip[2] == a1_clip_enabled[2])
	{
		return;
	}

	auto found = a1_program_uniforms.find(a1_bound_program);
	if (found == a1_program_uniforms.end())
	{
		A1_ProgramUniforms locations = {
			glGetUniformLocation(a1_bound_program, A1_ALPHA_TEST_UNIFORM),
			glGetUniformLocation(a1_bound_program, A1_CLIP_ENABLED_UNIFORM)
		};
		found = a1_program_uniforms.emplace(a1_bound_program, locations).first;
	}
	if (found->second.alpha_ref >= 0)
		glUniform1f(found->second.alpha_ref, a1_alpha_ref);
	if (found->second.clip_enabled >= 0)
		glUniform3f(found->second.clip_enabled, a1_clip_enabled[0], a1_clip_enabled[1], a1_clip_enabled[2]);

	a1_pushed_program = a1_bound_program;
	a1_pushed_ref = a1_alpha_ref;
	for (int i = 0; i < 3; ++i) a1_pushed_clip[i] = a1_clip_enabled[i];
	a1_pushed_valid = true;
}

// Web port (see ../../WEB_PORT_PLAN.md, M6b): Emscripten's legacy-GL GLSL
// rewriter (libglemu.js) turns the legacy built-ins these shaders use into
// uniforms and attributes that it then feeds itself. Three of the ones this
// engine needs come out broken, all for the same underlying reason -- the
// rewriter is a list of plain substring regexes over the source text, with no
// lexer, so longer built-in names get eaten by the rules for shorter ones:
//
//   - `gl_ModelViewMatrixInverse` is matched by the `gl_ModelViewMatrix`
//     rule, and becomes the never-declared `u_modelViewInverse`.
//   - `gl_NormalMatrix` is matched by the `gl_Normal` rule, and becomes the
//     never-declared `a_normalMatrix`.
//   - `gl_Fog.start` has no rule at all (there are rules for .color, .end,
//     .scale and .density), so `gl_Fog` survives undeclared.
//
// Between them these failed every shader in the engine, which is why the
// first in-browser run compiled none of them. Rewrite them here, before the
// emulation gets to see them.
static void a1_replace_all(std::string& s, const std::string& from, const std::string& to)
{
	for (std::string::size_type at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
		s.replace(at, from.size(), to);
}

// Both matrices are derived from `gl_ModelViewMatrix`, which the emulation
// does supply correctly, by inverting its 3x3 part in the shader. Deriving
// beats declaring: the emulation has its own `u_normalMatrix`, but only
// uploads it when GL_LIGHTING is enabled, and this engine never enables it --
// so declaring that would compile and then silently hand every shader a zero
// matrix. GLSL ES 1.00 has no `inverse()`, hence the explicit adjugate over
// determinant. This is exact for any invertible modelview, including the
// scaled ones the model renderer sets up, rather than assuming a rigid
// transform.
static const char* const a1_matrix_helpers = R"(
mat3 a1_NormalMatrix() {
	vec3 c0 = cross(gl_ModelViewMatrix[1].xyz, gl_ModelViewMatrix[2].xyz);
	vec3 c1 = cross(gl_ModelViewMatrix[2].xyz, gl_ModelViewMatrix[0].xyz);
	vec3 c2 = cross(gl_ModelViewMatrix[0].xyz, gl_ModelViewMatrix[1].xyz);
	float det = dot(gl_ModelViewMatrix[0].xyz, c0);
	/* transpose(inverse(A)) has the cofactor vectors as its columns */
	return mat3(c0, c1, c2) / det;
}
mat4 a1_ModelViewInverse() {
	vec3 c0 = cross(gl_ModelViewMatrix[1].xyz, gl_ModelViewMatrix[2].xyz);
	vec3 c1 = cross(gl_ModelViewMatrix[2].xyz, gl_ModelViewMatrix[0].xyz);
	vec3 c2 = cross(gl_ModelViewMatrix[0].xyz, gl_ModelViewMatrix[1].xyz);
	float det = dot(gl_ModelViewMatrix[0].xyz, c0);
	/* inverse(A) has them as its rows instead */
	mat3 ai = mat3(c0.x, c1.x, c2.x,
	               c0.y, c1.y, c2.y,
	               c0.z, c1.z, c2.z) / det;
	vec3 t = -(ai * gl_ModelViewMatrix[3].xyz);
	return mat4(vec4(ai[0], 0.0), vec4(ai[1], 0.0), vec4(ai[2], 0.0), vec4(t, 1.0));
}
)";

// Injected into every vertex shader. The emulation transforms each plane into
// eye space when glClipPlane is called, so the test is the same dot product
// its own generated shader uses.
//
// Scaled down only to keep the interpolated value inside a fragment shader's
// mediump range: Marathon world coordinates reach ~32768, past mediump's
// 16384 limit, and the emulation injects `precision mediump float` into
// fragment shaders. Only the sign is ever read, and scaling by a positive
// constant commutes with linear interpolation, so this changes nothing else.
static const char* const a1_clip_helper = R"(
uniform vec4 u_clipPlaneEquation0;
uniform vec4 u_clipPlaneEquation1;
uniform vec4 u_clipPlaneEquation5;
varying vec3 a1_clipDist;
vec3 a1_ClipDistances() {
	vec4 ec = gl_ModelViewMatrix * gl_Vertex;
	return vec3(dot(ec, u_clipPlaneEquation0),
	            dot(ec, u_clipPlaneEquation1),
	            dot(ec, u_clipPlaneEquation5)) / 1024.0;
}
)";
#endif

GLhandleARB parseShader(const GLcharARB* str, GLenum shaderType) {

	GLint status;
	GLhandleARB shader = glCreateShaderObjectARB(shaderType);

	std::vector<const GLcharARB*> source;

#ifdef __EMSCRIPTEN__
	// Web port (see ../../WEB_PORT_PLAN.md, M6b): GLSL ES has no
	// sampler2DRect/texture2DRect. Rectangle textures are mapped to ordinary
	// 2D textures (see OGL_Emscripten_Compat.h) and every coordinate that
	// reaches them is normalized at the point it is generated, so the
	// sampling calls differ in name only.
	source.push_back("#define sampler2DRect sampler2D\n");
	source.push_back("#define texture2DRect texture2D\n");
	// gl_ClipVertex does not exist in GLSL ES either, so this is
	// unconditional here rather than a preference. The switch and the
	// matching #ifndef guards in the .vert files are already part of the
	// engine -- upstream added them for desktop drivers with the same gap.
	const bool disable_clip_vertex = true;
#else
	const bool disable_clip_vertex = DisableClipVertex();
#endif
        if (disable_clip_vertex) {
            source.push_back("#define DISABLE_CLIP_VERTEX\n");
        }
	if (Wanting_sRGB)
	{
		source.push_back("#define GAMMA_CORRECTED_BLENDING\n");
	}
	if (Bloom_sRGB)
	{
		source.push_back("#define BLOOM_SRGB_FRAMEBUFFER\n");
	}
#ifdef __EMSCRIPTEN__
	// Order matters here for exactly the prefix reason that breaks the
	// emulation: the longer names have to be rewritten first, or the shorter
	// rules below would eat them in turn.
	std::string body(str);
	a1_replace_all(body, "gl_ModelViewMatrixInverse", "a1_ModelViewInverse()");
	a1_replace_all(body, "gl_NormalMatrix", "a1_NormalMatrix()");
	// `start` is recoverable exactly from two fields the emulation *does*
	// provide, since it defines u_fogScale as 1/(end - start). Written in the
	// gl_Fog spelling so the emulation's own rules still declare and upload
	// both, which means no new uniform has to be plumbed through from C++.
	a1_replace_all(body, "gl_Fog.start", "(gl_Fog.end - 1.0/gl_Fog.scale)");
	// Only inject the helpers where they are actually used -- they reference
	// gl_ModelViewMatrix, which the emulation only rewrites in vertex shaders,
	// so injecting them unconditionally would break every fragment shader.
	if (body.find("a1_ModelViewInverse()") != std::string::npos ||
	    body.find("a1_NormalMatrix()") != std::string::npos)
	{
		source.push_back(a1_matrix_helpers);
	}

	// Clip planes (see OGL_Emscripten_Compat.h). Hang the computation off the
	// shader's single `gl_Position = ...;` write rather than trying to find
	// the end of main(). wall.vert adjusts gl_Position.z on the next line,
	// which is unaffected -- the clip distances do not depend on it.
	if (shaderType == GL_VERTEX_SHADER_ARB)
	{
		const std::string::size_type pos_at = body.find("gl_Position");
		const std::string::size_type eq = (pos_at == std::string::npos) ? std::string::npos : body.find('=', pos_at);
		const std::string::size_type end = (eq == std::string::npos) ? std::string::npos : body.find(';', eq);
		if (end != std::string::npos)
		{
			body.insert(end + 1, "\n\t" A1_CLIP_DISTANCE_VARYING " = a1_ClipDistances();");
			source.push_back(a1_clip_helper);
		}
	}

	// Alpha test (see OGL_Emscripten_Compat.h): WebGL can only drop a fragment
	// with `discard`, so turn the shader's single `gl_FragColor = <expr>;`
	// into a test followed by the assignment. Every fragment shader in the
	// engine writes gl_FragColor exactly once, as the last statement of
	// main() -- web/test/legacyGlslRewrite.test.ts asserts that invariant,
	// since a second write here would silently go untested.
	//
	// Applied to every fragment shader rather than only the sprite ones,
	// because that is what fixed-function GL does: the alpha test applies to
	// all fragments, whatever is drawing. With the test disabled the uniform
	// is negative and nothing is ever discarded.
	const std::string::size_type write_at = body.find("gl_FragColor");
	if (write_at != std::string::npos)
	{
		const std::string::size_type eq = body.find('=', write_at);
		const std::string::size_type end = (eq == std::string::npos) ? std::string::npos : body.find(';', eq);
		if (end != std::string::npos)
		{
			const std::string expr = body.substr(eq + 1, end - eq - 1);
			// A disabled plane multiplies to exactly 0.0, which is not < 0.0,
			// so the mask alone expresses "off" -- the emulation uploads
			// equations for disabled planes too, and they must be ignored.
			body.replace(write_at, end + 1 - write_at,
			             "{ if (any(lessThan(" A1_CLIP_DISTANCE_VARYING " * " A1_CLIP_ENABLED_UNIFORM
			             ", vec3(0.0)))) discard;"
			             " vec4 a1_c =" + expr + ";"
			             " if (a1_c.a <= " A1_ALPHA_TEST_UNIFORM ") discard;"
			             " gl_FragColor = a1_c; }");
			source.push_back("uniform float " A1_ALPHA_TEST_UNIFORM ";\n"
			                 "uniform vec3 " A1_CLIP_ENABLED_UNIFORM ";\n"
			                 "varying vec3 " A1_CLIP_DISTANCE_VARYING ";\n");
		}
	}

	source.push_back(body.c_str());
#else
	source.push_back(str);
#endif

	glShaderSourceARB(shader, source.size(), &source[0], NULL);

	glCompileShaderARB(shader);
	glGetObjectParameterivARB(shader, GL_OBJECT_COMPILE_STATUS_ARB, &status);

	if(status) {
		return shader;
	} else {
        GLint infoLen = 0;
        glGetShaderiv((GLuint)(size_t)shader, GL_INFO_LOG_LENGTH, &infoLen);
        
        if(infoLen > 1)
        {
            char* infoLog = (char*) malloc(sizeof(char) * infoLen);
            glGetShaderInfoLog((GLuint)(size_t)shader, infoLen, NULL, infoLog);
            logError("Error compiling shader:\n%s\n", infoLog);
            free(infoLog);
        }
        
		glDeleteObjectARB(shader);
		return 0;
	}
}

void Shader::loadAll() {
	initDefaultPrograms();
	if (!_shaders.size()) 
	{
		_shaders.reserve(NUMBER_OF_SHADER_TYPES);
		for (int i = 0; i < NUMBER_OF_SHADER_TYPES; ++i) 
		{
			_shaders.push_back(Shader(_shader_names[i]));
		}
	}
}

void Shader::unloadAll() {
	for (int i = 0; i < _shaders.size(); ++i) 
	{
		_shaders[i].unload();
	}
#ifdef __EMSCRIPTEN__
	// Web port: the emulated fixed-function uniform locations are keyed by
	// program name, and GL is free to hand the same name back out after these
	// are deleted.
	A1_ResetEmulatedFixedFunctionCache();
#endif
}

Shader::Shader(const std::string& name) : _programObj(0), _passes(-1), _loaded(false) {
    initDefaultPrograms();
    if (defaultVertexPrograms.count(name) > 0) {
	    _vert = defaultVertexPrograms[name];
    }
    if (defaultFragmentPrograms.count(name) > 0) {
	    _frag = defaultFragmentPrograms[name];
    }
}    

Shader::Shader(const std::string& name, FileSpecifier& vert, FileSpecifier& frag, int16& passes) : _programObj(0), _passes(passes), _loaded(false) {
	initDefaultPrograms();
	
	parseFile(vert,  _vert);
	if (_vert.empty() && defaultVertexPrograms.count(name) > 0) 
	{
		_vert = defaultVertexPrograms[name];
	}
	
	parseFile(frag, _frag);
	if (_frag.empty() && defaultFragmentPrograms.count(name) > 0) 
	{
		_frag = defaultFragmentPrograms[name];
	}
}

void Shader::init() {

	std::fill_n(_uniform_locations, static_cast<int>(NUMBER_OF_UNIFORM_LOCATIONS), -1);
	std::fill_n(_cached_floats, static_cast<int>(NUMBER_OF_UNIFORM_LOCATIONS), 0.0);

	_loaded = true;

	_programObj = glCreateProgramObjectARB();

	assert(!_vert.empty());
	GLhandleARB vertexShader = parseShader(_vert.c_str(), GL_VERTEX_SHADER_ARB);
    if(!vertexShader) {
        _vert = defaultVertexPrograms["error"];
        vertexShader = parseShader(_vert.c_str(), GL_VERTEX_SHADER_ARB);
    }
	
	glAttachObjectARB(_programObj, vertexShader);
	glDeleteObjectARB(vertexShader);

	assert(!_frag.empty());
	GLhandleARB fragmentShader = parseShader(_frag.c_str(), GL_FRAGMENT_SHADER_ARB);
	if(!fragmentShader) {
        _frag = defaultFragmentPrograms["error"];
        fragmentShader = parseShader(_frag.c_str(), GL_FRAGMENT_SHADER_ARB);
    }
    
	glAttachObjectARB(_programObj, fragmentShader);
	glDeleteObjectARB(fragmentShader);
	
	glLinkProgramARB(_programObj);
    
    GLint linked;
    glGetProgramiv((GLuint)(size_t)_programObj, GL_LINK_STATUS, &linked);
    if(!linked)
    {
      GLint infoLen = 0;
      glGetProgramiv((GLuint)(size_t)_programObj, GL_INFO_LOG_LENGTH, &infoLen);
      if(infoLen > 1)
      {
        char* infoLog = (char*) malloc(sizeof(char) * infoLen);
        glGetProgramInfoLog((GLuint)(size_t)_programObj, infoLen, NULL, infoLog);
        logError("Error linking program:\n%s\n", infoLog);
        free(infoLog);
      }
      glDeleteProgram((GLuint)(size_t)_programObj);
    }

	assert(_programObj);

	glUseProgramObjectARB(_programObj);

	glUniform1iARB(getUniformLocation(U_Texture0), 0);
	glUniform1iARB(getUniformLocation(U_Texture1), 1);
	glUniform1iARB(getUniformLocation(U_Texture2), 2);
	glUniform1iARB(getUniformLocation(U_Texture3), 3);	

	glUseProgramObjectARB(0);

//	assert(glGetError() == GL_NO_ERROR);
}

void Shader::setFloat(UniformName name, float f) {

	if (_cached_floats[name] != f) {
		_cached_floats[name] = f;
		glUniform1fARB(getUniformLocation(name), f);
	}
}

void Shader::setMatrix4(UniformName name, float *f) {

	glUniformMatrix4fvARB(getUniformLocation(name), 1, false, f);
}

Shader::~Shader() {
	unload();
}

void Shader::enable() {
	if(!_loaded) { init(); }
	glUseProgramObjectARB(_programObj);
}

void Shader::disable() {
	glUseProgramObjectARB(0);
}

void Shader::unload() {
	if(_programObj) {
		glDeleteObjectARB(_programObj);
		_programObj = 0;
		_loaded = false;
	}
}

int16 Shader::passes() {
	return _passes;
}

void initDefaultPrograms() {
    if (defaultVertexPrograms.size() > 0)
        return;
    
    
    defaultVertexPrograms["error"] = ""
    "varying vec4 vertexColor;\n"
    "void main(void) {\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "    vertexColor = vec4(1.0, 1.0, 0.0, 1.0);\n"
    "}\n";
    defaultFragmentPrograms["error"] = ""
    "float round(float n){ \n"
    "   float nSign = 1.0; \n"
    "   if ( n < 0.0 ) { nSign = -1.0; }; \n"
    "   return nSign * floor(abs(n)+0.5); \n"
    "} \n"
    "void main (void) {\n"
    "    gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0);\n"
    "    float checkerSize = 8.0;\n"
    "    float phase = 0.0;\n"
    "    if( mod(round(gl_FragCoord.y / checkerSize), 2.0) == 0.0) {\n"
    "       phase = checkerSize;\n"
    "    }\n"
    "    if (mod(round((gl_FragCoord.x + phase) / checkerSize), 2.0)==0.0) {\n"
    "       gl_FragColor.a = 0.5;\n"
    "    }\n"
    "}\n";
    
	defaultVertexPrograms["gamma"] = ""
	"varying vec4 vertexColor;\n"
	"void main(void) {\n"
	"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
	"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
	"	vertexColor = gl_Color;\n"
	"}\n";
	defaultFragmentPrograms["gamma"] = ""
	"uniform sampler2DRect texture0;\n"
	"uniform float gammaAdjust;\n"
	"void main (void) {\n"
	"	vec4 color0 = texture2DRect(texture0, gl_TexCoord[0].xy);\n"
	"	gl_FragColor = vec4(pow(color0.r, gammaAdjust), pow(color0.g, gammaAdjust), pow(color0.b, gammaAdjust), 1.0);\n"
	"}\n";
	
    defaultVertexPrograms["blur"] = ""
        "varying vec4 vertexColor;\n"
        "void main(void) {\n"
        "	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
        "	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "	vertexColor = gl_Color;\n"
        "}\n";
    defaultFragmentPrograms["blur"] = ""
        "uniform sampler2DRect texture0;\n"
        "uniform float offsetx;\n"
        "uniform float offsety;\n"
        "uniform float pass;\n"
        "varying vec4 vertexColor;\n"
        "const float f0 = 0.14012035;\n"
        "const float f1 = 0.24122258;\n"
        "const float o1 = 1.45387071;\n"
        "const float f2 = 0.13265595;\n"
        "const float o2 = 3.39370426;\n"
        "const float f3 = 0.04518872;\n"
        "const float o3 = 5.33659787;\n"
        "#ifdef BLOOM_SRGB_FRAMEBUFFER\n"
        "vec3 s2l(vec3 srgb) { return srgb; }\n"
        "vec3 l2s(vec3 linear) { return linear; }\n"
        "#else\n"
        "vec3 s2l(vec3 srgb) { return srgb * srgb; }\n"
        "vec3 l2s(vec3 linear) { return sqrt(linear); }\n"
        "#endif\n"
        "void main (void) {\n"
        "	vec2 s = vec2(offsetx, offsety);\n"
        "	// Thanks to Renaud Bedard - http://theinstructionlimit.com/?p=43\n"
        "	vec3 c = s2l(texture2DRect(texture0, gl_TexCoord[0].xy).rgb);\n"
        "	vec3 t = f0 * c;\n"
        "	t += f1 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy - o1*s).rgb);\n"
        "	t += f1 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy + o1*s).rgb);\n"
        "	t += f2 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy - o2*s).rgb);\n"
        "	t += f2 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy + o2*s).rgb);\n"
        "	t += f3 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy - o3*s).rgb);\n"
        "	t += f3 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy + o3*s).rgb);\n"
        "	gl_FragColor = vec4(l2s(t), 1.0) * vertexColor;\n"
        "}\n";    
    
    defaultVertexPrograms["bloom"] = ""
        "varying vec4 vertexColor;\n"
        "void main(void) {\n"
        "	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
        "	gl_TexCoord[1] = gl_MultiTexCoord1;\n"
        "	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "	vertexColor = gl_Color;\n"
        "}\n";
    defaultFragmentPrograms["bloom"] = ""
        "uniform sampler2DRect texture0;\n"
        "uniform sampler2DRect texture1;\n"
        "uniform float pass;\n"
        "varying vec4 vertexColor;\n"
        "vec3 s2l(vec3 srgb) { return srgb * srgb; }\n"
        "vec3 l2s(vec3 linear) { return sqrt(linear); }\n"
		"#ifndef BLOOM_SRGB_FRAMEBUFFER\n"
	    "vec3 b2l(vec3 bloom) { return bloom * bloom; }\n"
		"#else\n"
		"vec3 b2l(vec3 bloom) { return bloom; }\n"
        "#endif\n"
        "void main (void) {\n"
        "	vec4 color0 = texture2DRect(texture0, gl_TexCoord[0].xy);\n"
        "	vec4 color1 = texture2DRect(texture1, gl_TexCoord[1].xy);\n"
        "	vec3 color = l2s(s2l(color0.rgb) + b2l(color1.rgb));\n"
        "	gl_FragColor = vec4(color, 1.0);\n"
        "}\n";

	defaultVertexPrograms["landscape"] =
        #include "Shaders/landscape.vert"
		;
	defaultFragmentPrograms["landscape"] =
		#include "Shaders/landscape.frag"
		;
	
    defaultVertexPrograms["landscape_bloom"] = defaultVertexPrograms["landscape"];
	defaultFragmentPrograms["landscape_bloom"] =
		#include "Shaders/landscape_bloom.frag"
		;
	
	defaultVertexPrograms["landscape_infravision"] = defaultVertexPrograms["landscape"];
	defaultFragmentPrograms["landscape_infravision"] =
        #include "Shaders/landscape_infravision.frag"
		;

	defaultVertexPrograms["sprite"] =
        #include "Shaders/sprite.vert"
		;
	defaultFragmentPrograms["sprite"] =
        #include "Shaders/sprite.frag"
		;
	
    defaultVertexPrograms["sprite_bloom"] = defaultVertexPrograms["sprite"];
	defaultFragmentPrograms["sprite_bloom"] =
		#include "Shaders/sprite_bloom.frag"
		;

	defaultVertexPrograms["sprite_infravision"] = defaultVertexPrograms["sprite"];
	defaultFragmentPrograms["sprite_infravision"] =
        #include "Shaders/sprite_infravision.frag"
		;
	
    defaultVertexPrograms["invincible"] = defaultVertexPrograms["sprite"];
	defaultFragmentPrograms["invincible"] =
		#include "Shaders/invincible.frag"
		;
	
    defaultVertexPrograms["invincible_bloom"] = defaultVertexPrograms["invincible"];
	defaultFragmentPrograms["invincible_bloom"] =
        #include "Shaders/invincible_bloom.frag"
		;

    defaultVertexPrograms["invisible"] = defaultVertexPrograms["sprite"];
	defaultFragmentPrograms["invisible"] =
        #include "Shaders/invisible.frag"
		;
    defaultVertexPrograms["invisible_bloom"] = defaultVertexPrograms["invisible"];
	defaultFragmentPrograms["invisible_bloom"] =
        #include "Shaders/invisible_bloom.frag"
		;

	defaultVertexPrograms["wall"] =
        #include "Shaders/wall.vert"
		;
	defaultFragmentPrograms["wall"] =
        #include "Shaders/wall.frag"
		;
	
    defaultVertexPrograms["wall_bloom"] = defaultVertexPrograms["wall"];
	defaultFragmentPrograms["wall_bloom"] =
		#include "Shaders/wall_bloom.frag"
		;
	
	defaultVertexPrograms["wall_infravision"] = defaultVertexPrograms["wall"];
	defaultFragmentPrograms["wall_infravision"] =
        #include "Shaders/wall_infravision.frag"
		;
    
    defaultVertexPrograms["bump"] = defaultVertexPrograms["wall"];
	defaultFragmentPrograms["bump"] =
        #include "Shaders/bump.frag"
		;
	
    defaultVertexPrograms["bump_bloom"] = defaultVertexPrograms["bump"];
    defaultFragmentPrograms["bump_bloom"] =
        #include "Shaders/bump_bloom.frag"
		;

	defaultVertexPrograms["landscape_sphere"] =
		#include "Shaders/landscape_sphere.vert"
	;

	defaultFragmentPrograms["landscape_sphere"] =
		#include "Shaders/landscape_sphere.frag"
	;

	defaultVertexPrograms["landscape_sphere_bloom"] = defaultVertexPrograms["landscape_sphere"];
	defaultFragmentPrograms["landscape_sphere_bloom"] =
		#include "Shaders/landscape_sphere_bloom.frag"
	;

	defaultVertexPrograms["landscape_sphere_infravision"] = defaultVertexPrograms["landscape_sphere"];
	defaultFragmentPrograms["landscape_sphere_infravision"] =
		#include "Shaders/landscape_sphere_infravision.frag"
	;
}

#endif
