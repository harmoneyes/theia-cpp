/**
 * Copyright 2023 Tobii AB
 */
import { checkFrameResolution } from './helpers.js';
import { FrameFormat } from './types.js';
export class FrameProcessor {
    constructor(algoModule, internalWET, frameWidth, frameHeight, frameFormat) {
        this.algoModule = algoModule;
        this.internalWET = internalWET;
        this.frameWidth = frameWidth;
        this.frameHeight = frameHeight;
        this.frameFormat = frameFormat;
        this.updateFrameSettings = ({ frameWidth, frameHeight, frameFormat }) => {
            checkFrameResolution(frameWidth || this.frameWidth, frameHeight || this.frameHeight);
            this.frameWidth = frameWidth || this.frameWidth;
            this.frameHeight = frameHeight || this.frameHeight;
            if (frameFormat !== undefined) {
                this.frameFormat = frameFormat;
            }
            const newDataSize = this.frameWidth * this.frameHeight;
            if (newDataSize !== this.frameDataSize) {
                this.frameDataSize = newDataSize;
                this.allocImagePointer();
            }
            if (this.frameFormat === FrameFormat.RGB) {
                this.internalFrameFormat = this.algoModule.InternalFrameFormat.FRAME_FORMAT_RGB;
                this.frameDataSize *= 3;
            }
            else if (this.frameFormat === FrameFormat.RGBA) {
                this.internalFrameFormat = this.algoModule.InternalFrameFormat.FRAME_FORMAT_RGBA;
                this.frameDataSize *= 4;
            }
            else if (this.frameFormat === FrameFormat.GRAYSCALE) {
                this.internalFrameFormat = this.algoModule.InternalFrameFormat.FRAME_FORMAT_GRAY8;
            }
        };
        this.allocImagePointer = () => {
            if (this.imagePointer) {
                this.algoModule._free(this.imagePointer);
            }
            this.imagePointer = this.algoModule._malloc(this.frameDataSize);
        };
        this.processRegularFrame = (frameData, timestamp, options = {}) => {
            if (options.frameWidth || options.frameHeight || options.frameFormat) {
                this.updateFrameSettings(options);
            }
            if (!this.imagePointer) {
                throw new Error('processRegularFrame called while imagePointer is undefined, ' +
                    'which indicates that the FrameProcessor instance has been destroyed');
            }
            this.algoModule.HEAPU8.set(frameData, this.imagePointer);
            const internalFrameMetadata = {
                timestampMs: timestamp,
                widthPx: this.frameWidth,
                heightPx: this.frameHeight,
                format: this.internalFrameFormat,
                dataSize: this.frameDataSize
            };
            let processFrameResult = this.internalWET.processFrame(this.imagePointer, internalFrameMetadata);
            return processFrameResult;
        };
        this.processCalibrationFrame = (frameData, timestamp, stimulusPoint, options = {}) => {
            if (options.frameWidth || options.frameHeight || options.frameFormat) {
                this.updateFrameSettings(options);
            }
            if (!this.imagePointer) {
                throw new Error('processCalibrationFrame called while imagePointer is undefined, ' +
                    'which indicates that the FrameProcessor instance has been destroyed');
            }
            this.algoModule.HEAPU8.set(frameData, this.imagePointer);
            const wasmFrameMetadata = {
                timestampMs: timestamp,
                widthPx: this.frameWidth,
                heightPx: this.frameHeight,
                format: this.internalFrameFormat,
                dataSize: this.frameDataSize
            };
            let processFrameResult = this.internalWET.processCalibrationFrame(this.imagePointer, wasmFrameMetadata, stimulusPoint.x, stimulusPoint.y);
            return processFrameResult;
        };
        this.destroy = () => {
            if (this.imagePointer !== undefined) {
                this.algoModule._free(this.imagePointer);
                this.imagePointer = undefined;
            }
        };
        this.frameDataSize = frameWidth * frameHeight;
        checkFrameResolution(frameWidth, frameHeight);
        if (frameFormat === FrameFormat.RGB) {
            this.internalFrameFormat = this.algoModule.InternalFrameFormat.FRAME_FORMAT_RGB;
            this.frameDataSize *= 3;
        }
        else if (frameFormat === FrameFormat.RGBA) {
            this.internalFrameFormat = this.algoModule.InternalFrameFormat.FRAME_FORMAT_RGBA;
            this.frameDataSize *= 4;
        }
        else {
            this.internalFrameFormat = this.algoModule.InternalFrameFormat.FRAME_FORMAT_GRAY8;
        }
        this.imagePointer = this.algoModule._malloc(this.frameDataSize);
    }
}
//# sourceMappingURL=FrameProcessor.js.map