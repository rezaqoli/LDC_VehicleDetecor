#include "AutoCalibrator.h"
#include <cstring>

AutoCalibrator::AutoCalibrator()
    : cfg_(), baseline_fast_(0), baseline_slow_(0), baseline_(0), noise_variance_(0),
      noise_std_(0), noise_rms_(0), prev_sample_(0), diff_sq_sum_(0), drift_score_(0),
      confidence_(0), shadow_center_(0), shadow_noise_(0), stable_ms_(0), last_now_ms_(0),
      last_event_end_ms_(0), last_calib_ms_(0), degraded_since_ms_(0), fault_since_ms_(0), invalid_since_ms_(0), event_active_(false),
      state_(CalibState::IDLE), health_(SensorHealth::HEALTHY), last_update_(CalibrationUpdate::NONE),
      calib_sample_count_(0), calib_mean_(0), calib_m2_(0), calib_diff_sq_sum_(0),
      calib_prev_val_(0), block_start_ms_(0), block_count_(0), block_mean_(0), block_m2_(0),
      block_diff_sq_sum_(0), block_prev_(0), shadow_count_(0), shadow_head_(0)
{
    memset(block_means_, 0, sizeof(block_means_));
    memset(block_stds_, 0, sizeof(block_stds_));
    memset(block_rms_, 0, sizeof(block_rms_));
}

AutoCalibrator::AutoCalibrator(const AutoCalibConfig &cfg) : AutoCalibrator() { cfg_ = cfg; }
uint32_t AutoCalibrator::elapsed(uint32_t now, uint32_t then) { return now - then; }
void AutoCalibrator::setConfig(const AutoCalibConfig &cfg) { cfg_ = cfg; }
AutoCalibConfig AutoCalibrator::getConfig() const { return cfg_; }

void AutoCalibrator::begin(uint32_t now_ms)
{
    reset();
    last_now_ms_ = last_calib_ms_ = now_ms;
    resetShadow(now_ms);
}

void AutoCalibrator::reset()
{
    baseline_fast_ = baseline_slow_ = baseline_ = 0.0f;
    noise_variance_ = noise_std_ = noise_rms_ = prev_sample_ = diff_sq_sum_ = 0.0f;
    drift_score_ = confidence_ = shadow_center_ = shadow_noise_ = 0.0f;
    stable_ms_ = last_now_ms_ = last_event_end_ms_ = last_calib_ms_ = 0;
    degraded_since_ms_ = fault_since_ms_ = invalid_since_ms_ = 0;
    event_active_ = false;
    state_ = CalibState::IDLE;
    health_ = SensorHealth::HEALTHY;
    last_update_ = CalibrationUpdate::NONE;
    resetManualCollector();
    resetShadow(0);
}

void AutoCalibrator::setCalibrationResult(float baseline, float noise_std, float noise_rms, uint32_t now_ms)
{
    applyModel(baseline, noise_std, noise_rms, now_ms, CalibrationUpdate::MANUAL_APPLIED);
    state_ = CalibState::IDLE;
}

void AutoCalibrator::resetManualCollector()
{
    calib_sample_count_ = 0;
    calib_mean_ = calib_m2_ = calib_diff_sq_sum_ = 0.0;
    calib_prev_val_ = 0.0f;
}

void AutoCalibrator::resetShadow(uint32_t now_ms)
{
    block_start_ms_ = now_ms;
    block_count_ = 0;
    block_mean_ = block_m2_ = block_diff_sq_sum_ = 0.0;
    block_prev_ = 0.0f;
    shadow_count_ = shadow_head_ = 0;
    stable_ms_ = 0;
}

void AutoCalibrator::startManualCalibration()
{
    state_ = CalibState::COLLECTING;
    resetManualCollector();
}

void AutoCalibrator::startAutoRecalibration()
{
    resetShadow(last_now_ms_);
    state_ = CalibState::QUIET_MONITOR;
}

void AutoCalibrator::setAutoReanchorEnabled(bool enabled) { cfg_.auto_reanchor_enabled = enabled; }
bool AutoCalibrator::autoReanchorEnabled() const { return cfg_.auto_reanchor_enabled; }

void AutoCalibrator::collectManual(float sample)
{
    ++calib_sample_count_;
    const double delta = (double)sample - calib_mean_;
    calib_mean_ += delta / (double)calib_sample_count_;
    calib_m2_ += delta * ((double)sample - calib_mean_);
    if (calib_sample_count_ > 1)
    {
        const double d = (double)sample - calib_prev_val_;
        calib_diff_sq_sum_ += d * d;
    }
    calib_prev_val_ = sample;
}

