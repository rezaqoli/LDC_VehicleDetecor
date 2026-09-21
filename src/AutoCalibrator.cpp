#include "AutoCalibrator.h"

// ============================================================
// Constructors
// ============================================================
AutoCalibrator::AutoCalibrator()
    : cfg_(), baseline_fast_(0), baseline_slow_(0), baseline_(0),
      noise_std_(0), noise_rms_(0), prev_sample_(0), diff_sq_sum_(0),
      drift_score_(0), drift_warn_duration_ms_(0), confidence_(0),
      state_(CalibState::IDLE), health_(SensorHealth::HEALTHY),
      last_event_end_ms_(0), quiet_start_ms_(0), last_calib_ms_(0),
      last_fault_ms_(0), last_degraded_ms_(0),
      calib_sample_count_(0), calib_sum_(0), calib_m2_(0),
      calib_diff_sq_sum_(0), calib_prev_val_(0)
{
}

AutoCalibrator::AutoCalibrator(const AutoCalibConfig &cfg)
    : AutoCalibrator()
{
    cfg_ = cfg;
}

// ============================================================
// Configuration
// ============================================================
void AutoCalibrator::setConfig(const AutoCalibConfig &cfg) { cfg_ = cfg; }
AutoCalibConfig AutoCalibrator::getConfig() const { return cfg_; }

// ============================================================
// Initialization
// ============================================================
void AutoCalibrator::begin(uint32_t now_ms)
{
    reset();
    last_calib_ms_ = now_ms;
    state_ = CalibState::IDLE;
}

void AutoCalibrator::reset()
{
    baseline_fast_ = 0;
    baseline_slow_ = 0;
    baseline_ = 0;
    noise_std_ = 0;
    noise_rms_ = 0;
    prev_sample_ = 0;
    diff_sq_sum_ = 0;
    drift_score_ = 0;
    drift_warn_duration_ms_ = 0;
    confidence_ = 0;
    state_ = CalibState::IDLE;
    health_ = SensorHealth::HEALTHY;
    last_event_end_ms_ = 0;
    quiet_start_ms_ = 0;
    last_calib_ms_ = 0;
    last_fault_ms_ = 0;
    last_degraded_ms_ = 0;
    calib_sample_count_ = 0;
    calib_sum_ = 0;
    calib_m2_ = 0;
    calib_diff_sq_sum_ = 0;
    calib_prev_val_ = 0;
}

void AutoCalibrator::setCalibrationResult(float baseline, float noise_std, float noise_rms, uint32_t now_ms)
{
    reset();
    baseline_ = baseline;
    baseline_fast_ = baseline;
    baseline_slow_ = baseline;
    noise_std_ = noise_std;
    noise_rms_ = noise_rms;
    prev_sample_ = baseline;
    confidence_ = 0.9f;
    last_calib_ms_ = now_ms;
    state_ = CalibState::IDLE;
}

// ============================================================
// Sample Processing
// ============================================================
void AutoCalibrator::onSample(float raw, uint32_t now_ms, bool event_active)
{
    if (state_ == CalibState::COLLECTING || state_ == CalibState::AUTO_RECALIBRATING)
    {
        // Accumulate calibration samples
        calib_sample_count_++;
        double delta = (double)raw - calib_sum_ / (double)calib_sample_count_;
        calib_sum_ += (double)raw;
        double delta2 = (double)raw - calib_sum_ / (double)calib_sample_count_;
        calib_m2_ += delta * delta2;

        if (calib_sample_count_ > 1)
        {
            double d = (double)raw - (double)calib_prev_val_;
            calib_diff_sq_sum_ += d * d;
        }
        calib_prev_val_ = raw;

        // Collect enough samples for calibration
        uint32_t needed = 640;
        if (calib_sample_count_ >= needed)
        {
            applyCalibrationResult();
        }
        return;
    }

    // Normal operation: update baselines during quiet periods
    if (!event_active && baseline_ > 0.0f)
    {
        bool quiet_ok = verifyQuietConditions();
        if (quiet_ok)
        {
            updateBaselines(raw);
            updateNoise(raw);
            updateDrift();
            updateConfidence(true);
        }
        else
        {
            updateConfidence(false);
        }
    }

    // Track quiet period
    if (!event_active)
    {
        if (quiet_start_ms_ == 0)
            quiet_start_ms_ = now_ms;
    }
    else
    {
        quiet_start_ms_ = 0;
    }

    // Check for auto-recalibration trigger
    if (!event_active && shouldRecalibrate() && isQuiet())
    {
        startAutoRecalibration();
    }

    prev_sample_ = raw;
}

