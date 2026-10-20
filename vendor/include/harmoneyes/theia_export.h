#ifndef THEIA_EXPORT_H
#define THEIA_EXPORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct theia_sdk_t theia_sdk_t;
#ifdef THEIA_HAS_PUPIL_LABS
typedef struct theia_neon_t theia_neon_t;
#endif /* THEIA_HAS_PUPIL_LABS */

typedef struct {
    int64_t timestamp_ns;
    int64_t window_start_timestamp_ns;
    int window_index;
    const char* tracker;
    const char* const* features_names; /* borrowed; valid until the next call on the same thread -- copy if you need to retain */
    const double* features_values;
    int features_count;
} ace_feature_row_t;

typedef struct {
    const char* firing_id;
    const char* rule_id;
    const char* rule_name;
    const char* input;
    int level;
    int previous_level;
    const char* trigger;
    int sustained_sec;
    int level_share_percent;
    const char* message;
    int64_t timestamp_ns;
    int64_t fired_at_unix_ms;
} alert_event_t;

typedef struct {
    const char* id;
    const char* name;
    const char* summary;
    const char* input;
    const char* input_name;
    const char* levels;
    int on_increase;
    int on_decrease;
    int min_duration_sec;
    int level_share_percent;
    int level_share_window_sec;
    int cooldown_sec;
    int readiness_window_sec;
    const char* source;
    int64_t last_fired_at_unix_ms;
} alert_rule_info_t;

typedef struct {
    const char* source;
    int rule_count;
    int skipped_rule_count;
    const char* skipped_detail;
    const char* etag;
    const char* generated_at;
    const char* tenant_id;
    const char* subject;
    int64_t loaded_at_unix_ms;
    const char* last_error;
    const char* sync_state;
    const char* sync_url;
    int64_t last_sync_attempt_unix_ms;
    int64_t last_sync_success_unix_ms;
    const char* sync_error;
    int pending_firings;
    int64_t last_firing_upload_unix_ms;
    const char* firing_upload_error;
} alert_rules_status_t;

typedef struct {
    int level;
    const char* label;
    double composite_score;
    int total_observations;
} attention_result_t;

typedef struct {
    int previous_level;
    int new_level;
    int reason;
    int64_t timestamp_ns;
} attention_transition_event_t;

typedef struct {
    float fx;
    float fy;
    float cx;
    float cy;
} camera_calibration_t;

typedef struct {
    const char* vision_conditions;
    const char* sex_at_birth;
    const char* dominant_hand;
    const char* race;
    const char* ethnicity;
    int tit_1_scored;
} cog_load_metadata_t;

typedef struct {
    int level;
    float confidence;
    float prob_high;
    float prob_low;
    int batch_number;
    int64_t timestamp_ns;
    const char* model_label;
} cog_load_result_t;

typedef struct {
    int previous_level;
    int new_level;
    int reason;
    int64_t timestamp_ns;
} cog_load_transition_event_t;

typedef struct {
    int sample_rate;
    float window_size;
    int screen_x;
    int screen_y;
    int dpi;
    float centimeters_from_screen;
} config_t;

typedef struct {
    const char* name;
    const char* server;
    const char* ip;
    int port;
    const char* device_id;
    const char* device_name;
} discovered_device_t;

typedef struct {
    int level;
    float confidence;
    int batch_number;
    int ttt_level;
    int ttt_minutes;
} fatigue_result_t;

typedef struct {
    int previous_level;
    int new_level;
    int reason;
    int64_t timestamp_ns;
} fatigue_transition_event_t;