bool AutoCalibrator::finishManual(uint32_t now_ms)
{
    if (calib_sample_count_ < cfg_.calibration_samples) return false;
    const float mean = (float)calib_mean_;
    const float variance = calib_sample_count_ > 1
                               ? (float)(calib_m2_ / (calib_sample_count_ - 1)) : 0.0f;
    const float stddev = sqrtf(fmaxf(variance, 0.0f));
    float rms = calib_sample_count_ > 1
                    ? sqrtf((float)(calib_diff_sq_sum_ / (calib_sample_count_ - 1))) * 0.70710678f
                    : stddev;
    if (rms < 1e-6f) rms = stddev;
    if (mean <= 0.0f || !isfinite(mean) || stddev / mean > cfg_.quiet_max_std_ratio)
    {
        last_update_ = CalibrationUpdate::REJECTED;
        resetManualCollector();
        return false;
    }
    applyModel(mean, stddev, rms, now_ms, CalibrationUpdate::MANUAL_APPLIED);
    state_ = CalibState::IDLE;
    return true;
}

void AutoCalibrator::collectShadow(float sample, uint32_t now_ms)
{
    if (block_count_ == 0)
    {
        block_start_ms_ = now_ms;
        block_mean_ = sample;
        block_m2_ = block_diff_sq_sum_ = 0.0;
        block_prev_ = sample;
        block_count_ = 1;
        return;
    }
    ++block_count_;
    const double delta = (double)sample - block_mean_;
    block_mean_ += delta / (double)block_count_;
    block_m2_ += delta * ((double)sample - block_mean_);
    const double d = (double)sample - block_prev_;
    block_diff_sq_sum_ += d * d;
    block_prev_ = sample;
}

float AutoCalibrator::median(float *values, uint8_t count)
{
    for (uint8_t i = 1; i < count; ++i)
    {
        const float v = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > v) { values[j + 1] = values[j]; --j; }
        values[j + 1] = v;
    }
    if (!count) return 0.0f;
    return (count & 1) ? values[count / 2]
                       : 0.5f * (values[count / 2 - 1] + values[count / 2]);
}

bool AutoCalibrator::evaluateShadow(float &center, float &stddev, float &rms, float &slope) const
{
    const uint8_t required = (uint8_t)fminf((float)MAX_SHADOW_BLOCKS,
        ceilf((float)cfg_.min_quiet_ms / fmaxf((float)cfg_.shadow_block_ms, 1.0f)));
    if (shadow_count_ < required || required < 2) return false;
    float means[MAX_SHADOW_BLOCKS], values[MAX_SHADOW_BLOCKS];
    for (uint8_t i = 0; i < required; ++i)
    {
        const uint8_t idx = (shadow_head_ + MAX_SHADOW_BLOCKS - required + i) % MAX_SHADOW_BLOCKS;
        means[i] = block_means_[idx];
        values[i] = block_stds_[idx];
    }
    center = median(means, required);
    stddev = median(values, required);
    for (uint8_t i = 0; i < required; ++i)
    {
        const uint8_t idx = (shadow_head_ + MAX_SHADOW_BLOCKS - required + i) % MAX_SHADOW_BLOCKS;
        values[i] = block_rms_[idx];
    }
    rms = median(values, required);
    const uint8_t first = (shadow_head_ + MAX_SHADOW_BLOCKS - required) % MAX_SHADOW_BLOCKS;
    const uint8_t last = (shadow_head_ + MAX_SHADOW_BLOCKS - 1) % MAX_SHADOW_BLOCKS;
    const float seconds = fmaxf((required - 1) * cfg_.shadow_block_ms / 1000.0f, 0.001f);
    slope = (block_means_[last] - block_means_[first]) / fmaxf(center, 1.0f) / seconds;
    for (uint8_t i = 0; i < required; ++i)
    {
        const uint8_t idx = (shadow_head_ + MAX_SHADOW_BLOCKS - required + i) % MAX_SHADOW_BLOCKS;
        values[i] = fabsf(block_means_[idx] - center);
    }
    const float spread = fmaxf(1.4826f * median(values, required), stddev) / fmaxf(center, 1.0f);
    return center > 0.0f && isfinite(center) && spread <= cfg_.quiet_max_std_ratio &&
           fabsf(slope) <= cfg_.quiet_max_slope;
}

