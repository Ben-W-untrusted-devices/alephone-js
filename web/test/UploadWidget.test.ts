import { describe, expect, it, vi } from "vitest";
import { UploadWidget } from "../src/upload/UploadWidget";
import type { KnownFileType } from "../src/upload/knownFileTypes";

function fileList(files: File[]): FileList {
  return Object.assign({}, files, { length: files.length }) as unknown as FileList;
}

function inputOf(widget: UploadWidget): HTMLInputElement {
  return widget.element.querySelector("input[type=file]") as HTMLInputElement;
}

function statusTextOf(widget: UploadWidget): string | null {
  return widget.element.querySelector(".alephone-upload-widget__status")
    ?.textContent ?? null;
}

describe("UploadWidget", () => {
  it("reports files chosen via the file input", () => {
    const onFilesReady = vi.fn();
    const widget = new UploadWidget({ onFilesReady });
    const input = inputOf(widget);

    const file = new File(["abc"], "Map.sceA");
    Object.defineProperty(input, "files", { value: fileList([file]) });
    input.dispatchEvent(new Event("change"));

    expect(onFilesReady).toHaveBeenCalledTimes(1);
    const [files, summary] = onFilesReady.mock.calls[0] as [
      unknown[],
      { recognizedTypes: KnownFileType[] }
    ];
    expect(files).toHaveLength(1);
    expect(summary.recognizedTypes.map((t) => t.label)).toEqual([
      "Scenario/Map",
    ]);
  });

  it("shows a friendly summary once recognized files are found", () => {
    const widget = new UploadWidget({ onFilesReady: () => {} });
    const input = inputOf(widget);
    Object.defineProperty(input, "files", {
      value: fileList([new File(["abc"], "Map.sceA")]),
    });
    input.dispatchEvent(new Event("change"));

    expect(statusTextOf(widget)).toContain("Recognized: Scenario/Map");
  });

  it("hints when no recognized scenario files are found", () => {
    const widget = new UploadWidget({ onFilesReady: () => {} });
    const input = inputOf(widget);
    Object.defineProperty(input, "files", {
      value: fileList([new File(["abc"], "Readme.md")]),
    });
    input.dispatchEvent(new Event("change"));

    expect(statusTextOf(widget)).toContain("No recognized scenario files");
  });

  it("toggles a dragover class while a drag is in progress", () => {
    const widget = new UploadWidget({ onFilesReady: () => {} });

    widget.element.dispatchEvent(new Event("dragover", { cancelable: true }));
    expect(
      widget.element.classList.contains("alephone-upload-widget--dragover")
    ).toBe(true);

    widget.element.dispatchEvent(new Event("dragleave"));
    expect(
      widget.element.classList.contains("alephone-upload-widget--dragover")
    ).toBe(false);
  });

  it("clicking the widget opens the file picker", () => {
    const widget = new UploadWidget({ onFilesReady: () => {} });
    const input = inputOf(widget);
    const clickSpy = vi.spyOn(input, "click");

    widget.element.dispatchEvent(new MouseEvent("click", { bubbles: true }));

    expect(clickSpy).toHaveBeenCalledTimes(1);
  });
});
