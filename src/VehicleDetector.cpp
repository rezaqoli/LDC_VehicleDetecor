// ============================================================
// VehicleDetector.cpp  —  v5.2 Implementation
// ============================================================
#include "VehicleDetector.h"
#include "MqttHandler.h" // Include the new header
#include "config.h"

// ============================================================
// Feature flag
// ============================================================
bool send_full_event_features = true;

// ============================================================
// Constructor
// ============================================================
VehicleDetector::VehicleDetector(const char *id, DetectorConfig cfg)
    : cfg_(cfg)
{
    strncpy(id_, id, sizeof(id_) - 1);
    id_[sizeof(id_) - 1] = '\0';
    reset_state(DetectorState::WARMUP);
    calibrator_.begin(millis());
}

// ============================================================
// Public Accessors
// ============================================================
void VehicleDetector::setConfig(const DetectorConfig &c)
{
    if (cfg_.entry_mode != c.entry_mode)
        reset_entry_tracking();
    cfg_ = c;
}
DetectorConfig VehicleDetector::getConfig() const { return cfg_; }
void VehicleDetector::setDualLoopMode(bool enabled) { dual_loop_mode_ = enabled; }
bool VehicleDetector::isDualLoopMode() const { return dual_loop_mode_; }

float VehicleDetector::baseline() const { return baseline_; }
const char *VehicleDetector::id() const { return id_; }
float VehicleDetector::noiseStd() const { return noise_std_; }
float VehicleDetector::noiseRms() const { return noise_rms_; }
float VehicleDetector::noisePercent() const
{
    return (baseline_ > 1e-6f) ? (noise_std_ / baseline_) : 0.0f;
}

// ============================================================
// Auto-Calibration Accessors
// ============================================================
AutoCalibrator &VehicleDetector::calibrator() { return calibrator_; }
const AutoCalibrator &VehicleDetector::calibrator() const { return calibrator_; }
float VehicleDetector::confidence() const { return calibrator_.confidence(); }
float VehicleDetector::driftScore() const { return calibrator_.driftScore(); }
SensorHealth VehicleDetector::health() const { return calibrator_.health(); }
void VehicleDetector::buildCalibStatus(char *buf, size_t bufSize) const { calibrator_.buildStatusString(buf, bufSize); }

// ============================================================
// Calibration
// ============================================================
void VehicleDetector::startCalibration()
{
    reset_state(DetectorState::CALIBRATING);
    calibrator_.startManualCalibration();
}

void VehicleDetector::finishCalibration()
{
    baseline_ = (float)calib_mean_;
    float var_sample = 0.0f;
    if (calib_cnt_ > 1)
    {
        var_sample = (float)(calib_m2_ / (double)(calib_cnt_ - 1));
    }
    if (var_sample < 0.0f)
        var_sample = 0.0f;
    noise_std_ = sqrtf(var_sample);

    float diff_rms = 0.0f;
    if (calib_cnt_ > 1)
    {
        diff_rms = sqrtf((float)(calib_diff_sq_sum_ / (double)(calib_cnt_ - 1)));
    }
    noise_rms_ = diff_rms * 0.70710678f;
    if (noise_rms_ < 1e-6f)
        noise_rms_ = noise_std_;

    // Seed the adaptive calibrator with the initial calibration. Calling
    // begin() here used to erase these values, leaving it unable to adapt.
    calibrator_.setCalibrationResult(baseline_, noise_std_, noise_rms_, millis());

    if (cfg_.auto_threshold)
    {
        float noise_percent = (baseline_ > 1e-6f) ? (noise_std_ / baseline_) : 0.0f;
        float new_abs_dev = cfg_.abs_sigma * noise_percent;
        float new_enter_thresh = cfg_.enter_sigma * noise_percent;

        cfg_.absolute_min_dev = clampf(new_abs_dev, cfg_.min_abs_dev, cfg_.max_abs_dev);
        cfg_.enter_thresh = clampf(new_enter_thresh, cfg_.min_enter_thresh, cfg_.max_enter_thresh);
        if (cfg_.enter_thresh < cfg_.absolute_min_dev)
            cfg_.enter_thresh = cfg_.absolute_min_dev;
    }

    state_ = DetectorState::IDLE;
    char msg[512];
    snprintf(msg, sizeof(msg),
             "[%s] Calibration done: baseline=%.6f, noise_std=%.6f, noise_rms=%.6f, noise_percent=%.6f, enter_thresh=%.6f, absolute_min_dev=%.6f",
             id_, baseline_, noise_std_, noise_rms_, noisePercent(), cfg_.enter_thresh, cfg_.absolute_min_dev);
    wsSend(msg);
    Serial.printf(
        "[%s] Baseline=%.1f std=%.2f rms=%.2f noise=%.6f -> enter=%.6f abs=%.6f\n",
        id_, baseline_, noise_std_, noise_rms_, noisePercent(), cfg_.enter_thresh, cfg_.absolute_min_dev);
}

