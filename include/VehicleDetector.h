// ============================================================
// VehicleDetector.h  —  v5.3 (Auto-Calibration Enabled)
// Declaration-only header; implementation in VehicleDetector.cpp
// ============================================================
#pragma once
#include <Arduino.h>
#include <cstring>
#include <math.h>
#include "SignalProcessing.h"
#include "AutoCalibrator.h"

// FreeRTOS types (always available on ESP32)
#include "freertos/semphr.h"

extern SemaphoreHandle_t wsMutex;
void wsSend(const char *msg);

// ============================================================
// Detector Configuration
// ============================================================
enum class EntryDetectionMode : uint8_t
{
    BASELINE = 0,
    DERIVATIVE = 1
};

struct DetectorConfig
{
    float enter_thresh = 0.0008f;
    float exit_diff_th = 50.0f;
    float exit_ratio = 0.25f;
    uint32_t exit_hysteresis_cnt = 5;
    float absolute_min_dev = 0.0003f;

    uint32_t min_event_ms = 20;
    uint32_t max_event_ms = 8000;

    // ========== Length-based classification bounds (meters) ==========
    float motor_max_len = 2.4f;
    float car_max_len = 4.0f;
    float pickup_max_len = 5.0f;
    float van_max_len = 6.5f;
    float bus_max_len = 11.0f;
    float truck_s_max_len = 8.0f;
    float truck_2_max_len = 10.5f;
    float truck_3_max_len = 14.0f;
    float truck_4_plus_min_len = 14.0f;

    float classify_peak_to_thresh_low = 2.5f;
    float classify_peak_to_thresh_high = 8.0f;
    float classify_rise_short_ms = 45.0f;
    float classify_rise_mid_ms = 160.0f;
    float classify_rise_long_ms = 300.0f;
    float classify_energy_low = 0.00001f;
    float classify_energy_mid = 0.00008f;
    float classify_energy_high = 0.00020f;
    float classify_crest_spiky = 2.8f;
    float classify_crest_broad = 1.6f;
    float classify_skew_tol = 0.35f;
    float classify_skew_high = 0.75f;
    float classify_com_center_min = 0.38f;
    float classify_com_center_max = 0.62f;
    float classify_com_edge_min = 0.32f;
    float classify_com_edge_max = 0.68f;
    float classify_width_medium = 0.20f;
    float classify_width_wide = 0.45f;
    float classify_std_peak_high = 0.35f;
    float classify_std_peak_low = 0.18f;

    // ========== Axle / Multi-peak detection ==========
    float peak_prominence_ratio = 0.35f;
    float min_axle_distance_ms = 80.0f;
    // Relative signal change per second.
    float min_entry_slope = 0.0005f;
    uint32_t min_slow_enter_ms = 80;
    float peak_prominence_abs = 0.0002f;

    uint32_t calib_samples = 640;
    uint32_t warmup_samples = 50;
    float baseline_alpha = 0.0004f;

    uint32_t confirm_samples = 3;
    uint32_t min_event_samples = 6;
    float smoothing_alpha = 0.35f;
    float peak_to_baseline_ratio = 1.8f;
    float enter_hysteresis_ratio = 0.70f;
    float exit_hysteresis_ratio = 0.55f;

    bool auto_threshold = true;
    float enter_sigma = 5.0f;
    float abs_sigma = 2.5f;
    float min_enter_thresh = 0.0008f;
    float min_abs_dev = 0.0003f;
    float max_enter_thresh = 1.0f;
    float max_abs_dev = 1.0f;
    float default_speed_kmh = 90.0f;

    // Entry detector selection. Derivative thresholds are expressed as
    // multiples of calibrated sample-to-sample noise, not raw ADC counts.
    EntryDetectionMode entry_mode = EntryDetectionMode::BASELINE;
    float derivative_sigma = 5.0f;
    float derivative_slow_sigma = 6.0f;
    uint32_t derivative_slow_window_ms = 600;
    bool auto_reanchor_enabled = true;
};

// ============================================================
// Event Result
// ============================================================
struct EventResult
{
    static const int MAX_EVENT_SIGNAL = 768;

    uint32_t start_us, peak_us, end_us;
    float duration_ms;
    float peak_dev, mean_dev, std_dev, energy, area, crest_factor;
    float rise_ms, decay_ms, skewness, max_slope;
    int width_half_max;
    int anomaly_score;
    char vehicle_class[12];
    float baseline_at_event;
    float spatial_speed_ms;
    uint32_t sample_count;
    char channel_id[8];