typedef struct {
    int64_t timestamp_ns;
    float left_eye_x;
    float left_eye_y;
    float right_eye_x;
    float right_eye_y;
    double pupil_diameter_left;
    double pupil_diameter_right;
    double pupil_diameter;
    float depth;
    int8_t left_eye_validity;
    int8_t right_eye_validity;
    int8_t left_eye_status;
    int8_t right_eye_status;
    float left_eye_confidence;
    float right_eye_confidence;
    int32_t blink_id;
    float eyeball_center_left_x;
    float eyeball_center_left_y;
    float eyeball_center_left_z;
    float optical_axis_left_x;
    float optical_axis_left_y;
    float optical_axis_left_z;
    float eyeball_center_right_x;
    float eyeball_center_right_y;
    float eyeball_center_right_z;
    float optical_axis_right_x;
    float optical_axis_right_y;
    float optical_axis_right_z;
    float eyelid_angle_top_left;
    float eyelid_angle_bottom_left;
    float eyelid_angle_top_right;
    float eyelid_angle_bottom_right;
    float eyelid_aperture_left_mm;
    float eyelid_aperture_right_mm;
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    float quat_w;
    float quat_x;
    float quat_y;
    float quat_z;
    float gaze_x_norm;
    float gaze_y_norm;
    float screen_width_mm;
    float screen_height_mm;
    int32_t screen_res_width_px;
    int32_t screen_res_height_px;
    double head_pose_depth_cm;
    double head_pose_tz;
} gaze_sample_t;

typedef struct {
    double time_unix_seconds;
    int ground_truth_value;
} ground_truth_point_t;

typedef struct {
    double low_percentage;
    double moderate_percentage;
    double high_percentage;
    int total_predictions;
} mental_readiness_result_t;

typedef struct {
    double nominal_window_seconds;
    double actual_window_length_seconds;
    int previous_level;
    int new_level;
    int reason;
    double mental_readiness_percentage;
    int64_t timestamp_ns;
} mental_readiness_transition_event_t;

typedef struct {
    double nominal_window_seconds;
    int64_t window_start_timestamp_ns;
    int64_t window_end_timestamp_ns;
    double low_cog_load_percentage;
    double moderate_cog_load_percentage;
    double high_cog_load_percentage;
    double mental_readiness_percentage;
    int level;
    int predictions_in_window;
} mental_readiness_window_measurement_t;

typedef struct {
    const char* name;
    uint32_t version;
    const char* commit;
} model_info_t;

typedef struct {
    float discovery_timeout_seconds;
    int buffer_size;
    float blink_pairing_window_seconds;
    int sample_rate_hz;
} neon_options_t;

typedef struct {
    int64_t timestamp_ns;
    int32_t blink_id;
    float pupil_diameter_left;
    float pupil_diameter_right;
    float eyeball_center_left_x;
    float eyeball_center_left_y;
    float eyeball_center_left_z;
    float optical_axis_left_x;
    float optical_axis_left_y;
    float optical_axis_left_z;
    float eyeball_center_right_x;
    float eyeball_center_right_y;
    float eyeball_center_right_z;
    float optical_axis_right_x;
    float optical_axis_right_y;
    float optical_axis_right_z;
    float left_eye_x;
    float left_eye_y;
    float right_eye_x;
    float right_eye_y;
    float pupil_diameter;
    int worn;
    float eyelid_angle_top_left;
    float eyelid_angle_bottom_left;
    float eyelid_angle_top_right;
    float eyelid_angle_bottom_right;
    float eyelid_aperture_left_mm;
    float eyelid_aperture_right_mm;
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    float quat_w;
    float quat_x;
    float quat_y;
    float quat_z;
} preprocessed_sample_t;

typedef struct {
    int is_refined;
    int refinement_count;
    int total_training_samples;
    const char* base_model;
    float low_confidence_threshold;
    float high_confidence_threshold;
    int consensus_window_size;
    float consensus_threshold;
} refinement_info_t;

