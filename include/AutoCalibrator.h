#pragma once

#include <Arduino.h>
#include <math.h>

// ============================================================
// Auto-Calibration Configuration
// ============================================================
struct AutoCalibConfig
{
    uint32_t post_event_hold_ms = 5000;
    uint32_t min_quiet_ms = 15000;
    uint32_t recalib_min_interval_ms = 6 * 3600 * 1000;

    float quiet_max_std_ratio = 0.0015f;
    float quiet_max_slope = 0.0002f;

    float baseline_alpha_fast = 0.02f;
    float baseline_alpha_slow = 0.0003f;
    float baseline_alpha_main = 0.0004f;
    float noise_alpha = 0.01f;

    float drift_warn_threshold = 4.0f;
    float drift_fault_threshold = 12.0f;

    float outlier_sigma_limit = 5.0f;

    float confidence_decay_rate = 0.001f;
    float confidence_boost_rate = 0.005f;

    uint32_t fault_holdoff_ms = 30000;
    uint32_t degraded_holdoff_ms = 10000;
};

// ============================================================
// Sensor Health State
// ============================================================
enum class SensorHealth : uint8_t
{
    HEALTHY,
    DEGRADED,
    FAULT
};

// ============================================================
// Calibration State
// ============================================================
enum class CalibState : uint8_t
{
    IDLE,
    COLLECTING,
    APPLYING,
    QUIET_MONITOR,
    AUTO_RECALIBRATING
};

// ============================================================
// AutoCalibrator Class
// ============================================================
class AutoCalibrator
{
public:
    AutoCalibrator();
    explicit AutoCalibrator(const AutoCalibConfig &cfg);

    void begin(uint32_t now_ms);
    void reset();

    // Sample interface
    void onSample(float raw, uint32_t now_ms, bool event_active);

    // Event lifecycle
    void onEventStart(uint32_t now_ms);
    void onEventEnd(uint32_t now_ms);

    // Manual calibration
    void startManualCalibration();
    void startAutoRecalibration();

    // State queries
    bool isQuiet() const;
    bool shouldRecalibrate() const;
    bool isFault() const;
    bool isDegraded() const;
    bool isCalibrating() const;

    // Accessors
    float baseline() const;
    float baselineFast() const;
    float baselineSlow() const;
    float noiseStd() const;
    float noiseRms() const;
    float noisePercent() const;
    float confidence() const;
    float driftScore() const;
    SensorHealth health() const;
    CalibState state() const;

    // Threshold computation
    float enterThreshold(float enter_sigma, float min_enter, float max_enter) const;
    float absoluteMinDev(float abs_sigma, float min_dev, float max_dev) const;

    // Configuration
    void setConfig(const AutoCalibConfig &cfg);
    AutoCalibConfig getConfig() const;

    // Build status string for reporting
    void buildStatusString(char *buf, size_t bufSize) const;

private:
    AutoCalibConfig cfg_;

    // Triple baseline
    float baseline_fast_;
    float baseline_slow_;
    float baseline_;

    // Noise estimation
    float noise_std_;
    float noise_rms_;
    float prev_sample_;
    float diff_sq_sum_;

    // Drift detection
    float drift_score_;
    uint32_t drift_warn_duration_ms_;

    // Confidence
    float confidence_;

    // State tracking
    CalibState state_;
    SensorHealth health_;

    uint32_t last_event_end_ms_;
    uint32_t quiet_start_ms_;
    uint32_t last_calib_ms_;
    uint32_t last_fault_ms_;
    uint32_t last_degraded_ms_;

    // Calibration collection (for manual/auto recalibration)
    uint32_t calib_sample_count_;
    double calib_sum_;
    double calib_m2_;
    double calib_diff_sq_sum_;
    float calib_prev_val_;

    // Internal methods
    void updateBaselines(float sample);
    void updateNoise(float sample);
    void updateDrift();
    void updateConfidence(bool quiet_ok);
    void applyCalibrationResult();
    bool verifyQuietConditions() const;
    void checkFault();
};
