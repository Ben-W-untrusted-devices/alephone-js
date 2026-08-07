/**
 * The subset of Emscripten's Module.FS API this port's browser-side glue
 * code depends on. Kept minimal and separate from the real (much larger)
 * FS type so the mounting logic can be unit-tested against a plain fake
 * instead of needing a real compiled Module in the test environment.
 */
export interface EmscriptenFS {
  mkdirTree(path: string): void;
  writeFile(path: string, data: Uint8Array): void;
}
