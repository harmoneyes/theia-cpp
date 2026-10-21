#ifndef THEIA_EXPORT_H
#define THEIA_EXPORT_H

/* HarmonEyes Theia native library: the C interface used by the C++ SDK headers. */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct theia_sdk_t theia_sdk_t;

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
    int level;
    float confidence;
    float prob_high;
    float prob_low;
    int batch_number;
    int64_t timestamp_ns;
    const char* model_label;
} cog_load_result_t;

typedef struct {
    int sample_rate;
    float window_size;
    int screen_x;
    int screen_y;
    int dpi;
    float centimeters_from_screen;
} config_t;

typedef struct {
    int level;
    float confidence;
    int batch_number;
    int ttt_level;
    int ttt_minutes;
} fatigue_result_t;

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
    const char* name;
    uint32_t version;
    const char* commit;
} model_info_t;

typedef struct {
    const uint8_t* bgr_data;
    size_t bgr_data_len;
    int width;
    int height;
    double timestamp_unix_seconds;
    const uint8_t* encoded_data;
    size_t encoded_data_len;
    const uint8_t* extradata;
    size_t extradata_len;
    int is_keyframe;
} video_frame_t;

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

typedef void (*theia_log_callback_t)(const char* message, void* user_data);
typedef void (*theia_cog_load_callback_t)(cog_load_result_t result, void* user_data);
typedef void (*theia_fatigue_callback_t)(fatigue_result_t result, void* user_data);
typedef void (*theia_alert_callback_t)(alert_event_t event, void* user_data);

typedef struct {
    const void* reserved_0;
    const char* platform;
    config_t config;
    theia_cog_load_callback_t cog_load_ace_callback;
    void* cog_load_ace_callback_user_data;
    void (*reserved_1)(void);
    void* reserved_2;
    void (*reserved_3)(void);
    void* reserved_4;
    void (*reserved_5)(void);
    void* reserved_6;
    theia_fatigue_callback_t fatigue_callback;
    void* fatigue_callback_user_data;
    void (*reserved_7)(void);
    void* reserved_8;
    void (*reserved_9)(void);
    void* reserved_10;
    void (*reserved_11)(void);
    void* reserved_12;
    void (*reserved_13)(void);
    void* reserved_14;
    void (*reserved_15)(void);
    void* reserved_16;
    void (*reserved_17)(void);
    void* reserved_18;
    theia_log_callback_t log_callback;
    void* log_callback_user_data;
    void (*reserved_19)(void);
    void* reserved_20;
    void (*reserved_21)(void);
    void* reserved_22;
    const void* reserved_23;
    const void* reserved_24;
    int disable_cog_load_model;
    int reserved_25;
    const void* reserved_26;
    int reserved_27;
    const void* reserved_28;
    int reserved_29;
    const void* reserved_30;
    int reserved_31;
    const void* reserved_32;
    int disable_fatigue_model;
    const void* reserved_33;
    const char* model_cache_dir;
    const void* reserved_34;
    const void* reserved_35;
    const char* lease_token;
    const char* lease_expected_device_id;
    const char* lease_expected_tenant_id;
    const void* reserved_36;
} options_t;

theia_error_code_t theia_get_last_error_code(void);
const char* theia_get_last_error_message(void);
theia_sdk_t* theia_sdk_create(options_t options);
void theia_sdk_destroy(theia_sdk_t* sdk);
const char* theia_sdk_version(void);
int theia_sdk_start_session(theia_sdk_t* sdk, const char* session_uuid, const char* task_type);
int theia_sdk_stop_session(theia_sdk_t* sdk);
int theia_sdk_add_gaze_samples(theia_sdk_t* sdk, gaze_sample_t* samples, size_t samples_count);
int theia_sdk_start_data_streaming(theia_sdk_t* sdk);
int theia_sdk_stop_data_streaming(theia_sdk_t* sdk);
int theia_sdk_is_tracker_connected(const theia_sdk_t* sdk);
int theia_sdk_has_connection_timeout(const theia_sdk_t* sdk);
int theia_sdk_get_cog_load_ace(const theia_sdk_t* sdk, cog_load_result_t* out);
int theia_sdk_get_fatigue(const theia_sdk_t* sdk, fatigue_result_t* out);
int theia_sdk_get_attention_ace(const theia_sdk_t* sdk, attention_result_t* out);
int theia_sdk_get_mental_readiness_ace(const theia_sdk_t* sdk, double elapsed_seconds, mental_readiness_result_t* out);
int theia_sdk_get_cog_load_prediction_history(const theia_sdk_t* sdk, const cog_load_result_t** out, size_t* out_count);
int theia_sdk_start_video(theia_sdk_t* sdk);
int theia_sdk_stop_video(theia_sdk_t* sdk);
int theia_sdk_is_video_streaming(const theia_sdk_t* sdk);
int theia_sdk_get_video_frame(const theia_sdk_t* sdk, video_frame_t* out);
int theia_sdk_set_ganzin_connection(theia_sdk_t* sdk, const char* ip, int port, int rtsp_port);
int theia_sdk_set_fatigue_time_offset_seconds(theia_sdk_t* sdk, double seconds);
int theia_sdk_set_mental_readiness_min_session_seconds(theia_sdk_t* sdk, double seconds);
int theia_sdk_set_cog_load_model_settings(theia_sdk_t* sdk, int consensus_window_size, float consensus_threshold, float low_confidence_threshold, float high_confidence_threshold);
int theia_sdk_set_graph_start_time(theia_sdk_t* sdk, double unix_seconds);
int theia_sdk_get_model_info(const theia_sdk_t* sdk, const model_info_t** out, size_t* out_count);
int theia_sdk_process_baselining_data(theia_sdk_t* sdk, double start_time_sec, double end_time_sec, ground_truth_point_t* ground_truth_values, size_t ground_truth_values_count);
int theia_sdk_process_general_model_baseline(theia_sdk_t* sdk, double easy_start, double easy_end, double hard_start, double hard_end, int easy_gt, int hard_gt);
int theia_sdk_process_refining_data(theia_sdk_t* sdk, ground_truth_point_t* ground_truth_values, size_t ground_truth_values_count, double baselining_end_time, const char* refining_model);
int theia_sdk_save_refined_model(const theia_sdk_t* sdk, const char* path);
int theia_sdk_load_refined_model(theia_sdk_t* sdk, const char* path);
int theia_sdk_reset_to_base_model(theia_sdk_t* sdk);
int theia_sdk_set_alert_rules(theia_sdk_t* sdk, const char* rules_json);
int theia_sdk_set_alert_callback(theia_sdk_t* sdk, theia_alert_callback_t callback, void* callback_user_data);
int theia_sdk_list_alert_rules(const theia_sdk_t* sdk, const alert_rule_info_t** out, size_t* out_count);
alert_rules_status_t theia_sdk_get_alert_rules_status(const theia_sdk_t* sdk);
int theia_sdk_set_alert_subject(theia_sdk_t* sdk, const char* subject);
int theia_sdk_enable_alert_sync(theia_sdk_t* sdk, const char* api_base_url, int interval_seconds, int report_firings);
int theia_sdk_disable_alert_sync(theia_sdk_t* sdk);
int theia_sdk_refresh_alert_rules(theia_sdk_t* sdk);
int theia_sdk_set_lease_token(theia_sdk_t* sdk, const char* lease_token);

#ifdef __cplusplus
}
#endif

#endif /* THEIA_EXPORT_H */