// ============================================================
// Threshold Recalculation
// ============================================================
void VehicleDetector::recalcThresholds()
{
    if (cfg_.auto_threshold && baseline_ > 0.0f && noise_std_ > 0.0f)
    {
        float noise_percent = noise_std_ / baseline_;
        float new_abs_dev = cfg_.abs_sigma * noise_percent;
        float new_enter_thresh = cfg_.enter_sigma * noise_percent;

        cfg_.absolute_min_dev = clampf(new_abs_dev, cfg_.min_abs_dev, cfg_.max_abs_dev);
        cfg_.enter_thresh = clampf(new_enter_thresh, cfg_.min_enter_thresh, cfg_.max_enter_thresh);
        if (cfg_.enter_thresh < cfg_.absolute_min_dev)
            cfg_.enter_thresh = cfg_.absolute_min_dev;

        char msg[128];
        snprintf(msg, sizeof(msg),
                 "[%s] Thresholds updated: enter=%.6f, abs=%.6f (sigma=%.2f)",
                 id_, cfg_.enter_thresh, cfg_.absolute_min_dev, cfg_.enter_sigma);
        wsSend(msg);
    }
}

// ============================================================
// Internal Utilities
// ============================================================
float VehicleDetector::clampf(float v, float lo, float hi)
{
    if (lo > hi)
    {
        float t = lo;
        lo = hi;
        hi = t;
    }
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

float VehicleDetector::smoothInput(float val)
{
    if (fabsf(filtered_val_) < 1e-6f)
    {
        filtered_val_ = val;
    }
    else
    {
        filtered_val_ = filtered_val_ * (1.0f - cfg_.smoothing_alpha) + val * cfg_.smoothing_alpha;
    }
    return filtered_val_;
}

void VehicleDetector::update_baseline(float val)
{
    float raw_enter_window = cfg_.enter_thresh * baseline_;
    if (fabsf(val - baseline_) < (raw_enter_window * 0.5f))
    {
        baseline_ = baseline_ * (1.0f - cfg_.baseline_alpha) + val * cfg_.baseline_alpha;
    }
}

void VehicleDetector::reset_state(DetectorState s)
{
    state_ = s;
    calib_cnt_ = 0;
    calib_mean_ = 0.0;
    calib_m2_ = 0.0;
    prev_calib_val_ = 0.0f;
    calib_diff_sq_sum_ = 0.0;
    warmup_cnt_ = 0;
    signal_cnt_ = 0;
    exit_counter_ = 0;
    above_thresh_count_ = 0;
    sustained_dev_cnt_ = 0;
    ev_start_us_ = 0;
    ev_peak_us_ = 0;
    ev_peak_dev_ = 0;
    reset_entry_tracking();
}

void VehicleDetector::reset_entry_tracking()
{
    derivative_initialized_ = false;
    prev_filtered_us_ = 0;
    derivative_origin_dev_ = 0.0f;
    derivative_origin_us_ = 0;
    derivative_rise_samples_ = 0;
    derivative_candidate_ = false;
    derivative_candidate_floor_ = 0.0f;
    above_thresh_count_ = 0;
    sustained_dev_cnt_ = 0;
}

// ============================================================
// Feed (main state machine)
// ============================================================
bool VehicleDetector::feed(uint32_t raw, uint32_t ts_us, EventResult &result)
{
    float val = (float)raw;

    switch (state_)
    {
    case DetectorState::WARMUP:
    {
        if (++warmup_cnt_ >= cfg_.warmup_samples)
        {
            reset_state(DetectorState::CALIBRATING);
        }
        return false;
    }

    case DetectorState::CALIBRATING:
    {
        if (calib_cnt_ == 0)
        {
            calib_mean_ = val;
            calib_m2_ = 0.0;
            prev_calib_val_ = val;
            calib_diff_sq_sum_ = 0.0;
            calib_cnt_ = 1;
        }
        else
        {
            ++calib_cnt_;
            double delta = (double)val - calib_mean_;
            calib_mean_ += delta / (double)calib_cnt_;
            double delta2 = (double)val - calib_mean_;
            calib_m2_ += delta * delta2;

            double d = (double)val - (double)prev_calib_val_;
            calib_diff_sq_sum_ += d * d;
            prev_calib_val_ = val;
        }

        uint32_t needed = cfg_.calib_samples ? cfg_.calib_samples : 1;

        if (calib_cnt_ >= needed)
        {
            finishCalibration();
        }
        return false;
    }

    case DetectorState::IDLE:
    {
        update_baseline(val);
        float filtered = smoothInput(val);
        if (baseline_ <= 1e-6f)
            return false;
        float dev = (filtered - baseline_) / baseline_;

        bool enter_condition = false;
        const bool derivative_mode = (cfg_.entry_mode == EntryDetectionMode::DERIVATIVE);

        if (!derivative_mode)
        {
            // Preserve the original baseline-threshold detector.
            float dt_ms = (prev_filtered_us_ != 0 && ts_us > prev_filtered_us_)
                              ? (ts_us - prev_filtered_us_) / 1000.0f
                              : 5.0f;
            float slope = fabsf(filtered - prev_filtered_) / fmaxf(dt_ms, 0.001f);
            bool fast_entry = (dev >= cfg_.enter_thresh && dev >= cfg_.absolute_min_dev &&
                               slope >= cfg_.min_entry_slope);

            if (dev >= cfg_.absolute_min_dev && dev < cfg_.enter_thresh)
                sustained_dev_cnt_++;
            else
                sustained_dev_cnt_ = 0;

            bool slow_entry = (sustained_dev_cnt_ * dt_ms >= cfg_.min_slow_enter_ms) &&
                              (dev >= cfg_.absolute_min_dev);
            enter_condition = fast_entry || slow_entry;
        }
        else if (!derivative_initialized_)
        {
            // The first filtered sample only initializes the differentiator;
            // otherwise startup would look like a very large rising edge.
            derivative_initialized_ = true;
            derivative_origin_dev_ = dev;
            derivative_origin_us_ = ts_us;
            derivative_rise_samples_ = 1;
        }
        else
        {
            const float adaptive_rms = calibrator_.noiseRms() > 1e-6f
                                           ? calibrator_.noiseRms()
                                           : noise_rms_;
            // noise_rms is the underlying sample noise estimate. sqrt(2)
            // converts it to the standard deviation of adjacent differences.
            // One raw count is retained as a floor for a perfectly quiet,
            // quantized calibration signal.
            const float diff_noise_dev = fmaxf(1.41421356f * adaptive_rms / baseline_,
                                               1.0f / baseline_);
            const float delta_dev = (filtered - prev_filtered_) / baseline_;
            const uint32_t rise_ms = (ts_us - derivative_origin_us_) / 1000;

            if (delta_dev < -diff_noise_dev ||
                rise_ms > cfg_.derivative_slow_window_ms ||
                dev < derivative_origin_dev_)
            {
                derivative_origin_dev_ = dev;
                derivative_origin_us_ = ts_us;
                derivative_rise_samples_ = 1;
            }
            else
            {
                derivative_rise_samples_++;
            }

            const float net_rise = dev - derivative_origin_dev_;
            const float sharp_threshold = cfg_.derivative_sigma * diff_noise_dev;
            const float slow_threshold = cfg_.derivative_slow_sigma * diff_noise_dev *
                                         sqrtf((float)derivative_rise_samples_);
            const bool sharp_entry = delta_dev >= sharp_threshold;
            const bool slow_entry = rise_ms >= cfg_.min_slow_enter_ms &&
                                    net_rise >= slow_threshold;

            if (!derivative_candidate_ && (sharp_entry || slow_entry))
            {
                derivative_candidate_ = true;
                derivative_candidate_floor_ = derivative_origin_dev_;
                above_thresh_count_ = 1;
            }
            else if (derivative_candidate_)
            {
                // Confirm that the rise did not immediately collapse. The
                // initiating derivative need not repeat on a sharp step.
                if (dev + 2.0f * diff_noise_dev >= derivative_candidate_floor_)
                    above_thresh_count_++;
                else
                {
                    derivative_candidate_ = false;
                    above_thresh_count_ = 0;
                }
            }

            enter_condition = derivative_candidate_;
        }

        prev_filtered_ = filtered;
        prev_filtered_us_ = ts_us;

        if (enter_condition)
        {
            if (!derivative_mode)
                above_thresh_count_++;
            if (above_thresh_count_ >= cfg_.confirm_samples)
            {
                state_ = DetectorState::IN_EVENT;
                calibrator_.onEventStart(ts_us / 1000);
                ev_start_us_ = ts_us;
                ev_peak_us_ = ts_us;
                ev_peak_dev_ = dev;
                signal_cnt_ = 0;
                exit_counter_ = 0;
                above_thresh_count_ = 0;
                signal_[signal_cnt_++] = dev;
                sustained_dev_cnt_ = 0;
                derivative_candidate_ = false;
            }
        }
        else
        {
            above_thresh_count_ = 0;
        }
        // Update adaptive statistics after making the entry decision, so the
        // leading edge cannot raise its own derivative threshold. Once entry
        // is confirmed, mark this sample active to keep it out of quiet noise.
        calibrator_.onSample(val, ts_us / 1000,
                             state_ == DetectorState::IN_EVENT || derivative_candidate_);
        return false;
    }

    case DetectorState::IN_EVENT:
    {
        float filtered = smoothInput(val);
        if (baseline_ <= 1e-6f)
            return false;
        float dev = (filtered - baseline_) / baseline_;

        if (signal_cnt_ < MAX_SIGNAL)
        {
            signal_[signal_cnt_++] = dev;
        }

        if (dev > ev_peak_dev_)
        {
            ev_peak_dev_ = dev;
            ev_peak_us_ = ts_us;
        }

        uint32_t elapsed_ms = (ts_us - ev_start_us_) / 1000;
        const bool derivative_mode = (cfg_.entry_mode == EntryDetectionMode::DERIVATIVE);
        // Use the stable calibration deviation for exit. The adaptive
        // calibrator may have observed part of a slow ramp before entry was
        // confirmed, which must not be allowed to lift the exit floor.
        const float derivative_exit_floor = cfg_.abs_sigma * noise_std_ / baseline_;
        float exit_level = derivative_mode
                               ? fmaxf(ev_peak_dev_ * cfg_.exit_ratio, derivative_exit_floor)
                               : fmaxf(ev_peak_dev_ * cfg_.exit_ratio,
                                       cfg_.enter_thresh * cfg_.exit_hysteresis_ratio);
        bool exit_condition = (dev < exit_level);
        bool duration_ok = (elapsed_ms >= cfg_.min_event_ms);
        bool peak_ok = derivative_mode ||
                       (ev_peak_dev_ >= cfg_.enter_thresh && ev_peak_dev_ >= cfg_.absolute_min_dev);

        if (exit_condition)
        {
            exit_counter_++;
        }
        else
        {
            exit_counter_ = 0;
        }

        if (exit_counter_ >= cfg_.exit_hysteresis_cnt && duration_ok && peak_ok && signal_cnt_ >= cfg_.min_event_samples)
        {
            calibrator_.onEventEnd(ts_us / 1000);
            state_ = DetectorState::IDLE;
            extract_features(ts_us, result);
            classify(result);
            return true;
        }

        if (elapsed_ms > cfg_.max_event_ms)
        {
            Serial.printf("[%s] Event timeout\n", id_);
            reset_state(DetectorState::IDLE);
        }
        return false;
    }
    }
    return false;
}

// ============================================================
// Feature Extraction
// ============================================================
void VehicleDetector::extract_features(uint32_t end_us, EventResult &ev)
{
    int n = signal_cnt_;
    ev.start_us = ev_start_us_;
    ev.peak_us = ev_peak_us_;
    ev.end_us = end_us;
    ev.duration_ms = (end_us - ev_start_us_) / 1000.0f;
    ev.sample_count = (uint32_t)n;
    ev.baseline_at_event = baseline_;
    ev.spatial_speed_ms = 0.0f;
    strncpy(ev.channel_id, id_, sizeof(ev.channel_id) - 1);
    ev.channel_id[sizeof(ev.channel_id) - 1] = '\0';

    if (dual_loop_mode_)
    {
        int copy_count = (n < EventResult::MAX_EVENT_SIGNAL) ? n : EventResult::MAX_EVENT_SIGNAL;
        for (int i = 0; i < copy_count; i++)
        {
            ev.time_sig[i] = signal_[i];
        }
    }

    // --- Basic stats ---
    int peak_idx = 0;
    ev.num_peaks = 0;
    for (int i = 1; i < n; i++)
    {
        if (signal_[i] > signal_[peak_idx])
            peak_idx = i;
    }
    ev.peak_dev = ev_peak_dev_;
    ev.rise_ms = (ev_peak_us_ - ev_start_us_) / 1000.0f;
    ev.decay_ms = (end_us - ev_peak_us_) / 1000.0f;

    float sum = 0, sum_sq = 0;
    for (int i = 0; i < n; i++)
    {
        sum += signal_[i];
        sum_sq += signal_[i] * signal_[i];
    }
    ev.mean_dev = sum / n;
    ev.energy = sum_sq * 5.0f / 1000.0f;
    ev.area = sum * 5.0f / 1000.0f;

    float var = 0;
    for (int i = 0; i < n; i++)
    {
        float v = signal_[i] - ev.mean_dev;
        var += v * v;
    }
    ev.std_dev = sqrtf(var / n);
    if (ev.std_dev < 1e-6f)
        ev.std_dev = 1e-6f;

    float rms = sqrtf(sum_sq / n);
    ev.crest_factor = (rms > 1e-9f) ? (ev.peak_dev / rms) : 0.0f;

    // --- Skewness, Kurtosis, CoM ---
    float sk = 0;
    float mass_sum = 0.0f;
    float weight_sum = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float v = signal_[i] - ev.mean_dev;
        float z = v / ev.std_dev;
        sk += z * z * z;

        float w = signal_[i] > 0.0f ? signal_[i] : 0.0f;
        weight_sum += w;
        mass_sum += (float)i * w;
    }
    ev.skewness = sk / n;
    ev.kurtosis = 0.0f;
    ev.com_idx = (weight_sum > 0.0f) ? (mass_sum / weight_sum) : 0.0f;

    // --- Zero crossings (relative to mean) ---
    ev.zero_crossings = 0;
    for (int i = 1; i < n; i++)
    {
        if ((signal_[i - 1] - ev.mean_dev) * (signal_[i] - ev.mean_dev) < 0)
            ev.zero_crossings++;
    }

    // --- Front energy ratio ---
    float front_sum_sq = 0;
    int half_n = n / 2;
    for (int i = 0; i < half_n; i++)
        front_sum_sq += signal_[i] * signal_[i];
    ev.front_energy_ratio = (sum_sq > 1e-9f) ? (front_sum_sq / sum_sq) : 0.5f;

    // --- Peak detection & axle distance ---
    const float peak_threshold = ev.peak_dev * cfg_.peak_prominence_ratio;
    const float min_prominence = fmaxf(cfg_.peak_prominence_abs, peak_threshold);
    const float min_peak_height = fmaxf(cfg_.peak_prominence_abs, ev.peak_dev * 0.25f);
    int peak_indices[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int peak_count = 0;

    for (int i = 1; i < n - 1 && peak_count < 8; i++)
    {
        if (!(signal_[i] >= signal_[i - 1] && signal_[i] >= signal_[i + 1]))
            continue;

        if (signal_[i] < min_peak_height)
            continue;

        float left_min = signal_[i];
        for (int L = i - 1; L >= 0; --L)
        {
            if (signal_[L] < left_min)
                left_min = signal_[L];
        }

        float right_min = signal_[i];
        for (int R = i + 1; R < n; ++R)
        {
            if (signal_[R] < right_min)
                right_min = signal_[R];
        }

        float prominence = signal_[i] - fmaxf(left_min, right_min);
        if (prominence < min_prominence)
            continue;

        if (peak_count > 0)
        {
            const int min_spacing_samples = (int)ceilf(cfg_.min_axle_distance_ms / 5.0f);
            if ((i - peak_indices[peak_count - 1]) < min_spacing_samples)
                continue;
        }

        peak_indices[peak_count++] = i;
    }

    if (peak_count == 0 && ev.peak_dev >= cfg_.absolute_min_dev)
        peak_count = 1;

    ev.num_peaks = peak_count;

    if (peak_count >= 2)
    {
        float total_dist = 0;
        for (int i = 1; i < peak_count; i++)
            total_dist += (peak_indices[i] - peak_indices[i - 1]) * 5.0f;
        ev.peak_distance_ms = total_dist / (peak_count - 1);
    }
    else
    {
        ev.peak_distance_ms = 0.0f;
    }

    // --- Spectral Flatness & Dominant Freq (using DFT bin 1..10) ---
    ev.dft_re = 0.0f;
    ev.dft_im = 0.0f;
    ev.dominant_freq_hz = 0.0f;
    ev.spectral_flatness = 0.0f;

    if (n > 20)
    {
        float max_mag = 0.0f;
        int max_bin = 1;
        for (int k = 1; k <= 2 && k < n / 2; k++)
        {
            float re = 0, im = 0;
            float angle_step = -2.0f * PI * k / n;
            for (int i = 0; i < n; i++)
            {
                float angle = angle_step * i;
                re += signal_[i] * cosf(angle);
                im += signal_[i] * sinf(angle);
            }
            float mag = sqrtf(re * re + im * im);
            if (mag > max_mag)
            {
                max_mag = mag;
                max_bin = k;
                ev.dft_re = re;
                ev.dft_im = im;
            }
        }
        float fs = 200.0f;
        ev.dominant_freq_hz = max_bin * fs / n;

        float log_sum = 0;
        for (int i = 0; i < n; i++)
        {
            float a = fabsf(signal_[i]) + 1e-10f;
            log_sum += logf(a);
        }
        float geo_mean = expf(log_sum / n);
        float arith_mean = sum / n + 1e-10f;
        ev.spectral_flatness = geo_mean / arith_mean;
    }

    // --- Width at half max ---
    float half_peak = ev.peak_dev * 0.5f;
    int width_samples = 0;
    for (int i = 0; i < n; i++)
    {
        if (signal_[i] >= half_peak)
            width_samples++;
    }
    ev.width_half_max = width_samples;

    // --- Max slope ---
    float max_slope = 0;
    for (int i = 1; i < n; i++)
    {
        float sl = fabsf(signal_[i] - signal_[i - 1]) / 5.0f;
        if (sl > max_slope)
            max_slope = sl;
    }
    ev.max_slope = max_slope;

    // --- Anomaly score ---
    ev.anomaly_score = (int)((ev.peak_dev / fmaxf(cfg_.enter_thresh, 1e-6f)) * 20.0f);
    if (ev.anomaly_score > 100)
        ev.anomaly_score = 100;

    // --- Length estimation (single loop fallback) ---
    if (n > 1 && !dual_loop_mode_)
    {
        float speed_ms = cfg_.default_speed_kmh * 1000.0f / 3600.0f;
        ev.estimated_length_m = (ev.duration_ms / 1000.0f) * speed_ms;
        ev.estimated_length_m = fmaxf(ev.estimated_length_m, 0.5f);
        ev.spatial_speed_ms = speed_ms;
    }
}

// ============================================================
// Classification Engine v5.2
// ============================================================
void VehicleDetector::classify(EventResult &ev)
{
    float len = ev.estimated_length_m;
    int peaks = ev.num_peaks;
    float abs_skew = fabsf(ev.skewness);
    float crest = ev.crest_factor;
    float width_ratio = (ev.sample_count > 0) ? ((float)ev.width_half_max / (float)ev.sample_count) : 0.0f;
    float com_norm = (ev.sample_count > 1) ? (ev.com_idx / (float)(ev.sample_count - 1)) : 0.5f;
    float peak_to_thresh = ev.peak_dev / fmaxf(cfg_.enter_thresh, 1e-6f);

    // Score buckets
    int motor = 0, car = 0, pickup = 0, van = 0, bus = 0;
    int truck_s = 0, truck_2 = 0, truck_3 = 0, truck_4 = 0;


    // 3. AXLE SPACING (normalized to meters)
    if (ev.peak_distance_ms > 0)
    {
        float axle_spacing_meters = 0.0f;

        if (ev.spatial_speed_ms > 1.0f)
        {
            axle_spacing_meters = ev.peak_distance_ms * 0.001f * ev.spatial_speed_ms;
        }
        else
        {
            float assumed_speed_ms = cfg_.default_speed_kmh / 3.6f;
            axle_spacing_meters = ev.peak_distance_ms * 0.001f * assumed_speed_ms;
        }

        if (axle_spacing_meters < 2.5f)
        {
            car += 3;
            pickup += 2;
        }
        else if (axle_spacing_meters < 3.5f)
        {
            pickup += 3;
            van += 2;
            truck_s += 2;
        }
        else if (axle_spacing_meters < 5.0f)
        {
            truck_2 += 4;
            truck_s += 2;
            bus += 2;
        }
        else
        {
            truck_3 += 4;
            truck_4 += 5;
            bus += 3;
        }
    }

    // 4. CREST FACTOR
    if (crest > cfg_.classify_crest_spiky)
    {
        motor += 3;
        car += 3;
    }
    else if (crest > cfg_.classify_crest_broad)
    {
        car += 2;
        pickup += 2;
        van += 1;
    }
    else
    {
        bus += 3;
        truck_2 += 2;
        truck_3 += 2;
        truck_4 += 2;
        van += 1;
    }

    // 5. SKEWNESS & CoM (symmetry)
    if (abs_skew < cfg_.classify_skew_tol && com_norm > cfg_.classify_com_center_min && com_norm < cfg_.classify_com_center_max)
    {
        car += 2;
        van += 2;
        bus += 1;
    }
    else if (abs_skew > cfg_.classify_skew_high || com_norm < cfg_.classify_com_edge_min || com_norm > cfg_.classify_com_edge_max)
    {
        pickup += 2;
        truck_2 += 1;
        truck_3 += 1;
        truck_4 += 1;
    }

    // 6. RISE TIME
    if (ev.rise_ms < cfg_.classify_rise_short_ms)
    {
        motor += 3;
        car += 2;
    }
    else if (ev.rise_ms < cfg_.classify_rise_mid_ms)
    {
        car += 2;
        pickup += 2;
        van += 1;
    }
    else if (ev.rise_ms < cfg_.classify_rise_long_ms)
    {
        van += 1;
        bus += 2;
        truck_2 += 2;
    }
    else
    {
        bus += 3;
        truck_3 += 2;
        truck_4 += 2;
    }

    // 7. ENERGY LEVEL
    if (ev.energy < cfg_.classify_energy_low)
    {
        motor += 2;
        car += 1;
    }
    else if (ev.energy < cfg_.classify_energy_mid)
    {
        car += 2;
        pickup += 1;
        van += 1;
    }
    else if (ev.energy < cfg_.classify_energy_high)
    {
        pickup += 2;
        van += 2;
        truck_s += 1;
    }
    else
    {
        bus += 2;
        truck_2 += 2;
        truck_3 += 3;
        truck_4 += 3;
    }

    // 8. WIDTH RATIO
    if (width_ratio < cfg_.classify_width_medium)
    {
        motor += 2;
        car += 2;
    }
    else if (width_ratio < cfg_.classify_width_wide)
    {
        car += 2;
        pickup += 2;
        van += 2;
        truck_s += 1;
    }
    else
    {
        bus += 3;
        truck_2 += 2;
        truck_3 += 2;
        truck_4 += 2;
    }

    // 9. ZERO CROSSINGS
    if (ev.zero_crossings <= 1)
    {
        car += 2;
        motor += 1;
    }
    else if (ev.zero_crossings <= 3)
    {
        pickup += 2;
        van += 2;
        truck_s += 1;
    }
    else if (ev.zero_crossings <= 6)
    {
        truck_2 += 3;
        truck_s += 1;
        van += 1;
    }
    else
    {
        truck_3 += 3;
        truck_4 += 4;
        bus += 2;
    }

    // 10. FRONT ENERGY RATIO
    if (ev.front_energy_ratio < 0.4f || ev.front_energy_ratio > 0.6f)
    {
        truck_2 += 1;
        truck_3 += 1;
        truck_4 += 1;
        pickup += 1;
    }
    else
    {
        car += 1;
        van += 1;
        bus += 1;
    }


    // 2. NUM PEAKS (axle count proxy)
    if (peaks <= 1)
    {
        motor += 2;
        car += 2;
        van += 2;
        pickup  += 2;
        truck_s += 2;
        bus     +=2;
        truck_2 = truck_3 = truck_4 = 0;
    }
    else if (peaks == 2)
    {
        //car += 2;
        //pickup += 4;
        motor = car = pickup = van = 0;
        van += 3;
        truck_s += 2;
        truck_2 += 8;
        truck_3 += 5;
    }
    else if (peaks == 3)
    {
        motor = car = pickup = van = truck_s = 0;
        truck_2 += 5;
        truck_s += 3;
        van += 2;
        pickup += 1;
        truck_3 += 10;
        truck_4 += 8;
    }
    else if (peaks == 4)
    {
        motor = car = pickup = van = truck_s = 0;
        truck_3 += 16;
        truck_2 += 12;
        truck_4 += 20;
        bus += 6;
    }
    else
    {
        motor = car = pickup = van = truck_s = 0;
        truck_4 += 20;
        truck_3 += 20;
        bus += 6;
    }

        // 1. LENGTH CLASSIFICATION (heaviest weight)
    if (len > 0.0f)
    {
        if (len < cfg_.motor_max_len)
        {
            motor += 8;
            car += 1;
            pickup = van = 0;
            truck_s= 0;
            bus    = 0;
            truck_4= 0;
            truck_3= 0;
            truck_2= 0;

        }
        else if (len < cfg_.car_max_len)
        {
            car += 10;
            pickup += 2;
            motor += 1;

            van    = 0;
            truck_s= 0;
            bus    = 0;
            truck_4= 0;
            truck_3= 0;
            truck_2= 0;
        }
        else if (len < cfg_.pickup_max_len)
        {
            motor   = 0;
            bus    = 0;
            truck_4= 0;
            truck_3= 0;
            truck_2= 0;
            pickup += 10;
            car += 2;
            van += 1;
        }
        else if (len < cfg_.van_max_len)
        {
            motor   = 0;
            car     = 0;
            bus    = 0;
            truck_4= 0;
            truck_3= 0;
            truck_2= 0;
            van += 10;
            pickup += 2;
            bus += 1;
            truck_s += 5;
        }
        else if (len < cfg_.truck_s_max_len)
        {
            motor   = 0;
            car     = 0;
            truck_s += 10;
            van += 2;
            truck_2 += 8;
            bus += 8;
        }
        else if (len < cfg_.truck_2_max_len)
        {
            motor   = 0;
            car     = 0;
            pickup  = 0;
            truck_2 += 10;
            truck_s += 2;
            van     += 2;
            truck_3 += 8;
            bus += 8;
        }
        else if (len < cfg_.truck_3_max_len)
        {
            motor = car = pickup = truck_s  = 0;
            truck_3 += 10;
            truck_2 += 4;
            truck_4 += 5;
            bus += 8;
        }
        else
        {
            motor = car = pickup = van = truck_s = truck_2 = 0;
            truck_4 += 14;
            truck_3 += 2;
            bus += 4;
        }
    }

    // ========== Winner selection ==========
    const char *label = "Unknown";
    int best = motor;

    if (car > best)        { best = car;      label = "Car"; }
    if (pickup > best)     { best = pickup;   label = "Pickup"; }
    if (van > best)        { best = van;      label = "Van"; }
    if (bus > best)        { best = bus;      label = "Bus"; }
    if (truck_s > best)    { best = truck_s;  label = "TruckS"; }
    if (truck_2 > best)    { best = truck_2;  label = "Truck2"; }
    if (truck_3 > best)    { best = truck_3;  label = "Truck3"; }
    if (truck_4 > best)    { best = truck_4;  label = "Truck4+"; }

    if (!label)
        label = "Unknown";
    strncpy(ev.vehicle_class, label, sizeof(ev.vehicle_class) - 1);
    ev.vehicle_class[sizeof(ev.vehicle_class) - 1] = '\0';
}

void VehicleDetector::reclassify(EventResult &ev)
{
    classify(ev);
}

// ============================================================
// Utility Functions
// ============================================================

void send_full_features(const EventResult &ev, void (*wsCallback)(const char *))
{
    char msg[1024];
    int written = snprintf(msg, sizeof(msg),
                           "EVENT|%s|start_us:%lu|peak_us:%lu|end_us:%lu|dur:%.1f|peak:%.6f|mean:%.6f|std:%.6f|energy:%.6f|area:%.6f|rise:%.1f|decay:%.1f|skew:%.6f|kurt:%.6f|max_slope:%.6f|w50:%d|score:%d|class:%s|baseline:%.1f|spatial_speed:%.3f|samples:%lu|est_len:%.1f|num_peaks:%d|com_idx:%.2f|dft_re:%.6f|dft_im:%.6f|crest:%.6f|pk_dist:%.1f|zcr:%d|front_en:%.2f|flat:%.3f|dom_hz:%.1f",
                           ev.channel_id,
                           (unsigned long)ev.start_us,
                           (unsigned long)ev.peak_us,
                           (unsigned long)ev.end_us,
                           ev.duration_ms,
                           ev.peak_dev,
                           ev.mean_dev,
                           ev.std_dev,
                           ev.energy,
                           ev.area,
                           ev.rise_ms,
                           ev.decay_ms,
                           ev.skewness,
                           ev.kurtosis,
                           ev.max_slope,
                           ev.width_half_max,
                           ev.anomaly_score,
                           ev.vehicle_class,
                           ev.baseline_at_event,
                           ev.spatial_speed_ms,
                           (unsigned long)ev.sample_count,
                           ev.estimated_length_m,
                           ev.num_peaks,
                           ev.com_idx,
                           ev.dft_re,
                           ev.dft_im,
                           ev.crest_factor,
                           ev.peak_distance_ms,
                           ev.zero_crossings,
                           ev.front_energy_ratio,
                           ev.spectral_flatness,
                           ev.dominant_freq_hz,
                           "");
    if (written < 0)
        return;
    if (written >= (int)sizeof(msg))
        Serial.println("[WARN] Event message truncated");

    if (wsCallback)
        wsCallback(msg);
    #ifdef ENABLE_MQTT
        mqttPublishEvent(msg);
    #endif
    Serial.println(msg);
}

void reportEvent(const EventResult &ev, void (*wsCallback)(const char *))
{
    if (send_full_event_features)
    {
        send_full_features(ev, wsCallback);
        return;
    }

    char msg[256];
    snprintf(msg, sizeof(msg),
             "EVENT|%s|dur:%.1f|peak:%.6f|w50:%d|rise:%.1f|class:%s|score:%d|crest:%.6f|len:%.1f|peaks:%d|pkdist:%.1f",
             ev.channel_id, ev.duration_ms, ev.peak_dev, ev.width_half_max,
             ev.rise_ms, ev.vehicle_class, ev.anomaly_score, ev.crest_factor,
             ev.estimated_length_m, ev.num_peaks, ev.peak_distance_ms);
    if (wsCallback)
        wsCallback(msg);
    Serial.printf("[%s] %.1fms peak=%.6f w50=%d class=%s len=%.1fm peaks=%d\n",
                  ev.channel_id, ev.duration_ms, ev.peak_dev, ev.width_half_max,
                  ev.vehicle_class, ev.estimated_length_m, ev.num_peaks);

    #ifdef ENABLE_MQTT
        mqttPublishEvent(msg);
    #endif
}