// ============================================================
// Event Lifecycle
// ============================================================
void AutoCalibrator::onEventStart(uint32_t now_ms)
{
    // Stop any quiet period tracking
    quiet_start_ms_ = 0;
}

void AutoCalibrator::onEventEnd(uint32_t now_ms)
{
    last_event_end_ms_ = now_ms;
}

// ============================================================
// Manual Calibration
// ============================================================
void AutoCalibrator::startManualCalibration()
{
    state_ = CalibState::COLLECTING;
    calib_sample_count_ = 0;
    calib_sum_ = 0;
    calib_m2_ = 0;
    calib_diff_sq_sum_ = 0;
    calib_prev_val_ = 0;
}

// ============================================================
// Auto Recalibration
// ============================================================
void AutoCalibrator::startAutoRecalibration()
{
    if (state_ == CalibState::AUTO_RECALIBRATING)
        return;

    state_ = CalibState::AUTO_RECALIBRATING;
    calib_sample_count_ = 0;
    calib_sum_ = 0;
    calib_m2_ = 0;
    calib_diff_sq_sum_ = 0;
    calib_prev_val_ = 0;
}

// ============================================================
// State Queries
// ============================================================
bool AutoCalibrator::isQuiet() const
{
    if (quiet_start_ms_ == 0)
        return false;

    uint32_t now = millis();
    if (now < quiet_start_ms_)
        return false; // wrap

    return (now - quiet_start_ms_) >= cfg_.min_quiet_ms;
}

bool AutoCalibrator::shouldRecalibrate() const
{
    if (baseline_ <= 0.0f)
        return false;

    uint32_t now = millis();
    if (now < last_calib_ms_)
        return false;

    // Periodic recalibration
    if ((now - last_calib_ms_) >= cfg_.recalib_min_interval_ms)
        return true;

    // Drift-based recalibration
    if (drift_score_ > cfg_.drift_warn_threshold)
        return true;

    return false;
}

bool AutoCalibrator::isFault() const { return health_ == SensorHealth::FAULT; }
bool AutoCalibrator::isDegraded() const { return health_ == SensorHealth::DEGRADED; }
bool AutoCalibrator::isCalibrating() const
{
    return state_ == CalibState::COLLECTING || state_ == CalibState::AUTO_RECALIBRATING;
}

// ============================================================
// Accessors
// ============================================================
float AutoCalibrator::baseline() const { return baseline_; }
float AutoCalibrator::baselineFast() const { return baseline_fast_; }
float AutoCalibrator::baselineSlow() const { return baseline_slow_; }
float AutoCalibrator::noiseStd() const { return noise_std_; }
float AutoCalibrator::noiseRms() const { return noise_rms_; }
float AutoCalibrator::confidence() const { return confidence_; }
float AutoCalibrator::driftScore() const { return drift_score_; }
SensorHealth AutoCalibrator::health() const { return health_; }
CalibState AutoCalibrator::state() const { return state_; }

float AutoCalibrator::noisePercent() const
{
    return (baseline_ > 1e-6f) ? (noise_std_ / baseline_) : 0.0f;
}

