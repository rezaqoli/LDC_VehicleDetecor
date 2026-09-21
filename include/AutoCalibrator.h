#pragma once

#include <Arduino.h>
#include <math.h>

struct AutoCalibConfig
{
    uint32_t post_event_hold_ms = 5000;
    uint32_t min_quiet_ms = 15000;
    uint32_t calibration_samples = 640;
    uint32_t shadow_block_ms = 1000;
    float quiet_max_std_ratio = 0.0015f;
    float quiet_max_slope = 0.0002f; // relative change per second
    float baseline_alpha_fast = 0.02f;
    float baseline_alpha_slow = 0.0003f;
    float baseline_alpha_main = 0.0004f;
    float noise_alpha = 0.01f;
    float drift_warn_threshold = 4.0f;
    float drift_fault_threshold = 12.0f;
    float outlier_sigma_limit = 5.0f;
    float min_reanchor_ratio = 0.0003f;
    float confidence_decay_rate = 0.001f;
    float confidence_boost_rate = 0.005f;
    uint32_t fault_holdoff_ms = 30000;
    uint32_t degraded_holdoff_ms = 10000;
    bool auto_reanchor_enabled = true;
};

enum class SensorHealth : uint8_t { HEALTHY, DEGRADED, FAULT };
enum class CalibState : uint8_t { IDLE, COLLECTING, APPLYING, QUIET_MONITOR, AUTO_RECALIBRATING };
enum class CalibrationUpdate : uint8_t { NONE, TRACKED, REANCHORED, MANUAL_APPLIED, REJECTED };

struct CalibrationSnapshot
{
    float baseline = 0.0f;
    float noise_std = 0.0f;
    float noise_rms = 0.0f;
    float confidence = 0.0f;
    float drift_score = 0.0f;
    float shadow_center = 0.0f;
    float shadow_offset = 0.0f;
    uint32_t stable_ms = 0;
    SensorHealth health = SensorHealth::HEALTHY;
    CalibState state = CalibState::IDLE;
    bool valid = false;
    bool auto_reanchor_enabled = true;
};

class AutoCalibrator
{
public:
    AutoCalibrator();
    explicit AutoCalibrator(const AutoCalibConfig &cfg);
    void begin(uint32_t now_ms);
    void reset();
    void setCalibrationResult(float baseline, float noise_std, float noise_rms, uint32_t now_ms);
    CalibrationUpdate onSample(float filtered, uint32_t now_ms, bool activity_suspected);
    void onInvalidSample(uint32_t now_ms);
    void onEventStart(uint32_t now_ms);
    void onEventEnd(uint32_t now_ms);
    void startManualCalibration();
    void startAutoRecalibration();
    void setAutoReanchorEnabled(bool enabled);
    bool autoReanchorEnabled() const;
    bool isQuiet() const;
    bool shouldRecalibrate() const;
    bool isFault() const;
    bool isDegraded() const;
    bool isCalibrating() const;
    float baseline() const;
    float baselineFast() const;
    float baselineSlow() const;
    float noiseStd() const;
    float noiseRms() const;
    float noisePercent() const;
    float confidence() const;
    float driftScore() const;
    float shadowCenter() const;
    float shadowOffset() const;
    uint32_t stableDurationMs() const;
    SensorHealth health() const;
    CalibState state() const;
    CalibrationSnapshot snapshot() const;
    float enterThreshold(float enter_sigma, float min_enter, float max_enter) const;
    float absoluteMinDev(float abs_sigma, float min_dev, float max_dev) const;
    void setConfig(const AutoCalibConfig &cfg);
    AutoCalibConfig getConfig() const;
    void buildStatusString(char *buf, size_t bufSize) const;
    void buildDetailedStatusString(char *buf, size_t bufSize) const;

private:
    static const uint8_t MAX_SHADOW_BLOCKS = 15;
    AutoCalibConfig cfg_;
    float baseline_fast_, baseline_slow_, baseline_;
    float noise_variance_, noise_std_, noise_rms_, prev_sample_, diff_sq_sum_;
    float drift_score_, confidence_, shadow_center_, shadow_noise_;
    uint32_t stable_ms_, last_now_ms_, last_event_end_ms_, last_calib_ms_;
    uint32_t degraded_since_ms_, fault_since_ms_, invalid_since_ms_;
    CalibState state_;
    SensorHealth health_;
    CalibrationUpdate last_update_;
    uint32_t calib_sample_count_;
    double calib_mean_, calib_m2_, calib_diff_sq_sum_;
    float calib_prev_val_;
    uint32_t block_start_ms_, block_count_;
    double block_mean_, block_m2_, block_diff_sq_sum_;
    float block_prev_;
    float block_means_[MAX_SHADOW_BLOCKS], block_stds_[MAX_SHADOW_BLOCKS], block_rms_[MAX_SHADOW_BLOCKS];
    uint8_t shadow_count_, shadow_head_;
    void resetManualCollector();
    void resetShadow(uint32_t now_ms);
    void collectManual(float sample);
    bool finishManual(uint32_t now_ms);
    void collectShadow(float sample, uint32_t now_ms);
    CalibrationUpdate finishShadowBlock(uint32_t now_ms);
    bool evaluateShadow(float &center, float &stddev, float &rms, float &slope) const;
    void applyModel(float baseline, float noise_std, float noise_rms, uint32_t now_ms, CalibrationUpdate update);
    void updateActiveModel(float sample);
    void updateDrift();
    void updateConfidence(bool accepted);
    void checkFault(uint32_t now_ms);
    static float median(float *values, uint8_t count);
    static uint32_t elapsed(uint32_t now, uint32_t then);
};