    // --- Advanced Features ---
    float kurtosis;
    int num_peaks;
    float com_idx;
    float dft_re, dft_im;
    float resampled_sig[64];
    float time_sig[MAX_EVENT_SIGNAL];
    float estimated_length_m;

    // --- Fleet Classification Features ---
    float peak_distance_ms;
    int zero_crossings;
    float front_energy_ratio;
    float spectral_flatness;
    float dominant_freq_hz;
};

// ============================================================
// Detector State Machine
// ============================================================
enum class DetectorState : uint8_t
{
    WARMUP,
    CALIBRATING,
    IDLE,
    IN_EVENT
};

typedef void (*RecalibrateCallback)(const char *);

// ============================================================
// Feature flag (defined in VehicleDetector.cpp)
// ============================================================
extern bool send_full_event_features;

// ============================================================
// VehicleDetector Class
// ============================================================
class VehicleDetector
{
public:
    explicit VehicleDetector(const char *id, DetectorConfig cfg = DetectorConfig{});

    void setConfig(const DetectorConfig &c);
    DetectorConfig getConfig() const;
    void setDualLoopMode(bool enabled);
    bool isDualLoopMode() const;

    RecalibrateCallback onRecalibrateNeeded = nullptr;

    bool feed(uint32_t raw, uint32_t ts_us, EventResult &result);
    void startCalibration();
    float baseline() const;
    const char *id() const;
    float noiseStd() const;
    float noiseRms() const;
    float noisePercent() const;
    float currentAnomaly() const;
    bool activitySuspected() const;
    void noteInvalidSample(uint32_t now_ms);
    void setAutoReanchorEnabled(bool enabled);
    bool autoReanchorEnabled() const;

    void reclassify(EventResult &ev);
    void classify(EventResult &ev);
    void recalcThresholds();

    // Auto-calibration accessors
    AutoCalibrator &calibrator();
    const AutoCalibrator &calibrator() const;
    float confidence() const;
    float driftScore() const;
    SensorHealth health() const;
    void buildCalibStatus(char *buf, size_t bufSize) const;
    void buildDetailedCalibStatus(char *buf, size_t bufSize) const;

private:
    DetectorConfig cfg_;
    char id_[8];
    DetectorState state_ = DetectorState::WARMUP;
    bool dual_loop_mode_ = false;

    // Auto-calibration engine
    AutoCalibrator calibrator_;

    double calib_sum_ = 0;
    uint32_t calib_cnt_ = 0;
    double calib_mean_ = 0.0;
    double calib_m2_ = 0.0;

    float prev_calib_val_ = 0.0f;
    double calib_diff_sq_sum_ = 0.0;

    float noise_std_ = 0.0f;
    float noise_rms_ = 0.0f;
    float baseline_ = 0;
    float effective_enter_thresh_ = 0.0008f;
    float effective_absolute_min_dev_ = 0.0003f;
    float current_anomaly_ = 0.0f;
    bool activity_suspected_ = false;
    float filtered_val_ = 0.0f;
    float prev_filtered_ = 0.0f;
    uint32_t prev_filtered_us_ = 0;
    bool derivative_initialized_ = false;
    float derivative_origin_dev_ = 0.0f;
    uint32_t derivative_origin_us_ = 0;
    uint32_t derivative_rise_samples_ = 0;
    bool derivative_candidate_ = false;
    float derivative_candidate_floor_ = 0.0f;
    uint32_t sustained_dev_cnt_ = 0;
    uint32_t slow_dev_start_us_ = 0;
    uint32_t warmup_cnt_ = 0;
    uint32_t above_thresh_count_ = 0;

    static const int MAX_SIGNAL = EventResult::MAX_EVENT_SIGNAL;
    float signal_[MAX_SIGNAL];
    int signal_cnt_ = 0;
    uint32_t exit_counter_ = 0;
    uint32_t ev_start_us_ = 0, ev_peak_us_ = 0;
    float ev_peak_dev_ = 0;

    static float clampf(float v, float lo, float hi);
    void finishCalibration();
    float smoothInput(float val);
    void syncCalibration(CalibrationUpdate update, uint32_t now_ms);
    void resetFilterHistory(float seed);
    void extract_features(uint32_t end_us, EventResult &ev);
    void reset_state(DetectorState s);
    void reset_entry_tracking();
};

// ============================================================
// Free-standing utility functions
// ============================================================
void send_full_features(const EventResult &ev, void (*wsCallback)(const char *));
void reportEvent(const EventResult &ev, void (*wsCallback)(const char *));
