// Entry point for the native-camera module. Loads the prebuilt .node
// file matching the current platform/arch, with a fallback to a debug
// build from `node-gyp build` for local development.

const path = require('node:path');
const fs = require('node:fs');

function loadNative() {
  const platformArch = `${process.platform}-${process.arch}`;
  const candidates = [
    // Prebuilt (release) — produced by `prebuildify`. Used in the shipped app.
    path.join(__dirname, 'prebuilds', platformArch, 'nativecam.node'),
    // node-gyp release build — local dev after `npm run build`.
    path.join(__dirname, 'build', 'Release', 'nativecam.node'),
    // node-gyp debug build — local dev after `npm run build:debug`.
    path.join(__dirname, 'build', 'Debug', 'nativecam.node'),
  ];

  let lastError;
  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) {
      try {
        return require(candidate);
      } catch (err) {
        lastError = err;
      }
    }
  }
  throw new Error(
    `[native-camera] no loadable .node file found for ${platformArch}. ` +
    `Tried: ${candidates.join(', ')}` +
    (lastError ? `\nLast load error: ${lastError.message}` : '')
  );
}

module.exports = loadNative();
