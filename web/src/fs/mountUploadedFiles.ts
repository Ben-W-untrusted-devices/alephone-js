import { UploadableFile } from "../upload/collectFiles";
import { EmscriptenFS } from "./EmscriptenFS";

export interface MountResult {
  /** Files actually written (excludes skipped entries -- see below). */
  readonly mountedCount: number;
  /** Entries skipped because their relative path was empty or unsafe. */
  readonly skippedCount: number;
  readonly mountRoot: string;
}

/**
 * Reads a File's bytes via FileReader rather than the newer Blob.arrayBuffer()
 * -- broader compatibility (notably jsdom, used by this project's unit
 * tests, doesn't implement Blob.arrayBuffer() as of the version in use here,
 * even though real browsers do).
 */
function readFileBytes(file: File): Promise<Uint8Array> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(new Uint8Array(reader.result as ArrayBuffer));
    reader.onerror = () => reject(reader.error ?? new Error(`Failed to read ${file.name}`));
    reader.readAsArrayBuffer(file);
  });
}

function sanitizedSegments(relativePath: string): string[] | null {
  const segments = relativePath
    .split("/")
    .map((s) => s.trim())
    .filter((s) => s.length > 0 && s !== ".");
  if (segments.length === 0) return null;
  // ".." would let a maliciously-crafted drag-drop entry (or a browser bug
  // in webkitRelativePath) write outside mountRoot -- shouldn't happen in
  // practice, but cheap to guard rather than trust.
  if (segments.some((s) => s === "..")) return null;
  return segments;
}

/**
 * If every file's path starts with the same top-level folder (the normal
 * case for a dropped/picked folder, e.g. "Marathon 2/Map.sceA"), strips it.
 * The engine's data_search_path only ever gets one directory to look in
 * (see WEB_PORT_PLAN.md, M4); scenario data lives directly under that
 * directory on a native install (Map, Plugins/, Scripts/, ...), not nested
 * one level deeper under the folder's own name, so without this, nothing
 * -- not even the recognized-extension rename below -- would end up where
 * the engine looks.
 */
function stripCommonLeadingFolder(allSegments: readonly string[][]): string[][] {
  if (allSegments.length === 0) return [];
  const first = allSegments[0];
  if (first.length < 2) return allSegments.map((s) => s.slice());
  const leader = first[0];
  const allShareLeader = allSegments.every((s) => s.length >= 2 && s[0] === leader);
  return allShareLeader ? allSegments.map((s) => s.slice(1)) : allSegments.map((s) => s.slice());
}

/**
 * The engine looks up its core scenario files by exact, extensionless name
 * (e.g. "Map", not "Map.sceA" -- see Source_Files/Misc/DefaultStringSets.cpp
 * and Source_Files/Files/preprocess_map_sdl.cpp's have_default_files(),
 * confirmed by running the compiled engine against mounted test files: it
 * kept reporting the data as missing until renamed). The extensions
 * (.sceA/.shpA/...) exist for this widget's own "looks like a scenario"
 * recognition (see knownFileTypes.ts) and, separately, for the file-browser
 * chooser UI -- not for how the engine finds its default files. Only the
 * four with a single fixed expected name are covered; saved games, films,
 * and physics models aren't looked up this way and keep their real names.
 */
const CANONICAL_TOP_LEVEL_NAMES: Readonly<Record<string, string>> = {
  scea: "Map",
  shpa: "Shapes",
  snda: "Sounds",
  imga: "Images",
};

function canonicalizeTopLevelName(name: string): string {
  const dot = name.lastIndexOf(".");
  if (dot === -1) return name;
  const ext = name.slice(dot + 1).toLowerCase();
  return CANONICAL_TOP_LEVEL_NAMES[ext] ?? name;
}

/**
 * Writes the bytes of every UploadableFile (from web/src/upload) into an
 * Emscripten FS-like filesystem, under mountRoot, preserving the relative
 * directory structure the user selected/dropped. Intended to run before the
 * compiled engine's main() is invoked (see WEB_PORT_PLAN.md, M4): the engine
 * expects a real directory of scenario data files, and browsers have no
 * filesystem to point it at, so this is the bridge from the upload widget
 * (M1) to that expectation.
 */
export async function mountUploadedFiles(
  fs: EmscriptenFS,
  files: readonly UploadableFile[],
  mountRoot = "/data"
): Promise<MountResult> {
  const root = mountRoot.replace(/\/+$/, "") || "/";
  fs.mkdirTree(root);
  const createdDirs = new Set<string>([root]);

  let skippedCount = 0;
  const sanitized: { entry: UploadableFile; segments: string[] }[] = [];
  for (const entry of files) {
    const segments = sanitizedSegments(entry.relativePath);
    if (!segments) {
      skippedCount++;
      continue;
    }
    sanitized.push({ entry, segments });
  }

  const stripped = stripCommonLeadingFolder(sanitized.map((s) => s.segments));

  let mountedCount = 0;
  for (let i = 0; i < sanitized.length; i++) {
    const segments = stripped[i];
    if (segments.length === 1) {
      segments[0] = canonicalizeTopLevelName(segments[0]);
    }

    if (segments.length > 1) {
      const dirPath = `${root}/${segments.slice(0, -1).join("/")}`;
      if (!createdDirs.has(dirPath)) {
        fs.mkdirTree(dirPath);
        createdDirs.add(dirPath);
      }
    }

    const bytes = await readFileBytes(sanitized[i].entry.file);
    fs.writeFile(`${root}/${segments.join("/")}`, bytes);
    mountedCount++;
  }

  return { mountedCount, skippedCount, mountRoot: root };
}
