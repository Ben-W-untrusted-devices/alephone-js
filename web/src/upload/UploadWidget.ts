import {
  collectFromDataTransferItems,
  collectFromFileList,
  FileCollectionSummary,
  summarize,
  UploadableFile,
} from "./collectFiles";

export interface UploadWidgetOptions {
  /** Called whenever a new set of files replaces the previous one. */
  onFilesReady: (files: UploadableFile[], summary: FileCollectionSummary) => void;
}

/**
 * Minimal, dependency-free drag-and-drop + folder-picker widget for
 * supplying scenario data files. Deliberately unstyled beyond a couple of
 * state classes so it's easy to restyle later; the goal here is the
 * lowest-friction path from "user has a folder of Marathon data" to
 * UploadableFile[], not a polished UI.
 */
export class UploadWidget {
  readonly element: HTMLElement;
  private readonly statusEl: HTMLElement;
  private readonly input: HTMLInputElement;

  constructor(private readonly options: UploadWidgetOptions) {
    this.element = document.createElement("div");
    this.element.className = "alephone-upload-widget";

    const prompt = document.createElement("p");
    prompt.className = "alephone-upload-widget__prompt";
    prompt.textContent =
      "Drag your Marathon scenario folder here, or click to choose files";
    this.element.appendChild(prompt);

    this.statusEl = document.createElement("p");
    this.statusEl.className = "alephone-upload-widget__status";
    this.element.appendChild(this.statusEl);

    this.input = document.createElement("input");
    this.input.type = "file";
    this.input.multiple = true;
    // Lets users pick an entire folder in one go where supported (Chromium,
    // Safari); browsers without it just fall back to multi-file selection.
    this.input.setAttribute("webkitdirectory", "");
    this.input.style.display = "none";
    this.element.appendChild(this.input);

    this.element.addEventListener("click", (event) => {
      // input.click() itself dispatches a bubbling click event, which would
      // hit this same listener again and recurse forever -- ignore clicks
      // that already originated from the (hidden) input.
      if (event.target === this.input) return;
      this.input.click();
    });
    this.input.addEventListener("change", () => {
      if (this.input.files) this.handleFiles(collectFromFileList(this.input.files));
    });

    this.element.addEventListener("dragover", (event) => {
      event.preventDefault();
      this.element.classList.add("alephone-upload-widget--dragover");
    });
    this.element.addEventListener("dragleave", () => {
      this.element.classList.remove("alephone-upload-widget--dragover");
    });
    this.element.addEventListener("drop", (event) => {
      event.preventDefault();
      this.element.classList.remove("alephone-upload-widget--dragover");
      const items = event.dataTransfer?.items;
      if (items && items.length > 0) {
        collectFromDataTransferItems(items).then((files) => this.handleFiles(files));
      } else if (event.dataTransfer?.files) {
        this.handleFiles(collectFromFileList(event.dataTransfer.files));
      }
    });
  }

  private handleFiles(files: UploadableFile[]): void {
    const summary = summarize(files);
    this.statusEl.textContent = describeSummary(summary);
    this.options.onFilesReady(files, summary);
  }
}

function describeSummary(summary: FileCollectionSummary): string {
  if (summary.files.length === 0) {
    return "No files found.";
  }
  const sizeMb = (summary.totalBytes / (1024 * 1024)).toFixed(1);
  if (summary.recognizedTypes.length === 0) {
    return `Found ${summary.files.length} file(s), ${sizeMb} MB. No recognized scenario files -- make sure you selected the right folder.`;
  }
  const recognized = summary.recognizedTypes.map((t) => t.label).join(", ");
  return `Found ${summary.files.length} file(s), ${sizeMb} MB. Recognized: ${recognized}.`;
}
