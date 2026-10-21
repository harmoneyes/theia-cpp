/**
 * Copyright 2023 Tobii AB
 */
import { FrameProcessor } from './FrameProcessor.js';
export const ERRMSG_NO_CALIB_CAP = "The application's Nexus Web license does not include the calibration capability.";
export class CalibrationSession {
    constructor(algoModule, internalWET, onCalibrationStopped, frameWidth, frameHeight, frameFormat) {
        this.algoModule = algoModule;
        this.internalWET = internalWET;
        this.onCalibrationStopped = onCalibrationStopped;
        this.calibrationStopped = false;
        /**
         * Send a webcam frame and calibration point coordinates to provide calibration data
         * to the eye tracker. Results from the analysis will be
         * emitted through the `gaze` observable.
         * @param frameData raw frame data - ensure that the data are in the same format
         * as specified when constructing the EyeTracker object
         * @param calibrationPoint the point on screen that is being calibrated
         * @param options.frameWidth the width [px] of the provided frame. if this is not specified, the frameWidth
         * specified in a previous send[Calibration]Frame call or when creating the associated EyeTracker object
         * will be used
         * @param options.frameHeight the width [px] of the provided frame. if this is not specified, the frameHeight
         * previously specified will be used
         * @param options.frameFormat the format of the provided frame. if this is not specified, the frameFormat
         * previously specified will be used
         * @returns an object with keys 'collectionSuccess' (true if calibration data were succesfully
         * collected from frame, otherwise false) and 'collectionAtPointFinished' (true if enough
         * calibration frames have been collected at point, otherwise false)
         */
        this.sendCalibrationFrame = (frameData, calibrationPoint, options = {}) => {
            this.assertHasNotStopped();
            const calibrationOutput = this.frameProcessor.processCalibrationFrame(frameData, performance.now(), calibrationPoint, options);
            return {
                collectionSuccess: calibrationOutput.valid,
                collectionAtPointFinished: calibrationOutput.enoughDataCollected
            };
        };
        /**
         * Compute a calibration based on data collected so far (with sendCalibrationFrame)
         * and apply the resulting calibration to the associated eye tracker.
         */
        this.computeAndApply = () => {
            this.assertHasNotStopped();
            const errorCode = this.internalWET.calibrateNow();
            if (errorCode === -1) {
                throw new Error(ERRMSG_NO_CALIB_CAP);
            }
            else if (errorCode !== 0) {
                throw new Error('Calibration application failed. Please double check how calibration procedure is performed.');
            }
        };
        /**
         * Signal that the calibration session has been completed and no
         * further calls to this session's methods will be done.
         */
        this.stop = () => {
            this.assertHasNotStopped('the calibration session has already been stopped');
            this.internalWET.calibrationStop();
            this.onCalibrationStopped();
        };
        this.assertHasNotStopped = (errMsg) => {
            if (this.calibrationStopped) {
                if (errMsg) {
                    throw new Error(errMsg);
                }
                throw new Error('the calibration session has been stopped. if you wish to recalibrate,' +
                    'run the eye tracker method .startCalibration to start a new session');
            }
        };
        const errorCodeCalibrationClear = this.internalWET.calibrationClear();
        if (errorCodeCalibrationClear === -1) {
            throw new Error(ERRMSG_NO_CALIB_CAP);
        }
        else if (errorCodeCalibrationClear !== 0) {
            // 0 corresponds to ALGO_OK, other values indicate errors
            throw new Error('Calibration clear failed.');
        }
        const errorCode = this.internalWET.calibrationStart();
        if (errorCode === -1) {
            throw new Error(ERRMSG_NO_CALIB_CAP);
        }
        else if (errorCode !== 0) {
            // 0 corresponds to ALGO_OK, other values indicate errors
            throw new Error('Calibration start failed.');
        }
        this.frameProcessor = new FrameProcessor(this.algoModule, this.internalWET, frameWidth, frameHeight, frameFormat);
    }
}
//# sourceMappingURL=CalibrationSession.js.map