import { LICENSE_VALIDATION_ERROR, LICENSE_VALIDATION_RESULT } from './constants.js';
/**
 * Copyright 2023 Tobii AB
 */
export const checkFrameResolution = (frameWidth, frameHeight) => {
    const MAX_FRAME_WIDTH = 1920;
    const MAX_FRAME_HEIGHT = 1080;
    if (frameWidth > MAX_FRAME_WIDTH) {
        throw new Error('You have provided too high a frameWidth value. The maximum allowed resolution is 1920x1080.');
    }
    if (frameHeight > MAX_FRAME_HEIGHT) {
        throw new Error('You have provided too high a frameHeight value. The maximum allowed resolution is 1920x1080.');
    }
};
/**
 * Replace numeric codes in license failure messages with their symbolic names.
 * - "... error: N" -> ERROR_*
 * - "... result: N" -> LICENSE_VALIDATION_RESULT_*
 * Code 0 is intentionally not mapped.
 */
export function normalizeLicenseMessage(text) {
    if (!text)
        return text;
    // result: <code> => LICENSE_VALIDATION_RESULT_*
    const resultPattern = /(result\s*:\s*)(\d+)/gi;
    if (resultPattern.test(text)) {
        return text.replace(resultPattern, (full, prefix, num) => {
            const code = parseInt(num, 10);
            const name = LICENSE_VALIDATION_RESULT[code];
            return name ? `${prefix}${name}` : full;
        });
    }
    // error: <code> => LICENSE_VALIDATION_ERROR_*
    const errorPattern = /(error\s*:\s*)(\d+)/gi;
    if (errorPattern.test(text)) {
        return text.replace(errorPattern, (full, prefix, num) => {
            const code = parseInt(num, 10);
            const name = LICENSE_VALIDATION_ERROR[code];
            return name ? `${prefix}${name}` : full;
        });
    }
    return text;
}
//# sourceMappingURL=helpers.js.map