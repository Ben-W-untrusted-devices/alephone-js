import { KnownFileType, recognizeFileType } from "./knownFileTypes.js";

export interface UploadableFile {
  /** Path relative to the folder the user selected/dropped, using "/" separators. */
  readonly relativePath: string;
  readonly size: number;
  readonly recognizedType?: KnownFileType;
  /** The underlying browser File, for reading bytes later (e.g. into MEMFS). */
  readonly file: File;
}

export interface FileCollectionSummary {
  readonly files: readonly UploadableFile[];
  readonly totalBytes: number;
  /** Unique recognized types, in the order first encountered. */
  readonly recognizedTypes: readonly KnownFileType[];
}

function toUploadableFile(file: File, relativePath: string): UploadableFile {
  return {
    relativePath,
    size: file.size,
    recognizedType: recognizeFileType(file.name),
    file,
  };
}

function relativePathOf(file: File): string {
  // Populated when the file came from an <input webkitdirectory> folder
  // pick; empty for individually-selected files.
  const webkitRelativePath = (file as File & { webkitRelativePath?: string })
    .webkitRelativePath;
  return webkitRelativePath && webkitRelativePath.length > 0
    ? webkitRelativePath
    : file.name;
}

/**
 * Normalizes a FileList/File[] from an <input type="file"> (with or without
 * webkitdirectory) into UploadableFile entries.
 */
export function collectFromFileList(
  input: FileList | readonly File[]
): UploadableFile[] {
  return Array.from(input).map((file) => toUploadableFile(file, relativePathOf(file)));
}

function readAllEntries(
  reader: FileSystemDirectoryReader
): Promise<FileSystemEntry[]> {
  return new Promise((resolve, reject) => {
    const all: FileSystemEntry[] = [];
    // readEntries() must be called repeatedly until it yields an empty
    // array -- a single call isn't guaranteed to return everything.
    const readBatch = () => {
      reader.readEntries((entries) => {
        if (entries.length === 0) {
          resolve(all);
        } else {
          all.push(...entries);
          readBatch();
        }
      }, reject);
    };
    readBatch();
  });
}

function readFileEntry(entry: FileSystemFileEntry): Promise<File> {
  return new Promise((resolve, reject) => entry.file(resolve, reject));
}

async function walkEntry(
  entry: FileSystemEntry,
  pathPrefix: string,
  out: UploadableFile[]
): Promise<void> {
  const path = pathPrefix ? `${pathPrefix}/${entry.name}` : entry.name;
  if (entry.isFile) {
    const file = await readFileEntry(entry as FileSystemFileEntry);
    out.push(toUploadableFile(file, path));
  } else if (entry.isDirectory) {
    const children = await readAllEntries(
      (entry as FileSystemDirectoryEntry).createReader()
    );
    for (const child of children) {
      await walkEntry(child, path, out);
    }
  }
}

/**
 * Normalizes a drag-and-drop DataTransferItemList into UploadableFile
 * entries, recursively walking dropped folders via the FileSystem Entry API
 * so a user can drop a whole scenario folder in one go. Falls back to
 * treating items as flat files on browsers without webkitGetAsEntry.
 */
export async function collectFromDataTransferItems(
  items: DataTransferItemList
): Promise<UploadableFile[]> {
  const fileItems = Array.from(items).filter((item) => item.kind === "file");
  const entries = fileItems.map((item) => item.webkitGetAsEntry?.() ?? null);

  const out: UploadableFile[] = [];
  if (entries.every((entry): entry is FileSystemEntry => entry != null)) {
    for (const entry of entries) {
      await walkEntry(entry, "", out);
    }
  } else {
    for (const item of fileItems) {
      const file = item.getAsFile();
      if (file) out.push(toUploadableFile(file, relativePathOf(file)));
    }
  }
  return out;
}

export function summarize(
  files: readonly UploadableFile[]
): FileCollectionSummary {
  const recognizedTypes: KnownFileType[] = [];
  let totalBytes = 0;
  for (const f of files) {
    totalBytes += f.size;
    if (f.recognizedType && !recognizedTypes.includes(f.recognizedType)) {
      recognizedTypes.push(f.recognizedType);
    }
  }
  return { files, totalBytes, recognizedTypes };
}
