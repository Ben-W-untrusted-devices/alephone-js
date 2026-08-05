import { describe, expect, it } from "vitest";
import { recognizeFileType } from "../src/upload/knownFileTypes";

describe("recognizeFileType", () => {
  it("recognizes known Aleph One data extensions", () => {
    expect(recognizeFileType("Map.sceA")?.label).toBe("Scenario/Map");
    expect(recognizeFileType("Shapes.shpA")?.label).toBe("Shapes");
    expect(recognizeFileType("Sounds.sndA")?.label).toBe("Sounds");
    expect(recognizeFileType("Images.imgA")?.label).toBe("Images");
  });

  it("matches regardless of extension case", () => {
    expect(recognizeFileType("map.SCEA")?.label).toBe("Scenario/Map");
    expect(recognizeFileType("map.ScEa")?.label).toBe("Scenario/Map");
  });

  it("returns undefined for unrecognized extensions", () => {
    expect(recognizeFileType("Readme.md")).toBeUndefined();
    expect(recognizeFileType("script.lua")).toBeUndefined();
  });

  it("returns undefined when there is no extension", () => {
    expect(recognizeFileType("Plugins")).toBeUndefined();
  });
});