CalibrationUpdate AutoCalibrator::finishShadowBlock(uint32_t now_ms)
{
    const bool manual_pending = state_ == CalibState::COLLECTING;
    if (block_count_ < 2) { resetShadow(now_ms); return CalibrationUpdate::REJECTED; }
    const float variance = (float)(block_m2_ / (block_count_ - 1));
    block_means_[shadow_head_] = (float)block_mean_;
    block_stds_[shadow_head_] = sqrtf(fmaxf(variance, 0.0f));
    block_rms_[shadow_head_] = sqrtf((float)(block_diff_sq_sum_ / (block_count_ - 1))) * 0.70710678f;
    shadow_head_ = (shadow_head_ + 1) % MAX_SHADOW_BLOCKS;
    if (shadow_count_ < MAX_SHADOW_BLOCKS) ++shadow_count_;
    block_start_ms_ = now_ms;
    block_count_ = 0;
    block_mean_ = block_m2_ = block_diff_sq_sum_ = 0.0;

    float center = 0, candidate_std = 0, candidate_rms = 0, slope = 0;
    if (!evaluateShadow(center, candidate_std, candidate_rms, slope))
    {
        stable_ms_ = 0;
        if (!manual_pending) state_ = CalibState::QUIET_MONITOR;
        return CalibrationUpdate::NONE;
    }
    shadow_center_ = center;
    shadow_noise_ = candidate_std;
    stable_ms_ = event_active_ ? 0 : cfg_.min_quiet_ms;
    if (!event_active_ && !manual_pending) state_ = CalibState::QUIET_MONITOR;
    if (event_active_ || manual_pending || baseline_ <= 0.0f || !cfg_.auto_reanchor_enabled)
        return CalibrationUpdate::NONE;
    const float material = fmaxf(cfg_.min_reanchor_ratio * baseline_,
                                 cfg_.drift_warn_threshold * fmaxf(noise_std_, 1.0f));
    if (fabsf(center - baseline_) < material) return CalibrationUpdate::NONE;
    applyModel(center, candidate_std, candidate_rms, now_ms, CalibrationUpdate::REANCHORED);
    resetShadow(now_ms);
    return CalibrationUpdate::REANCHORED;
}

void AutoCalibrator::applyModel(float baseline, float noise_std, float noise_rms,
                                uint32_t now_ms, CalibrationUpdate update)
{
    baseline_ = baseline_fast_ = baseline_slow_ = baseline;
    noise_std_ = fmaxf(noise_std, 0.0f);
    noise_variance_ = noise_std_ * noise_std_;
    noise_rms_ = fmaxf(noise_rms, 0.0f);
    diff_sq_sum_ = 2.0f * noise_rms_ * noise_rms_;
    prev_sample_ = baseline;
    drift_score_ = 0.0f;
    confidence_ = 0.9f;
    health_ = SensorHealth::HEALTHY;
    degraded_since_ms_ = fault_since_ms_ = invalid_since_ms_ = 0;
    last_calib_ms_ = now_ms;
    last_update_ = update;
}

void AutoCalibrator::updateActiveModel(float sample)
{
    const float sigma_floor = fmaxf(noise_std_, baseline_ * 0.00001f);
    const float innovation = sample - baseline_;
    if (fabsf(innovation) > cfg_.outlier_sigma_limit * sigma_floor) return;
    baseline_fast_ += cfg_.baseline_alpha_fast * (sample - baseline_fast_);
    baseline_slow_ += cfg_.baseline_alpha_slow * (sample - baseline_slow_);
    baseline_ += cfg_.baseline_alpha_main * innovation;
    const float residual = sample - baseline_;
    noise_variance_ = (1.0f - cfg_.noise_alpha) * noise_variance_ + cfg_.noise_alpha * residual * residual;
    noise_std_ = sqrtf(fmaxf(noise_variance_, 0.0f));
    if (prev_sample_ > 0.0f)
    {
        const float d = sample - prev_sample_;
        diff_sq_sum_ = (1.0f - cfg_.noise_alpha) * diff_sq_sum_ + cfg_.noise_alpha * d * d;
        noise_rms_ = sqrtf(fmaxf(diff_sq_sum_, 0.0f)) * 0.70710678f;
    }
    prev_sample_ = sample;
    updateDrift();
}