typedef struct {
    const uint8_t* bgr_data; /* borrowed; valid only for the duration of the callback that delivers this struct -- copy the bytes if you need to retain them */
    size_t bgr_data_len;
    int width;
    int height;
    double timestamp_unix_seconds;
    const uint8_t* encoded_data; /* borrowed; valid only for the duration of the callback that delivers this struct -- copy the bytes if you need to retain them */
    size_t encoded_data_len;
    const uint8_t* extradata; /* borrowed; valid only for the duration of the callback that delivers this struct -- copy the bytes if you need to retain them */
    size_t extradata_len;
    int is_keyframe;
} video_frame_t;

typedef struct {
    const mental_readiness_window_measurement_t* windows; /* borrowed; valid only for the duration of the callback that delivers this struct -- copy the elements if you need to retain them */
    size_t windows_count;
    int64_t timestamp_ns;
} mental_readiness_ace_result_t;

typedef void (*theia_ace_features_callback_t)(void* user_data, int64_t timestamp_ns, int window_index, const char* tracker, const char* const* names, const double* values, int count);
typedef void (*theia_alert_callback_t)(alert_event_t event, void* user_data);
typedef void (*theia_attention_ace_callback_t)(attention_result_t result, void* user_data);
typedef void (*theia_attention_transition_ace_callback_t)(attention_transition_event_t event, void* user_data);
typedef void (*theia_cog_load_callback_t)(cog_load_result_t result, void* user_data);
typedef void (*theia_cog_load_transition_ace_callback_t)(cog_load_transition_event_t event, void* user_data);
typedef void (*theia_fatigue_callback_t)(fatigue_result_t result, void* user_data);
typedef void (*theia_fatigue_transition_ace_callback_t)(fatigue_transition_event_t event, void* user_data);
typedef void (*theia_log_callback_t)(const char* message, void* user_data);
typedef void (*theia_mental_readiness_ace_callback_t)(mental_readiness_ace_result_t result, void* user_data);
typedef void (*theia_mental_readiness_transition_ace_callback_t)(mental_readiness_transition_event_t event, void* user_data);
typedef void (*theia_preprocessed_callback_t)(preprocessed_sample_t sample, void* user_data);
typedef void (*theia_video_callback_t)(video_frame_t frame, void* user_data);

