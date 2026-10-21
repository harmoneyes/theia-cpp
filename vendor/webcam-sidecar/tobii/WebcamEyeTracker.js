/**
 * Copyright 2023 Tobii AB
 */
var __awaiter = (this && this.__awaiter) || function (thisArg, _arguments, P, generator) {
    function adopt(value) { return value instanceof P ? value : new P(function (resolve) { resolve(value); }); }
    return new (P || (P = Promise))(function (resolve, reject) {
        function fulfilled(value) { try { step(generator.next(value)); } catch (e) { reject(e); } }
        function rejected(value) { try { step(generator["throw"](value)); } catch (e) { reject(e); } }
        function step(result) { result.done ? resolve(result.value) : adopt(result.value).then(fulfilled, rejected); }
        step((generator = generator.apply(thisArg, _arguments || [])).next());
    });
};
import algoModuleFactory from './nexus_wasm.js';
import { FrameFormat } from './types.js';
import { CalibrationSession, ERRMSG_NO_CALIB_CAP } from './CalibrationSession.js';
import { FrameProcessor } from './FrameProcessor.js';
import { checkFrameResolution, normalizeLicenseMessage } from './helpers.js';
export class WebcamEyeTracker {
    /**
     *
     * @param frameWidth width, in pixels, of webcam frames that will be fed with `sendFrame` (maximum of 1920)
     * @param frameHeight height of webcam frames that will be fed (maximum of 1080)
     * @param frameFormat color format of webcam frames that will be fed. defaults to RGB
     * @param screenWidthMm width, in millimeters, of device screen that filmed subject is looking at
     * @param screenHeightMm height of screen/display
     * @param fieldOfViewAngle webcam diagonal field of view angle, in degrees (about 75 for most regular webcams)
     * @param licenseInformation license key content as received from Tobii
     */
    constructor(frameWidth, frameHeight, frameFormat = FrameFormat.RGB, screenWidthMm, screenHeightMm, fieldOfViewAngle = 75, licenseInformation) {
        this.screenWidthMm = screenWidthMm;
        this.screenHeightMm = screenHeightMm;
        this.fieldOfViewAngle = fieldOfViewAngle;
        this.algoModule = undefined;
        this.readyResolved = false;
        this.isCalibrating = false;
        this.gazeSubscribers = new Set();
        /**
         * Update the the screen settings of the screen your application is running on. Correct screen settings
         * are required for good eye tracking results.
         * @param screenWidthMm Screen width in mm.
         * @param screenHeightMm Screen height in mm.
         */
        this.updateScreenSettings = (screenWidthMm, screenHeightMm) => {
            if (!this.algoModule || !this.internalWET) {
                throw new Error(WebcamEyeTracker.NOT_READY_ERRMSG);
            }
            this.assertIsNotCalibrating();
            if (screenWidthMm <= 0 || screenHeightMm <= 0) {
                throw new Error('incorrect screen specification - both width and height must be non-negative values');
            }
            this.screenWidthMm = screenWidthMm;
            this.screenHeightMm = screenHeightMm;
            this.internalWET.updateScreenSize(screenWidthMm, screenHeightMm);
        };
        /**
         * Subscribe a callback to the Gaze stream. All subscribers are continuously called
         * with Gaze objects, representing gaze x/y position on the screen, resulting from
         * analysis of `sendFrame` input.
         * @returns a subscription with a single method, unsubscribe, which may be called to
         * unsubscribe the subscriber from the Gaze stream
         */
        this.subscribeToGaze = (subscriber) => {
            this.gazeSubscribers.add(subscriber);
            return {
                unsubscribe: () => {
                    this.gazeSubscribers.delete(subscriber);
                }
            };
        };
        /**
         * Unsubscribe a callback from the Gaze stream. If the callback is not
         * currently subscribed to the stream, nothing happens.
         */
        this.unsubscribeFromGaze = (subscriber) => {
            this.gazeSubscribers.delete(subscriber);
        };
        /**
         * Send a webcam frame for eye tracking analysis. Results from the analysis will be
         * emitted through the `gaze` observable.
         * @param frameData raw frame data - ensure that the data are in the same format
         * as specified when constructing the EyeTracker object
         * @param options.timestamp value that will be included as timestamp [ms] of resulting Gaze
         * emitted to gaze subscribers. note that this value is optional but strongly recommended.
         * if no value is provided, a best-effort value for time at which `sendFrame` was called
         * will be generated instead.
         * @param options.userData an object with user-defined key-value pairs which will be included with
         * the resulting Gaze emitted to subscribers
         * @param options.frameWidth the width [px] of the provided frame. if this is not specified, the frameWidth
         * specified in a previous sendFrame call or when creating the EyeTracker object will be used
         * @param options.frameHeight the width [px] of the provided frame. if this is not specified, the frameHeight
         * previously specified will be used
         * @param options.frameFormat the format of the provided frame. if this is not specified, the frameFormat
         * previously specified will be used
         * @returns true if eye data were successfully extracted from frame, otherwise false
         */
        this.sendFrame = (frameData, options = {}) => {
            if (this.readyResolved === false || !this.frameProcessor) {
                throw new Error(WebcamEyeTracker.NOT_READY_ERRMSG);
            }
            const timestamp = options.timestamp || performance.now() + this.timeOriginInEpochTime;
            const userData = options.userData || {};
            const processFrameResult = this.frameProcessor.processRegularFrame(frameData, timestamp, options);
            const eyesCaptured = Boolean(processFrameResult.valid && processFrameResult.responsiveGazeCombinedResult.valid.value);
            let gaze;
            if (eyesCaptured) {
                const gazePos = processFrameResult.responsiveGazeCombinedResult.position;
                gaze = {
                    x: gazePos.x,
                    y: gazePos.y,
                    valid: true,
                    timestamp: timestamp,
                    userData: userData
                };
            }
            else {
                gaze = {
                    x: undefined,
                    y: undefined,
                    valid: false,
                    timestamp: timestamp,
                    userData: userData
                };
            }
            this.gazeSubscribers.forEach((subscriber) => subscriber(gaze));
            return eyesCaptured;
        };
        /**
         * Emit 'complete' event to subscribers and do clean up.
         */
        this.destroy = () => {
            if (!this.algoModule || !this.frameProcessor || !this.internalWET) {
                throw new Error(WebcamEyeTracker.NOT_READY_ERRMSG);
            }
            this.frameProcessor.destroy();
            const exitError = this.internalWET.exit();
            this.algoModule = undefined;
            if (exitError !== 0) {
                throw new Error(`Web Assembly exit function (exit) returned an error, code: ${exitError}`);
            }
        };
        /**
         * Start the calibration process, placing the tracker in a state ready
         * to receive data collection requests.
         * @returns an object through which all commands for the calibration session
         * are issued - before `.stop` is called on this object, no calls
         * to the eye tracker object's methods are permitted
         */
        this.calibrationStart = () => {
            if (!this.algoModule || !this.frameProcessor || !this.internalWET) {
                throw new Error(WebcamEyeTracker.NOT_READY_ERRMSG);
            }
            this.assertIsNotCalibrating();
            this.isCalibrating = true;
            return new CalibrationSession(this.algoModule, this.internalWET, this.calibrationStop, this.frameProcessor.frameWidth, this.frameProcessor.frameHeight, this.frameProcessor.frameFormat);
        };
        /**
         * Retrieve raw data of the currently applied calibration. This is used to reapply
         * the same calibration at a later time (e.g. after the browser session is reset)
         * by calling `WebcamEyeTracker.applyCalibration`. If this method is called
         * before a calibration session has been performed, it returns the default
         * (not tailored for any particular individual) calibration data.
         * @returns an array which is to later be passed in as-is to `.applyCalibration`
         */
        this.calibrationRetrieve = () => {
            if (!this.algoModule || !this.internalWET) {
                throw new Error(WebcamEyeTracker.NOT_READY_ERRMSG);
            }
            this.assertIsNotCalibrating();
            const { memoryPointer, size } = this.internalWET.calibrationRetrieve();
            if (memoryPointer === 0) {
                throw new Error('Unable to retrieve calibration due to internal bug');
            }
            // note: here we copy the calibration data from the linear memory buffer shared with WASM,
            // so that the user can do whatever they want with the copy
            return this.algoModule.HEAPU8.slice(memoryPointer, memoryPointer + size);
        };
        /**
         * Apply a previous calibration.
         * @param calibrationData array of calibration data previously retrieved with `calibrationRetrieve`
         */
        this.calibrationApply = (calibrationData) => {
            if (!this.algoModule || !this.internalWET) {
                throw new Error(WebcamEyeTracker.NOT_READY_ERRMSG);
            }
            if (!(calibrationData instanceof Uint8Array)) {
                throw new Error('Non-Uint8Array calibration data passed. Only use arrays produced by CalibrationRetrieve');
            }
            if (!calibrationData.length) {
                throw new Error('Zero-length calibration data passed. Only use arrays produced by CalibrationRetrieve');
            }
            const calNBytes = calibrationData.length;
            const calParamsPointer = this.algoModule._malloc(calNBytes);
            this.algoModule.HEAPU8.set(calibrationData, calParamsPointer);
            const errorCode = this.internalWET.calibrationApply({
                memoryPointer: calParamsPointer,
                size: calNBytes
            });
            this.algoModule._free(calParamsPointer);
            if (errorCode === -1) {
                throw new Error(ERRMSG_NO_CALIB_CAP);
            }
            else if (errorCode !== 0) {
                throw new Error('Invalid calibration data passed. Only use arrays produced by calibrationRetrieve.');
            }
        };
        // note that this is a private method, never to be called by users, only handed over
        // to CalibrationSession objects so that they can signal when a calibration is done
        this.calibrationStop = () => {
            this.isCalibrating = false;
        };
        this.assertIsNotCalibrating = () => {
            if (this.isCalibrating) {
                throw new Error('eye tracker is in calibration mode. to enable non-calibration actions, ' +
                    'first signal calibration end through CalibrationSession instance generated by .calibrationStart');
            }
        };
        if (typeof licenseInformation !== 'string') {
            throw new Error('License key must be a string');
        }
        if (licenseInformation === '') {
            throw new Error('License key may not be empty');
        }
        checkFrameResolution(frameWidth, frameHeight);
        this.timeOriginInEpochTime = Date.now() - performance.now();
        this.ready = new Promise((resolve, reject) => __awaiter(this, void 0, void 0, function* () {
            try {
                this.algoModule = yield algoModuleFactory();
            }
            catch (err) {
                reject(err);
                return;
            }
            this.internalWET = new this.algoModule.InternalWebcamEyeTracker(frameWidth, frameHeight, screenWidthMm, screenHeightMm, fieldOfViewAngle, licenseInformation);
            this.frameProcessor = new FrameProcessor(this.algoModule, this.internalWET, frameWidth, frameHeight, frameFormat);
            const init_error = this.internalWET.init();
            if (init_error !== 0) {
                reject(`Tobii Nexus initialization failed with error code: ${init_error}`);
            }
            const waitForValidation = (resolveFn, rejectFn, iWET, waitCount = 0) => {
                const { isFinished, message } = iWET.licenseValidation();
                if (isFinished) {
                    const ok = (message || '').trim().toLowerCase() === 'validation complete';
                    if (ok) {
                        this.readyResolved = true;
                        resolveFn();
                    }
                    else {
                        rejectFn(normalizeLicenseMessage(message));
                    }
                    return;
                }
                // wait for 10 secs
                if (waitCount > 10) {
                    rejectFn('Tobii Nexus license validation connection timed out');
                    return;
                }
                // wait 1 second and then check again
                setTimeout(() => waitForValidation(resolveFn, rejectFn, iWET, waitCount + 1), 1000);
            };
            waitForValidation(resolve, reject, this.internalWET);
        }));
    }
    get frameSettings() {
        var _a, _b, _c;
        return {
            width: (_a = this.frameProcessor) === null || _a === void 0 ? void 0 : _a.frameWidth,
            height: (_b = this.frameProcessor) === null || _b === void 0 ? void 0 : _b.frameHeight,
            format: (_c = this.frameProcessor) === null || _c === void 0 ? void 0 : _c.frameFormat
        };
    }
    get screenSettings() {
        return {
            widthMm: this.screenWidthMm,
            heightMm: this.screenHeightMm
        };
    }
    get cameraFieldOfViewAngle() {
        return this.fieldOfViewAngle;
    }
}
// TODO: update this error message / how 'ready' is handled?
// since atm, if someone calls destroy and then tries to use
// eg sendFrame, they will bump into the same error, even though
// 'ready' has resolved.
WebcamEyeTracker.NOT_READY_ERRMSG = "Eye tracking has not finished initiating, wait for 'ready' to resolve before processing frames";
//# sourceMappingURL=WebcamEyeTracker.js.map