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
 * directory on a native install (Map.sceA, Plugins/, Scripts/, ...), not
 * nested one level deeper under the folder's own name, so without this,
 * nothing would end up where the engine looks.
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
 * Writes the bytes of every UploadableFile (from web/src/upload) into an
 * Emscripten FS-like filesystem, under mountRoot, preserving the relative
 * directory structure and original filenames the user selected/dropped
 * (only a single shared leading wrapper folder is stripped -- see
 * stripCommonLeadingFolder). Intended to run before the compiled engine's
 * main() is invoked (see WEB_PORT_PLAN.md, M4): the engine expects a real
 * directory of scenario data files, and browsers have no filesystem to
 * point it at, so this is the bridge from the upload widget (M1) to that
 * expectation.
 *
 * Deliberately does *not* rename files to any "canonical" name (an earlier
 * version of this function renamed e.g. Map.sceA -> Map, reasoning that the
 * engine looks up its default map/shapes/sounds/images by exact,
 * extensionless name -- see Source_Files/Misc/DefaultStringSets.cpp). That
 * was wrong: real, properly-packaged scenarios (confirmed with the actual
 * Marathon 2 data, tested locally via Node -- see WEB_PORT_PLAN.md, M4g)
 * ship their own Scripts/Filenames.mml overriding those defaults to their
 * real on-disk names (e.g. "Map.sceA", even a subdirectory path for the
 * physics model), loaded automatically before those lookups ever run --
 * exactly how a native install already works. Renaming files here just
 * broke that mechanism for exactly the scenarios it matters most for.
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