typedef struct {
    const char* output_dir;
    const char* platform;
    config_t config;
    theia_cog_load_callback_t cog_load_ace_callback;
    void* cog_load_ace_callback_user_data;
    theia_cog_load_callback_t cog_load_computer_callback;
    void* cog_load_computer_callback_user_data;
    theia_cog_load_callback_t cog_load_computer_metadata_callback;
    void* cog_load_computer_metadata_callback_user_data;
    theia_cog_load_callback_t cog_load_driving_callback;
    void* cog_load_driving_callback_user_data;
    theia_fatigue_callback_t fatigue_callback;
    void* fatigue_callback_user_data;
    theia_attention_ace_callback_t attention_ace_callback;
    void* attention_ace_callback_user_data;
    theia_mental_readiness_ace_callback_t mental_readiness_ace_callback;
    void* mental_readiness_ace_callback_user_data;
    theia_cog_load_transition_ace_callback_t cog_load_transition_ace_callback;
    void* cog_load_transition_ace_callback_user_data;
    theia_fatigue_transition_ace_callback_t fatigue_transition_ace_callback;
    void* fatigue_transition_ace_callback_user_data;
    theia_attention_transition_ace_callback_t attention_transition_ace_callback;
    void* attention_transition_ace_callback_user_data;
    theia_mental_readiness_transition_ace_callback_t mental_readiness_transition_ace_callback;
    void* mental_readiness_transition_ace_callback_user_data;
    theia_log_callback_t log_callback;
    void* log_callback_user_data;
    theia_video_callback_t video_callback;
    void* video_callback_user_data;
    theia_ace_features_callback_t ace_features_callback;
    void* ace_features_callback_user_data;
    const char* ace_tracker;
    const char* ace_cog_load_model_path;
    /* HAND-PATCHED, not generator output — see converters.yaml's own comment
     * on this field and generate_c_api.py's header note before regenerating.
     * Inverted from the C++ struct's enable_cog_load_model (default true) so a
     * zero-init'd (memset/aggregate-init) options_t keeps cog load ON
     * by default, consistent with every other inverted "disable_" flag in
     * this struct. cpp2c has no inverted-boolean field mapping (only
     * same-value renames via cpp_field), so this can't be expressed in YAML —
     * plain regeneration silently reverts it back to a non-inverted
     * enable_cog_load_model, reintroducing the C-ABI zero-init bug this fixes
     * (a zeroed options_t would otherwise silently disable cog load for
     * every existing C-API consumer). This was the Keygen COGNITIVE_LOAD_MODEL
     * entitlement. Re-applied automatically by this script's Step 3 after
     * every regeneration until cpp2c grows real invert support. */
    int disable_cog_load_model;
    int enable_cog_load_computer_model;
    const char* ace_cog_load_computer_model_path;
    int enable_cog_load_computer_metadata_model;
    const char* ace_cog_load_computer_metadata_model_path;
    int enable_cog_load_driving_model;
    const char* ace_cog_load_driving_model_path;
    int disable_legacy_feature_extraction;
    const char* ace_sleepiness_model_path;
    /* HAND-PATCHED, not generator output — see converters.yaml's own comment
     * on this field and generate_c_api.py's header note before regenerating.
     * Inverted from the C++ struct's enable_fatigue_model (default true) so a
     * zero-init'd (memset/aggregate-init) options_t keeps fatigue ON
     * by default, consistent with every other inverted "disable_" flag in
     * this struct. cpp2c has no inverted-boolean field mapping (only
     * same-value renames via cpp_field), so this can't be expressed in YAML —
     * plain regeneration silently reverts it back to a non-inverted
     * enable_fatigue_model, reintroducing the C-ABI zero-init bug this fixes
     * (a zeroed options_t would otherwise silently disable fatigue for
     * every existing C-API consumer). This was the Keygen SLEEPINESS_MODEL
     * entitlement. Re-applied automatically by this script's Step 3 after
     * every regeneration until cpp2c grows real invert support. */
    int disable_fatigue_model;
    const char* models_dir;
    const char* model_cache_dir;
    const char* cdn_url;
    const char* refined_model_id;
    const char* lease_token;
    const char* lease_expected_device_id;
    const char* lease_expected_tenant_id;
    const char* rpc_config_path;
} options_t;

theia_sdk_t* theia_sdk_create(options_t options);
void theia_sdk_destroy(theia_sdk_t* sdk);

/* HAND-WRITTEN, not generator output — see generate_c_api.py's Step 4.
 * Set by theia_sdk_create() when it fails and returns NULL, so a caller can
 * tell WHY: an unverified lease (THEIA_ERROR_LEASE_*) from a missing model
 * artifact (THEIA_ERROR_MODEL_UNAVAILABLE) from anything else
 * (THEIA_ERROR_UNKNOWN) — previously every constructor failure collapsed to
 * a bare NULL with no diagnostic at all. Thread-local: safe to call from
 * multiple threads each creating their own SDK instance; each sees only its
 * own last error. Valid only immediately after a theia_sdk_create() call
 * that returned NULL — a successful create DOES reset it to THEIA_ERROR_NONE
 * (so a stale code from an earlier failed attempt on this thread can't be
 * misread as describing the current, successful handle). The one other
 * function that sets it is theia_sdk_set_lease_token(), in the same way: on
 * returning -1 it says why the lease was refused, on 0 it is reset. */
typedef enum theia_error_code_t {
    THEIA_ERROR_NONE = 0,
    THEIA_ERROR_UNKNOWN = 1,
    THEIA_ERROR_MODEL_UNAVAILABLE = 2,
    THEIA_ERROR_LEASE_MALFORMED = 10,
    THEIA_ERROR_LEASE_UNKNOWN_KEY = 11,
    THEIA_ERROR_LEASE_BAD_SIGNATURE = 12,
    THEIA_ERROR_LEASE_EXPIRED = 13,
    THEIA_ERROR_LEASE_DEVICE_MISMATCH = 14,
    THEIA_ERROR_LEASE_TENANT_MISMATCH = 15,
} theia_error_code_t;