// ============================================================
// Threshold Computation (with confidence penalty)
// ============================================================
float AutoCalibrator::enterThreshold(float enter_sigma, float min_enter, float max_enter) const
{
    if (baseline_ <= 0.0f || noise_std_ <= 0.0f)
        return min_enter;

    float noise_percent = noise_std_ / baseline_;
    float raw = enter_sigma * noise_percent;

    // Confidence penalty: low confidence -> higher thresholds -> fewer false detections
    float penalty = 1.0f + (1.0f - confidence_) * 0.5f;
    raw *= penalty;

    if (raw < min_enter) raw = min_enter;
    if (raw > max_enter) raw = max_enter;
    return raw;
}

float AutoCalibrator::absoluteMinDev(float abs_sigma, float min_dev, float max_dev) const
{
    if (baseline_ <= 0.0f || noise_std_ <= 0.0f)
        return min_dev;

    float noise_percent = noise_std_ / baseline_;
    float raw = abs_sigma * noise_percent;

    float penalty = 1.0f + (1.0f - confidence_) * 0.5f;
    raw *= penalty;

    if (raw < min_dev) raw = min_dev;
    if (raw > max_dev) raw = max_dev;
    return raw;
}

// ============================================================
// Internal: Update Baselines
// ============================================================
void AutoCalibrator::updateBaselines(float sample)
{
    if (baseline_ <= 0.0f)
    {
        // First sample: initialize all baselines
        baseline_fast_ = sample;
        baseline_slow_ = sample;
        baseline_ = sample;
        return;
    }

    baseline_fast_ = (1.0f - cfg_.baseline_alpha_fast) * baseline_fast_ + cfg_.baseline_alpha_fast * sample;
    baseline_slow_ = (1.0f - cfg_.baseline_alpha_slow) * baseline_slow_ + cfg_.baseline_alpha_slow * sample;
    baseline_ = (1.0f - cfg_.baseline_alpha_main) * baseline_ + cfg_.baseline_alpha_main * sample;
}

// ============================================================
// Internal: Update Noise
// ============================================================
void AutoCalibrator::updateNoise(float sample)
{
    if (baseline_ <= 0.0f)
        return;

    float dev = sample - baseline_;
    float abs_dev = fabsf(dev);

    // Exponential moving variance
    if (noise_std_ <= 0.0f)
    {
        noise_std_ = abs_dev;
    }
    else
    {
        float target = abs_dev;
        noise_std_ = noise_std_ * (1.0f - cfg_.noise_alpha) + target * cfg_.noise_alpha;
    }

    // RMS of differences
    if (prev_sample_ > 0.0f)
    {
        float diff = sample - prev_sample_;
        diff_sq_sum_ = diff_sq_sum_ * (1.0f - cfg_.noise_alpha) + diff * diff * cfg_.noise_alpha;
        noise_rms_ = sqrtf(diff_sq_sum_) * 0.70710678f;
    }
}

// ============================================================
// Internal: Update Drift
// ============================================================
void AutoCalibrator::updateDrift()
{
    if (noise_std_ < 1e-9f || baseline_ <= 0.0f)
    {
        drift_score_ = 0;
        return;
    }

    drift_score_ = fabsf(baseline_fast_ - baseline_slow_) / noise_std_;
}

// ============================================================
// Internal: Update Confidence
// ============================================================
void AutoCalibrator::updateConfidence(bool quiet_ok)
{
    if (baseline_ <= 0.0f)
    {
        confidence_ = 0;
        return;
    }

    if (quiet_ok && drift_score_ < cfg_.drift_warn_threshold)
    {
        // Healthy: confidence increases
        confidence_ += cfg_.confidence_boost_rate;
        if (confidence_ > 1.0f) confidence_ = 1.0f;
    }
    else if (drift_score_ > cfg_.drift_fault_threshold)
    {
        // Fault condition: confidence drops fast
        confidence_ -= cfg_.confidence_decay_rate * 5.0f;
        if (confidence_ < 0.0f) confidence_ = 0.0f;
    }
    else
    {
        // Degraded: slow confidence decay
        confidence_ -= cfg_.confidence_decay_rate;
        if (confidence_ < 0.0f) confidence_ = 0.0f;
    }
}

