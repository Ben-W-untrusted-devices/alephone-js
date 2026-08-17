import { existsSync, readdirSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { recognizeFileType } from "../src/upload/knownFileTypes";

/**
 * Exercises file-type recognition against the real Marathon 2 data kept
 * outside this repo for local testing (see ../../CLAUDE.md and
 * ../../WEB_PORT_PLAN.md: that data must never be copied into the repo or
 * bundled with the app). Reads it in place and skips entirely if it isn't
 * present, since it won't exist in CI or on other contributors' machines.
 */
const here = path.dirname(fileURLToPath(import.meta.url));
const dataDir =
  process.env.ALEPHONE_TEST_DATA_DIR ??
  path.resolve(here, "../../../Marathon 2");

describe.skipIf(!existsSync(dataDir))("real Marathon 2 data", () => {
  it("recognizes the core scenario data files", () => {
    const names = readdirSync(dataDir);
    const recognized = names
      .map((name) => recognizeFileType(name))
      .filter((t): t is NonNullable<typeof t> => t != null)
      .map((t) => t.label);

    expect(recognized).toEqual(
      expect.arrayContaining(["Scenario/Map", "Shapes", "Sounds"])
    );
  });

  it("recognizes the folder's music, which is loadable but not scenario data", () => {
    // Music.ogg used to be expected to come back unrecognized. It became a
    // known type deliberately, once music playback landed (WEB_PORT_PLAN.md,
    // M5) -- it isn't scenario data, but it does need to reach the engine.
    expect(recognizeFileType("Music.ogg")?.label).toBe("Music");
  });

  it("does not misclassify the folder's non-scenario entries", () => {
    expect(recognizeFileType("Readme.md")).toBeUndefined();
  });
});
