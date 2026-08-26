// SignalProcessing.h — v1.0
// Cross-Correlation + Spatial Resampling + DFT Fractional Delay
#pragma once
#ifndef SIGNAL_PROCESSING_H
#define SIGNAL_PROCESSING_H
#include <Arduino.h>
#include <math.h>

class SignalProcessing {
public:
    // ========== Cross-Correlation (Integer Delay) ==========
    static int crossCorrelation(const float* sig1, int n1, 
                                 const float* sig2, int n2, 
                                 float* max_corr = nullptr) {
        int max_lag = n2 - 1;
        int min_lag = -(n1 - 1);
        float best_corr = -1e9f;
        int best_lag = 0;
        
        for (int lag = min_lag; lag <= max_lag; lag++) {
            float corr = 0;
            int count = 0;
            
            for (int i = 0; i < n1; i++) {
                int j = i - lag;
                if (j >= 0 && j < n2) {
                    corr += sig1[i] * sig2[j];
                    count++;
                }
            }
            
            if (count > 0) {
                corr /= count;  // نرمال‌سازی
                if (corr > best_corr) {
                    best_corr = corr;
                    best_lag = lag;
                }
            }
        }
        
        if (max_corr) *max_corr = best_corr;
        return best_lag;
    }
    
    // ========== DFT Fractional Delay (Sub-Sample Precision) ==========
    static float fractionalDelay(const float* sig1, int n1, 
                                  const float* sig2, int n2) {
        // محاسبه DFT تک‌بین (k=1) برای هر دو سیگنال
        float real1 = 0, imag1 = 0;
        float real2 = 0, imag2 = 0;
        
        for (int n = 0; n < n1; n++) {
            float angle = 2.0f * M_PI * n / n1;
            real1 += sig1[n] * cosf(angle);
            imag1 -= sig1[n] * sinf(angle);
        }
        
        for (int n = 0; n < n2; n++) {
            float angle = 2.0f * M_PI * n / n2;
            real2 += sig2[n] * cosf(angle);
            imag2 -= sig2[n] * sinf(angle);
        }
        
        // Cross-spectral density: P1 * conj(P2)
        float cross_real = real1 * real2 + imag1 * imag2;
        float cross_imag = imag1 * real2 - real1 * imag2;
        
        // Phase angle
        float phase = atan2f(cross_imag, cross_real);
        
        // Fractional delay: -phase / (2π * k/N)
        float frac_delay = -phase / (2.0f * M_PI / n1);
        
        return frac_delay;
    }
    
    // ========== Estimate Time Delay (Combined) ==========
    // Takes raw sample pointers directly (e.g. from VehicleDetector::signal()),
    // so no separate WaveformBuffer copy of the event is required.
    static float estimateDelay(const float* sig1, int n1,
                                const float* sig2, int n2,
                                float sampling_ms = 1.0f) {
        if (n1 < 10 || n2 < 10) return -1;
        
        // Integer delay via cross-correlation
        int int_delay = crossCorrelation(sig1, n1, sig2, n2);
        
        // Fractional delay via DFT
        float frac_delay = fractionalDelay(sig1, n1, sig2, n2);
        
        // Total delay in samples
        float total_delay = int_delay + frac_delay;
        
        // Convert to milliseconds
        return total_delay * sampling_ms;
    }

    static float phaseShiftDelayMs(float dft_re1, float dft_im1,
                                   float dft_re2, float dft_im2,
                                   int n_samples, float sampling_ms) {
        if (n_samples < 2) return 0.0f;

        float cross_real = dft_re1 * dft_re2 + dft_im1 * dft_im2;
        float cross_imag = dft_im1 * dft_re2 - dft_re1 * dft_im2;
        float phase = atan2f(cross_imag, cross_real);

        float delay_samples = -phase / (2.0f * M_PI / (float)n_samples);
        return delay_samples * sampling_ms;
    }
    
    // ========== Spatial Resampling (Time → Distance Domain) ==========
    static int spatialResample(const float* time_signal, int n_samples,
                                float speed_ms, float sampling_ms,
                                float* spatial_signal, int n_output) {
        if (n_samples < 2 || speed_ms <= 0) return 0;
        
        // محاسبه طول کل خودرو (متر)
        float total_time_ms = n_samples * sampling_ms;
        float total_length_m = speed_ms * (total_time_ms / 1000.0f);
        
        if (total_length_m < 0.1f) return 0;  // خیلی کوتاه
        
        // باز-نمونه‌برداری به n_output نقطه در حوزه فضا
        for (int k = 0; k < n_output; k++) {
            // موقعیت فضایی هدف (متر)
            float x_target = (k / (float)(n_output - 1)) * total_length_m;
            
            // تبدیل به ایندکس زمانی
            float t_target_ms = (x_target / speed_ms) * 1000.0f;
            float idx_float = t_target_ms / sampling_ms;
            
            // درون‌یابی خطی
            int idx_low = (int)floorf(idx_float);
            int idx_high = idx_low + 1;
            
            if (idx_low < 0) idx_low = 0;
            if (idx_high >= n_samples) idx_high = n_samples - 1;
            
            float frac = idx_float - idx_low;
            spatial_signal[k] = time_signal[idx_low] * (1.0f - frac) + 
                                time_signal[idx_high] * frac;
        }
        
        return n_output;
    }
    
    // ========== Normalize to Zero Mean, Unit Variance ==========
    static void normalize(float* signal, int n) {
        if (n < 2) return;
        
        float mean = 0;
        for (int i = 0; i < n; i++) mean += signal[i];
        mean /= n;
        
        float var = 0;
        for (int i = 0; i < n; i++) {
            float d = signal[i] - mean;
            var += d * d;
        }
        var /= n;
        float std = sqrtf(var);
        
        if (std < 1e-6f) std = 1e-6f;
        
        for (int i = 0; i < n; i++) {
            signal[i] = (signal[i] - mean) / std;
        }
    }
};

#endif // SIGNAL_PROCESSING_H