theia_error_code_t theia_get_last_error_code(void);
const char* theia_get_last_error_message(void);
int theia_sdk_start_session(theia_sdk_t* sdk, const char* session_uuid, const char* task_type);
int theia_sdk_stop_session(theia_sdk_t* sdk);
int theia_sdk_add_gaze_samples(theia_sdk_t* sdk, gaze_sample_t* samples, size_t samples_count);
int theia_sdk_get_fatigue(const theia_sdk_t* sdk, fatigue_result_t* out);
int theia_sdk_get_attention(const theia_sdk_t* sdk, attention_result_t* out);
int theia_sdk_get_mental_readiness(const theia_sdk_t* sdk, double elapsed_seconds, mental_readiness_result_t* out);
int theia_sdk_get_cog_load_ace(const theia_sdk_t* sdk, cog_load_result_t* out);
int theia_sdk_get_cog_load_computer_ace(const theia_sdk_t* sdk, cog_load_result_t* out);
int theia_sdk_get_cog_load_computer_metadata_ace(const theia_sdk_t* sdk, cog_load_result_t* out);
int theia_sdk_get_cog_load_driving_ace(const theia_sdk_t* sdk, cog_load_result_t* out);
int theia_sdk_get_mental_workload_ace(const theia_sdk_t* sdk, cog_load_result_t* out);
int theia_sdk_get_mental_workload_computer_ace(const theia_sdk_t* sdk, cog_load_result_t* out);
int theia_sdk_get_mental_workload_computer_metadata_ace(const theia_sdk_t* sdk, cog_load_result_t* out);
int theia_sdk_get_attention_ace(const theia_sdk_t* sdk, attention_result_t* out);
int theia_sdk_get_mental_readiness_ace(const theia_sdk_t* sdk, double elapsed_seconds, mental_readiness_result_t* out);
int theia_sdk_set_mental_readiness_min_session_seconds(theia_sdk_t* sdk, double seconds);
int theia_sdk_set_fatigue_time_offset_seconds(theia_sdk_t* sdk, double seconds);
int theia_sdk_set_mental_readiness_low_high_threshold(theia_sdk_t* sdk, double threshold);
int theia_sdk_set_mental_readiness_report_windows(theia_sdk_t* sdk, double* nominal_window_seconds_list, size_t nominal_window_seconds_list_count);
int theia_sdk_is_running(const theia_sdk_t* sdk);
int theia_sdk_set_ganzin_connection(theia_sdk_t* sdk, const char* ip, int port, int rtsp_port);
int theia_sdk_set_tobii_g3_connection(theia_sdk_t* sdk, const char* ip, int port, int rtsp_port);
int theia_sdk_set_tobii_g3_gaze_frequency(theia_sdk_t* sdk, int hz);
int theia_sdk_calibrate_tobii_g3(theia_sdk_t* sdk);
int theia_sdk_start_data_streaming(theia_sdk_t* sdk);
int theia_sdk_stop_data_streaming(theia_sdk_t* sdk);
int theia_sdk_is_tracker_connected(const theia_sdk_t* sdk);
int theia_sdk_is_tracker_streaming(const theia_sdk_t* sdk);
int theia_sdk_start_video(theia_sdk_t* sdk);
int theia_sdk_stop_video(theia_sdk_t* sdk);
int theia_sdk_is_video_streaming(const theia_sdk_t* sdk);
int theia_sdk_get_video_frame(const theia_sdk_t* sdk, video_frame_t* out);
int theia_sdk_start_eye_video(theia_sdk_t* sdk);
int theia_sdk_stop_eye_video(theia_sdk_t* sdk);
int theia_sdk_is_eye_video_streaming(const theia_sdk_t* sdk);
int theia_sdk_get_eye_video_frame(const theia_sdk_t* sdk, video_frame_t* out);
int theia_sdk_batch_count(const theia_sdk_t* sdk);
const char* theia_sdk_version(void);
const char* theia_sdk_feature_version(void);
int theia_set_thread_affinity_enabled(int enabled);
int theia_pin_current_thread_efficiency(void);
int theia_sdk_get_model_versions(const theia_sdk_t* sdk, const char*** out, size_t* out_count);
int theia_sdk_get_ace_features(const theia_sdk_t* sdk, ace_feature_row_t* out);
refinement_info_t theia_sdk_get_refinement_info(const theia_sdk_t* sdk);
int theia_sdk_get_model_info(const theia_sdk_t* sdk, const model_info_t** out, size_t* out_count);
int theia_sdk_set_alert_rules(theia_sdk_t* sdk, const char* rules_json);
int theia_sdk_set_alert_callback(theia_sdk_t* sdk, theia_alert_callback_t callback, void* callback_user_data);
int theia_sdk_list_alert_rules(const theia_sdk_t* sdk, const alert_rule_info_t** out, size_t* out_count);
alert_rules_status_t theia_sdk_get_alert_rules_status(const theia_sdk_t* sdk);
int theia_sdk_set_alert_subject(theia_sdk_t* sdk, const char* subject);
int theia_sdk_enable_alert_sync(theia_sdk_t* sdk, const char* api_base_url, int interval_seconds, int report_firings);
int theia_sdk_disable_alert_sync(theia_sdk_t* sdk);
int theia_sdk_refresh_alert_rules(theia_sdk_t* sdk);
int theia_sdk_set_lease_token(theia_sdk_t* sdk, const char* lease_token);
int theia_sdk_save_refined_model(const theia_sdk_t* sdk, const char* path);
int theia_sdk_load_refined_model(theia_sdk_t* sdk, const char* path);
int theia_sdk_reset_to_base_model(theia_sdk_t* sdk);
int theia_sdk_set_cog_load_model_settings(theia_sdk_t* sdk, int consensus_window_size, float consensus_threshold, float low_confidence_threshold, float high_confidence_threshold);
int theia_sdk_set_refine_settings(theia_sdk_t* sdk, float growth_rate, double lookback_seconds, float refine_eta);
int theia_sdk_set_graph_start_time(theia_sdk_t* sdk, double unix_seconds);
int theia_sdk_set_window_size(theia_sdk_t* sdk, float seconds);
int theia_sdk_set_sdk_model_confidence_threshold(theia_sdk_t* sdk, float threshold);
int theia_sdk_process_baselining_data(theia_sdk_t* sdk, double start_time_sec, double end_time_sec, ground_truth_point_t* ground_truth_values, size_t ground_truth_values_count);
int theia_sdk_process_general_model_baseline(theia_sdk_t* sdk, double easy_start, double easy_end, double hard_start, double hard_end, int easy_gt, int hard_gt);
int theia_sdk_process_refining_data(theia_sdk_t* sdk, ground_truth_point_t* ground_truth_values, size_t ground_truth_values_count, double baselining_end_time, const char* refining_model);
int theia_sdk_predict_cog_load_ace_from_features(theia_sdk_t* sdk, const char* const* features_names, const double* features_values, const int* features_row_feature_counts, size_t features_row_count, const int64_t* features_row_timestamps_ns, const cog_load_result_t** out, size_t* out_count);
int theia_sdk_refine_cog_load_ace_from_features(theia_sdk_t* sdk, const char* const* features_names, const double* features_values, const int* features_row_feature_counts, size_t features_row_count, const int64_t* features_row_timestamps_ns, int* labels, size_t labels_count);
int theia_sdk_get_cog_load_prediction_history(const theia_sdk_t* sdk, const cog_load_result_t** out, size_t* out_count);
int theia_sdk_set_profile(theia_sdk_t* sdk, const char* profile_name);
int theia_sdk_set_task(theia_sdk_t* sdk, const char* task_name);
int theia_sdk_set_environment(theia_sdk_t* sdk, const char* environment_name);
int theia_sdk_set_lambda_url(theia_sdk_t* sdk, const char* url);
int theia_sdk_set_cog_load_metadata(theia_sdk_t* sdk, cog_load_metadata_t metadata);
const char* theia_sdk_get_profile(const theia_sdk_t* sdk);
const char* theia_sdk_get_task(const theia_sdk_t* sdk);
const char* theia_sdk_get_environment(const theia_sdk_t* sdk);
int theia_sdk_cleanup(theia_sdk_t* sdk);
int theia_sdk_batch_size(const theia_sdk_t* sdk);
int theia_sdk_analysis_timestamp(const theia_sdk_t* sdk);
int theia_sdk_has_connection_timeout(const theia_sdk_t* sdk);
#ifdef THEIA_HAS_PUPIL_LABS
theia_neon_t* theia_neon_create(void);
theia_neon_t* theia_neon_create_with_options(neon_options_t options);
void theia_neon_destroy(theia_neon_t* sdk);
int theia_neon_connect(theia_neon_t* sdk, float timeout_seconds);
int theia_neon_connect_device(theia_neon_t* sdk, discovered_device_t device);
int theia_neon_disconnect(theia_neon_t* sdk);
int theia_neon_is_connected(const theia_neon_t* sdk);
int theia_neon_start_data_streaming(theia_neon_t* sdk, theia_preprocessed_callback_t callback, void* callback_user_data);
int theia_neon_stop_data_streaming(theia_neon_t* sdk);
int theia_neon_has_data(const theia_neon_t* sdk);
int theia_neon_data_buffer_size(const theia_neon_t* sdk);
int theia_neon_is_data_streaming(const theia_neon_t* sdk);
int theia_neon_start_video(theia_neon_t* sdk);
int theia_neon_stop_video(theia_neon_t* sdk);
int theia_neon_is_video_streaming(const theia_neon_t* sdk);
int theia_neon_get_video_frame(const theia_neon_t* sdk, video_frame_t* out);
int theia_neon_set_video_remux(theia_neon_t* sdk, int enabled);
int theia_neon_poll_encoded_video_frame(theia_neon_t* sdk, video_frame_t* out);
int theia_neon_set_video_preview(theia_neon_t* sdk, int enabled);
int theia_neon_start_eye_video(theia_neon_t* sdk);
int theia_neon_stop_eye_video(theia_neon_t* sdk);
int theia_neon_is_eye_video_streaming(const theia_neon_t* sdk);
int theia_neon_get_eye_video_frame(const theia_neon_t* sdk, video_frame_t* out);
int theia_neon_set_eye_video_remux(theia_neon_t* sdk, int enabled);
int theia_neon_poll_encoded_eye_video_frame(theia_neon_t* sdk, video_frame_t* out);
int theia_neon_set_eye_video_preview(theia_neon_t* sdk, int enabled);
camera_calibration_t theia_neon_get_calibration(theia_neon_t* sdk);
const char* theia_neon_start_recording(theia_neon_t* sdk);
int theia_neon_stop_recording(theia_neon_t* sdk);
int theia_neon_cancel_recording(theia_neon_t* sdk);
int theia_neon_send_event(theia_neon_t* sdk, const char* name, int64_t timestamp_ns);
int theia_neon_set_blink_log_path(theia_neon_t* sdk, const char* path);
discovered_device_t theia_neon_device_info(const theia_neon_t* sdk);
#endif /* THEIA_HAS_PUPIL_LABS */

#ifdef __cplusplus
}
#endif

#endif /* THEIA_EXPORT_H */