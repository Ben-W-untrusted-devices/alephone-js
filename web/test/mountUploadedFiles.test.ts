import { describe, expect, it } from "vitest";
import { collectFromFileList } from "../src/upload/collectFiles";
import { EmscriptenFS } from "../src/fs/EmscriptenFS";
import { mountUploadedFiles } from "../src/fs/mountUploadedFiles";

function withRelativePath(file: File, relativePath: string): File {
  Object.defineProperty(file, "webkitRelativePath", {
    value: relativePath,
    configurable: true,
  });
  return file;
}

/** Minimal in-memory fake of the Emscripten FS API, for assertions. */
class FakeFS implements EmscriptenFS {
  readonly dirsCreated: string[] = [];
  readonly files = new Map<string, Uint8Array>();

  mkdirTree(path: string): void {
    this.dirsCreated.push(path);
  }

  writeFile(path: string, data: Uint8Array): void {
    this.files.set(path, data);
  }
}

describe("mountUploadedFiles", () => {
  it("writes flat files directly under mountRoot, canonicalizing recognized top-level names", async () => {
    const files = collectFromFileList([
      new File(["abc"], "Map.sceA"),
      new File(["defg"], "Shapes.shpA"),
    ]);
    const fs = new FakeFS();

    const result = await mountUploadedFiles(fs, files, "/data");

    expect(result).toEqual({ mountedCount: 2, skippedCount: 0, mountRoot: "/data" });
    expect([...fs.files.keys()].sort()).toEqual(["/data/Map", "/data/Shapes"]);
    expect(fs.files.get("/data/Map")).toEqual(new Uint8Array([97, 98, 99]));
  });

  it("strips the common leading folder and recreates the rest of the structure", async () => {
    const files = collectFromFileList([
      withRelativePath(new File(["a"], "Map.sceA"), "Marathon 2/Map.sceA"),
      withRelativePath(
        new File(["b"], "Enhancements.mml"),
        "Marathon 2/Plugins/Enhancements/Enhancements.mml"
      ),
    ]);
    const fs = new FakeFS();

    const result = await mountUploadedFiles(fs, files, "/data");

    expect(result.mountedCount).toBe(2);
    expect(fs.dirsCreated).toContain("/data/Plugins/Enhancements");
    // The top-level Map.sceA (directly under the dropped folder) is renamed
    // to the engine's expected canonical name; the nested plugin file, not
    // being a top-level scenario file, keeps its real name -- see
    // WEB_PORT_PLAN.md, M4.
    expect([...fs.files.keys()].sort()).toEqual([
      "/data/Map",
      "/data/Plugins/Enhancements/Enhancements.mml",
    ]);
  });

  it("does not recreate a directory it has already made", async () => {
    const files = collectFromFileList([
      withRelativePath(new File(["a"], "X.mml"), "Marathon 2/Plugins/X.mml"),
      withRelativePath(new File(["b"], "Y.mml"), "Marathon 2/Plugins/Y.mml"),
    ]);
    const fs = new FakeFS();

    await mountUploadedFiles(fs, files, "/data");

    expect(fs.dirsCreated.filter((d) => d === "/data/Plugins")).toHaveLength(1);
  });

  it("does not strip a leading folder when files don't share one", async () => {
    const files = collectFromFileList([
      withRelativePath(new File(["a"], "Map.sceA"), "Marathon 2/Map.sceA"),
      withRelativePath(new File(["b"], "Other.sceA"), "Different Folder/Other.sceA"),
    ]);
    const fs = new FakeFS();

    await mountUploadedFiles(fs, files, "/data");

    expect([...fs.files.keys()].sort()).toEqual([
      "/data/Different Folder/Other.sceA",
      "/data/Marathon 2/Map.sceA",
    ]);
  });

  it("only canonicalizes recognized top-level scenario files, not nested ones with the same extension", async () => {
    const files = collectFromFileList([
      withRelativePath(new File(["a"], "Map.sceA"), "Marathon 2/Map.sceA"),
      withRelativePath(
        new File(["b"], "SubMap.sceA"),
        "Marathon 2/Plugins/MyMod/SubMap.sceA"
      ),
    ]);
    const fs = new FakeFS();

    await mountUploadedFiles(fs, files, "/data");

    expect([...fs.files.keys()].sort()).toEqual([
      "/data/Map",
      "/data/Plugins/MyMod/SubMap.sceA",
    ]);
  });

  it("strips trailing slashes from mountRoot", async () => {
    const files = collectFromFileList([new File(["a"], "Map.sceA")]);
    const fs = new FakeFS();

    const result = await mountUploadedFiles(fs, files, "/data/");

    expect(result.mountRoot).toBe("/data");
    expect(fs.files.has("/data/Map")).toBe(true);
  });

  it("skips entries with an unsafe or empty relative path", async () => {
    const traversal = withRelativePath(new File(["x"], "evil"), "../../etc/passwd");
    const fs = new FakeFS();

    // Bypass collectFromFileList for the empty case: it always falls back to
    // file.name when webkitRelativePath is empty, so it can never itself
    // produce an UploadableFile with relativePath === "" -- but
    // mountUploadedFiles takes the interface directly and should still
    // defend against a future/other producer doing so.
    const result = await mountUploadedFiles(fs, [
      ...collectFromFileList([traversal]),
      { relativePath: "", size: 1, file: new File(["y"], "y") },
    ]);

    expect(result.mountedCount).toBe(0);
    expect(result.skippedCount).toBe(2);
    expect(fs.files.size).toBe(0);
  });

  it("defaults to mounting under /data", async () => {
    const files = collectFromFileList([new File(["a"], "Map.sceA")]);
    const fs = new FakeFS();

    const result = await mountUploadedFiles(fs, files);

    expect(result.mountRoot).toBe("/data");
  });
});
