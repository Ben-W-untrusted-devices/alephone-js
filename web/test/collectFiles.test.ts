import { describe, expect, it } from "vitest";
import {
  collectFromDataTransferItems,
  collectFromFileList,
  summarize,
} from "../src/upload/collectFiles";

function withRelativePath(file: File, relativePath: string): File {
  Object.defineProperty(file, "webkitRelativePath", {
    value: relativePath,
    configurable: true,
  });
  return file;
}

describe("collectFromFileList", () => {
  it("uses webkitRelativePath when present", () => {
    const file = withRelativePath(
      new File(["abc"], "Map.sceA"),
      "Marathon 2/Map.sceA"
    );
    const [entry] = collectFromFileList([file]);
    expect(entry.relativePath).toBe("Marathon 2/Map.sceA");
    expect(entry.recognizedType?.label).toBe("Scenario/Map");
    expect(entry.size).toBe(3);
  });

  it("falls back to the plain file name when there is no relative path", () => {
    const file = new File(["abc"], "Map.sceA");
    const [entry] = collectFromFileList([file]);
    expect(entry.relativePath).toBe("Map.sceA");
  });

  it("carries the original File through for later byte access", () => {
    const file = new File(["abc"], "notes.txt");
    const [entry] = collectFromFileList([file]);
    expect(entry.file).toBe(file);
  });
});

describe("summarize", () => {
  it("sums sizes and de-duplicates recognized types in first-seen order", () => {
    const files = collectFromFileList([
      new File(["a"], "Map.sceA"),
      new File(["bb"], "Shapes.shpA"),
      new File(["ccc"], "Map2.sceA"), // same recognized type as the first
      new File(["dddd"], "Readme.md"), // unrecognized
    ]);

    const summary = summarize(files);
    expect(summary.totalBytes).toBe(1 + 2 + 3 + 4);
    expect(summary.recognizedTypes.map((t) => t.label)).toEqual([
      "Scenario/Map",
      "Shapes",
    ]);
    expect(summary.files).toHaveLength(4);
  });

  it("reports no recognized types when nothing matches", () => {
    const files = collectFromFileList([new File(["a"], "Readme.md")]);
    expect(summarize(files).recognizedTypes).toEqual([]);
  });
});

// Minimal hand-rolled fakes for the FileSystem Entry API, which jsdom does
// not implement. These model just enough of FileSystemFileEntry /
// FileSystemDirectoryEntry / DataTransferItem to exercise the recursive
// folder walk.
function fakeFileEntry(name: string, file: File): FileSystemFileEntry {
  return {
    name,
    isFile: true,
    isDirectory: false,
    file: (success: (f: File) => void) => success(file),
  } as unknown as FileSystemFileEntry;
}

function fakeDirectoryEntry(
  name: string,
  children: FileSystemEntry[]
): FileSystemDirectoryEntry {
  let delivered = false;
  return {
    name,
    isFile: false,
    isDirectory: true,
    createReader: () => ({
      readEntries: (success: (entries: FileSystemEntry[]) => void) => {
        // Real implementations return everything then an empty array on the
        // next call; emulate that so the "keep calling until empty" loop in
        // collectFiles.ts is actually exercised.
        if (delivered) {
          success([]);
        } else {
          delivered = true;
          success(children);
        }
      },
    }),
  } as unknown as FileSystemDirectoryEntry;
}

function fakeDataTransferItems(entries: FileSystemEntry[]): DataTransferItemList {
  const items = entries.map(
    (entry) =>
      ({
        kind: "file",
        webkitGetAsEntry: () => entry,
        getAsFile: () => null,
      } as unknown as DataTransferItem)
  );
  return items as unknown as DataTransferItemList;
}

describe("collectFromDataTransferItems", () => {
  it("recursively walks a dropped folder", async () => {
    const mapFile = new File(["abc"], "Map.sceA");
    const readme = new File(["hi"], "Readme.md");
    const root = fakeDirectoryEntry("Marathon 2", [
      fakeFileEntry("Map.sceA", mapFile),
      fakeDirectoryEntry("Plugins", [fakeFileEntry("Readme.md", readme)]),
    ]);

    const result = await collectFromDataTransferItems(
      fakeDataTransferItems([root])
    );

    const byPath = Object.fromEntries(result.map((f) => [f.relativePath, f]));
    expect(byPath["Marathon 2/Map.sceA"].recognizedType?.label).toBe(
      "Scenario/Map"
    );
    expect(byPath["Marathon 2/Plugins/Readme.md"]).toBeDefined();
    expect(result).toHaveLength(2);
  });

  it("falls back to flat files when webkitGetAsEntry is unavailable", async () => {
    const file = new File(["abc"], "Map.sceA");
    const items = [
      {
        kind: "file",
        getAsFile: () => file,
      } as unknown as DataTransferItem,
    ] as unknown as DataTransferItemList;

    const result = await collectFromDataTransferItems(items);
    expect(result).toHaveLength(1);
    expect(result[0].relativePath).toBe("Map.sceA");
  });
});