void AutoCalibrator::updateDrift()
{
    drift_score_ = fabsf(baseline_fast_ - baseline_slow_) /
                   fmaxf(noise_std_, baseline_ * 0.00001f);
}

void AutoCalibrator::updateConfidence(bool accepted)
{
    confidence_ += accepted ? cfg_.confidence_boost_rate : -cfg_.confidence_decay_rate;
    if (drift_score_ > cfg_.drift_fault_threshold) confidence_ -= 4.0f * cfg_.confidence_decay_rate;
    if (confidence_ < 0.0f) confidence_ = 0.0f;
    if (confidence_ > 1.0f) confidence_ = 1.0f;
}

void AutoCalibrator::checkFault(uint32_t now_ms)
{
    if (drift_score_ > cfg_.drift_fault_threshold)
    {
        degraded_since_ms_ = 0;
        if (!fault_since_ms_) fault_since_ms_ = now_ms;
        if (elapsed(now_ms, fault_since_ms_) >= cfg_.fault_holdoff_ms) health_ = SensorHealth::FAULT;
    }
    else if (drift_score_ > cfg_.drift_warn_threshold)
    {
        fault_since_ms_ = 0;
        if (!degraded_since_ms_) degraded_since_ms_ = now_ms;
        if (elapsed(now_ms, degraded_since_ms_) >= cfg_.degraded_holdoff_ms) health_ = SensorHealth::DEGRADED;
    }
    else
    {
        degraded_since_ms_ = fault_since_ms_ = 0;
        health_ = SensorHealth::HEALTHY;
    }
}

CalibrationUpdate AutoCalibrator::onSample(float sample, uint32_t now_ms, bool activity_suspected)
{
    last_now_ms_ = now_ms;
    if (!isfinite(sample) || sample <= 0.0f) { onInvalidSample(now_ms); return CalibrationUpdate::REJECTED; }
    invalid_since_ms_ = 0;
    const bool hold_complete = !last_event_end_ms_ ||
                               elapsed(now_ms, last_event_end_ms_) >= cfg_.post_event_hold_ms;
    bool manual_rejected = false;
    if (state_ == CalibState::COLLECTING)
    {
        if (activity_suspected || !hold_complete)
        {
            // Manual calibration stays pending, but any partial event-tainted
            // window is discarded.
            if (calib_sample_count_) resetManualCollector();
        }
        else
        {
            collectManual(sample);
            if (calib_sample_count_ >= cfg_.calibration_samples)
            {
                if (finishManual(now_ms)) return CalibrationUpdate::MANUAL_APPLIED;
                manual_rejected = true;
            }
        }
    }
    collectShadow(sample, now_ms);
    CalibrationUpdate shadow_update = CalibrationUpdate::NONE;
    if (block_count_ > 1 && elapsed(now_ms, block_start_ms_) >= cfg_.shadow_block_ms)
        shadow_update = finishShadowBlock(now_ms);
    bool tracked = false;
    if (baseline_ > 0.0f && !activity_suspected && hold_complete)
    {
        const float before = baseline_;
        updateActiveModel(sample);
        tracked = baseline_ != before;
    }
    updateConfidence(tracked || shadow_update == CalibrationUpdate::REANCHORED);
    checkFault(now_ms);
    if (shadow_update != CalibrationUpdate::NONE) return shadow_update;
    if (manual_rejected) return CalibrationUpdate::REJECTED;
    return tracked ? CalibrationUpdate::TRACKED : CalibrationUpdate::NONE;
}

