/**
 * Guards the GLSL rewriting the web port depends on (see WEB_PORT_PLAN.md, M6b).
 *
 * Emscripten's legacy-GL emulation (libglemu.js) rewrites the legacy GLSL
 * built-ins our shaders use into uniforms and attributes it feeds itself. Its
 * rewriter is a list of plain substring regexes with no lexer, so longer
 * built-in names get eaten by the rules for shorter ones -- and it has no rule
 * at all for `gl_Fog.start`. That silently broke 14 of the 22 shaders on the
 * first in-browser run: they failed to compile, the renderer fell back to its
 * "error" shader, and nothing drew.
 *
 * `parseShader()` in Source_Files/RenderMain/OGL_Shader.cpp rewrites the three
 * affected built-ins before the emulation can see them. This test reproduces
 * both passes over the real shader sources and asserts nothing legacy survives.
 * It exists because the failure mode is invisible until a browser run: a new
 * shader using, say, `gl_Fog.start` would compile fine natively and break only
 * on the web.
 *
 * REWRITES and GLEMU_* below mirror the C++ and libglemu.js respectively; if
 * either changes, this needs updating with it.
 */
import { describe, expect, it } from "vitest";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

const SHADER_DIR = join(__dirname, "..", "..", "Source_Files", "RenderMain", "Shaders");

/** What parseShader() does, in its order -- longest name first. */
const REWRITES: ReadonlyArray<readonly [string, string]> = [
  ["gl_ModelViewMatrixInverse", "a1_ModelViewInverse()"],
  ["gl_NormalMatrix", "a1_NormalMatrix()"],
  ["gl_Fog.start", "(gl_Fog.end - 1.0/gl_Fog.scale)"],
];

function applyEnginePass(source: string): string {
  return REWRITES.reduce((acc, [from, to]) => acc.split(from).join(to), source);
}

/** The subset of libglemu.js's rewriter that matters for these shaders. */
function applyEmulationPass(source: string, isVertex: boolean): string {
  let s = source;
  if (isVertex) {
    s = s.split("ftransform()").join("(u_projection * u_modelView * a_position)");
    s = s.split("gl_ProjectionMatrix").join("u_projection");
    // NOTE: this rule is why gl_ModelViewMatrixInverse has to be gone already.
    s = s.split("gl_ModelViewMatrix").join("u_modelView");
    s = s.split("gl_Vertex").join("a_position");
    s = s.split("gl_ModelViewProjectionMatrix").join("(u_projection * u_modelView)");
    for (let i = 0; i < 8; i++) {
      s = s.split(`gl_TexCoord[${i}]`).join(`v_texCoord${i}`);
      s = s.split(`gl_MultiTexCoord${i}`).join(`a_texCoord${i}`);
      s = s.split(`gl_TextureMatrix[${i}]`).join(`u_textureMatrix${i}`);
    }
    s = s.split("gl_FrontColor").join("v_color");
    s = s.split("gl_Color").join("a_color");
    // ...and this rule is why gl_NormalMatrix has to be gone already.
    s = s.split("gl_Normal").join("a_normal");
  } else {
    for (let i = 0; i < 8; i++) s = s.split(`gl_TexCoord[${i}]`).join(`v_texCoord${i}`);
    s = s.split("gl_Color").join("v_color");
    // NOTE: there is deliberately no `gl_Fog.start` rule here -- that gap is
    // the third bug this guards against.
    s = s.split("gl_Fog.color").join("u_fogColor");
    s = s.split("gl_Fog.end").join("u_fogEnd");
    s = s.split("gl_Fog.scale").join("u_fogScale");
    s = s.split("gl_Fog.density").join("u_fogDensity");
  }
  return s.split("gl_FogFragCoord").join("v_fogFragCoord");
}

/** Built-ins that really do exist in GLSL ES 1.00, plus one we #ifdef out. */
const SURVIVES_LEGITIMATELY = new Set([
  "gl_Position",
  "gl_FragColor",
  "gl_PointSize",
  "gl_FragCoord",
  "gl_FrontFacing",
  "gl_PointCoord",
  // Guarded by #ifndef DISABLE_CLIP_VERTEX, which the web port always defines.
  "gl_ClipVertex",
]);

function unresolvedIdentifiers(compiled: string): string[] {
  const legacy = (compiled.match(/\bgl_[A-Za-z0-9_]*/g) ?? []).filter(
    (name) => !SURVIVES_LEGITIMATELY.has(name),
  );
  // Names the emulation produces by mangling a longer built-in, and then never
  // declares -- the exact identifiers the first browser run reported.
  const mangled = compiled.match(/\b[ua]_(?:modelViewInverse|normalMatrix)\b/g) ?? [];
  return [...new Set([...legacy, ...mangled])].sort();
}

const shaders = readdirSync(SHADER_DIR)
  .filter((name) => name.endsWith(".vert") || name.endsWith(".frag"))
  .map((name) => ({
    name,
    isVertex: name.endsWith(".vert"),
    source: readFileSync(join(SHADER_DIR, name), "utf8"),
  }));

describe("legacy GLSL rewriting for the WebGL 1.0 build", () => {
  it("finds the shaders", () => {
    expect(shaders.length).toBeGreaterThan(0);
  });

  it.each(shaders)("$name compiles clean through the emulation", ({ source, isVertex }) => {
    const compiled = applyEmulationPass(applyEnginePass(source), isVertex);
    expect(unresolvedIdentifiers(compiled)).toEqual([]);
  });

  // WebGL has no alpha test, so parseShader() rewrites each fragment shader's
  // single `gl_FragColor = <expr>;` into a discard followed by the assignment.
  // That rewrite assumes there is exactly one such write; a second one would
  // go silently untested, so pin the assumption here rather than in a comment.
  const fragmentShaders = shaders.filter((s) => !s.isVertex);

  it.each(fragmentShaders)("$name writes gl_FragColor exactly once", ({ source }) => {
    expect(source.match(/gl_FragColor/g) ?? []).toHaveLength(1);
  });

  it.each(fragmentShaders)("$name survives the alpha-test rewrite", ({ source }) => {
    const at = source.indexOf("gl_FragColor");
    const eq = source.indexOf("=", at);
    const end = source.indexOf(";", eq);
    expect(end).toBeGreaterThan(eq);

    const expr = source.slice(eq + 1, end);
    const rewritten =
      source.slice(0, at) +
      `{ vec4 a1_c =${expr}; if (a1_c.a <= a1_alphaTestRef) discard; gl_FragColor = a1_c; }` +
      source.slice(end + 1);

    // The expression must be self-contained, or hoisting it into a temp would
    // not compile.
    expect(expr).not.toContain(";");
    expect(rewritten).toContain("discard");
    expect(rewritten.match(/gl_FragColor/g) ?? []).toHaveLength(1);
    // Braces still balanced -- the rewrite adds a block, so a stray brace in
    // the captured expression would corrupt main().
    expect(rewritten.split("{").length).toBe(rewritten.split("}").length);
  });

  it("still detects the gaps if the engine's rewrite is skipped", () => {
    // Without applyEnginePass this must fail loudly -- otherwise the test above
    // proves nothing and would keep passing if parseShader() lost its rewrites.
    const broken = shaders.filter(
      ({ source, isVertex }) => unresolvedIdentifiers(applyEmulationPass(source, isVertex)).length > 0,
    );
    expect(broken.length).toBeGreaterThan(0);
  });
});
