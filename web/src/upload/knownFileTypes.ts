/**
 * Aleph One data file extensions worth recognizing in the upload widget, for
 * friendly "looks like a complete scenario" feedback only. Mirrors (a
 * non-exhaustive subset of) the extension table in
 * Source_Files/Files/FileHandler.cpp -- kept independent rather than
 * generated from it, since the C++ source isn't available at runtime here.
 * This is intentionally not used to reject anything: plugins, scripts, and
 * other supporting files don't follow a fixed naming scheme.
 */
export interface KnownFileType {
  readonly extension: string;
  readonly label: string;
}

export const KNOWN_SCENARIO_FILE_TYPES: readonly KnownFileType[] = [
  { extension: "scea", label: "Scenario/Map" },
  { extension: "shpa", label: "Shapes" },
  { extension: "snda", label: "Sounds" },
  { extension: "imga", label: "Images" },
  { extension: "phya", label: "Physics" },
  { extension: "sgaa", label: "Saved Game" },
  { extension: "fila", label: "Film/Replay" },
];

export function recognizeFileType(fileName: string): KnownFileType | undefined {
  const dot = fileName.lastIndexOf(".");
  if (dot === -1) return undefined;
  const ext = fileName.slice(dot + 1).toLowerCase();
  return KNOWN_SCENARIO_FILE_TYPES.find((t) => t.extension === ext);
}