void AutoCalibrator::onInvalidSample(uint32_t now_ms)
{
    last_now_ms_ = now_ms;
    resetShadow(now_ms);
    updateConfidence(false);
    if (!invalid_since_ms_) invalid_since_ms_ = now_ms;
    if (elapsed(now_ms, invalid_since_ms_) >= cfg_.fault_holdoff_ms)
        health_ = SensorHealth::FAULT;
}
void AutoCalibrator::onEventStart(uint32_t now_ms)
{
    last_now_ms_ = now_ms;
    event_active_ = true;
    resetShadow(now_ms);
}
void AutoCalibrator::onEventEnd(uint32_t now_ms)
{
    last_now_ms_ = last_event_end_ms_ = now_ms;
    event_active_ = false;
    // Require a fresh post-event stable window; event plateaus cannot qualify.
    resetShadow(now_ms);
}
bool AutoCalibrator::isQuiet() const { return stable_ms_ >= cfg_.min_quiet_ms; }
bool AutoCalibrator::shouldRecalibrate() const
{
    if (!cfg_.auto_reanchor_enabled || baseline_ <= 0.0f || !isQuiet()) return false;
    return fabsf(shadow_center_ - baseline_) >=
           fmaxf(cfg_.min_reanchor_ratio * baseline_, cfg_.drift_warn_threshold * fmaxf(noise_std_, 1.0f));
}
bool AutoCalibrator::isFault() const { return health_ == SensorHealth::FAULT; }
bool AutoCalibrator::isDegraded() const { return health_ == SensorHealth::DEGRADED; }
bool AutoCalibrator::isCalibrating() const { return state_ == CalibState::COLLECTING; }
float AutoCalibrator::baseline() const { return baseline_; }
float AutoCalibrator::baselineFast() const { return baseline_fast_; }
float AutoCalibrator::baselineSlow() const { return baseline_slow_; }
float AutoCalibrator::noiseStd() const { return noise_std_; }
float AutoCalibrator::noiseRms() const { return noise_rms_; }
float AutoCalibrator::noisePercent() const { return baseline_ > 1e-6f ? noise_std_ / baseline_ : 0.0f; }
float AutoCalibrator::confidence() const { return confidence_; }
float AutoCalibrator::driftScore() const { return drift_score_; }
float AutoCalibrator::shadowCenter() const { return shadow_center_; }
float AutoCalibrator::shadowOffset() const { return baseline_ > 0.0f ? (shadow_center_ - baseline_) / baseline_ : 0.0f; }
uint32_t AutoCalibrator::stableDurationMs() const { return stable_ms_; }
SensorHealth AutoCalibrator::health() const { return health_; }
CalibState AutoCalibrator::state() const { return state_; }

CalibrationSnapshot AutoCalibrator::snapshot() const
{
    CalibrationSnapshot s;
    s.baseline = baseline_; s.noise_std = noise_std_; s.noise_rms = noise_rms_;
    s.confidence = confidence_; s.drift_score = drift_score_; s.shadow_center = shadow_center_;
    s.shadow_offset = shadowOffset(); s.stable_ms = stable_ms_; s.health = health_;
    s.state = state_; s.valid = baseline_ > 0.0f; s.auto_reanchor_enabled = cfg_.auto_reanchor_enabled;
    return s;
}

float AutoCalibrator::enterThreshold(float sigma, float minimum, float maximum) const
{
    float v = baseline_ > 0.0f ? sigma * noise_std_ / baseline_ : minimum;
    return fminf(fmaxf(v, minimum), maximum);
}
float AutoCalibrator::absoluteMinDev(float sigma, float minimum, float maximum) const
{
    float v = baseline_ > 0.0f ? sigma * noise_std_ / baseline_ : minimum;
    return fminf(fmaxf(v, minimum), maximum);
}

void AutoCalibrator::buildStatusString(char *buf, size_t size) const
{
    const char *health = health_ == SensorHealth::FAULT ? "FAULT" :
                         health_ == SensorHealth::DEGRADED ? "DEGRADED" : "HEALTHY";
    const char *state = state_ == CalibState::COLLECTING ? "COLLECTING" :
                        state_ == CalibState::QUIET_MONITOR ? "MONITOR" : "IDLE";
    snprintf(buf, size, "health:%s,state:%s,baseline:%.1f,noise:%.2f,drift:%.2f,conf:%.2f",
             health, state, baseline_, noise_std_, drift_score_, confidence_);
}

void AutoCalibrator::buildDetailedStatusString(char *buf, size_t size) const
{
    const char *health = health_ == SensorHealth::FAULT ? "FAULT" :
                         health_ == SensorHealth::DEGRADED ? "DEGRADED" : "HEALTHY";
    snprintf(buf, size,
             "health:%s|baseline:%.3f|noise_std:%.3f|noise_rms:%.3f|noise_pct:%.7f|drift:%.3f|conf:%.3f|shadow:%.3f|shadow_offset:%.7f|stable_ms:%lu|auto:%u|last:%u",
             health, baseline_, noise_std_, noise_rms_, noisePercent(), drift_score_, confidence_,
             shadow_center_, shadowOffset(), (unsigned long)stable_ms_,
             cfg_.auto_reanchor_enabled ? 1U : 0U, (unsigned)last_update_);
}