// ============================================================
// Internal: Apply Calibration Result
// ============================================================
void AutoCalibrator::applyCalibrationResult()
{
    if (calib_sample_count_ < 10)
    {
        state_ = CalibState::IDLE;
        return;
    }

    float new_baseline = (float)(calib_sum_ / (double)calib_sample_count_);

    float var_sample = 0.0f;
    if (calib_sample_count_ > 1)
    {
        var_sample = (float)(calib_m2_ / (double)(calib_sample_count_ - 1));
    }
    if (var_sample < 0.0f) var_sample = 0.0f;
    float new_noise_std = sqrtf(var_sample);

    float new_noise_rms = 0.0f;
    if (calib_sample_count_ > 1)
    {
        new_noise_rms = sqrtf((float)(calib_diff_sq_sum_ / (double)(calib_sample_count_ - 1)));
    }
    new_noise_rms *= 0.70710678f;
    if (new_noise_rms < 1e-6f)
        new_noise_rms = new_noise_std;

    // Apply results
    baseline_ = new_baseline;
    baseline_fast_ = new_baseline;
    baseline_slow_ = new_baseline;
    noise_std_ = new_noise_std;
    noise_rms_ = new_noise_rms;

    drift_score_ = 0;
    confidence_ = 0.9f;
    last_calib_ms_ = millis();

    state_ = CalibState::IDLE;
}

// ============================================================
// Internal: Verify Quiet Conditions
// ============================================================
bool AutoCalibrator::verifyQuietConditions() const
{
    if (baseline_ <= 0.0f)
        return false;

    // Check noise ratio
    float noise_ratio = noise_std_ / baseline_;
    if (noise_ratio > cfg_.quiet_max_std_ratio)
        return false;

    return true;
}

// ============================================================
// Internal: Check Fault
// ============================================================
void AutoCalibrator::checkFault()
{
    uint32_t now = millis();

    if (drift_score_ > cfg_.drift_fault_threshold)
    {
        if (health_ != SensorHealth::FAULT)
        {
            if (now - last_fault_ms_ > cfg_.fault_holdoff_ms)
            {
                health_ = SensorHealth::FAULT;
                last_fault_ms_ = now;
                confidence_ = 0.1f;
            }
        }
    }
    else if (drift_score_ > cfg_.drift_warn_threshold)
    {
        if (health_ == SensorHealth::HEALTHY)
        {
            if (now - last_degraded_ms_ > cfg_.degraded_holdoff_ms)
            {
                health_ = SensorHealth::DEGRADED;
                last_degraded_ms_ = now;
            }
        }
    }
    else
    {
        if (health_ != SensorHealth::HEALTHY)
        {
            health_ = SensorHealth::HEALTHY;
        }
    }
}

// ============================================================
// Build Status String
// ============================================================
void AutoCalibrator::buildStatusString(char *buf, size_t bufSize) const
{
    const char *healthStr = "HEALTHY";
    if (health_ == SensorHealth::DEGRADED) healthStr = "DEGRADED";
    else if (health_ == SensorHealth::FAULT) healthStr = "FAULT";

    const char *stateStr = "IDLE";
    if (state_ == CalibState::COLLECTING) stateStr = "COLLECTING";
    else if (state_ == CalibState::AUTO_RECALIBRATING) stateStr = "AUTO_RECAL";
    else if (state_ == CalibState::QUIET_MONITOR) stateStr = "QUIET_MON";

    snprintf(buf, bufSize,
             "health:%s|state:%s|baseline:%.1f|noise_std:%.2f|noise_rms:%.2f|noise_pct:%.6f|drift:%.2f|conf:%.2f",
             healthStr, stateStr, baseline_, noise_std_, noise_rms_, noisePercent(), drift_score_, confidence_);
}
