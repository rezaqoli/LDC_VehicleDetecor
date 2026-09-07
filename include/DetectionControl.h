// ============================================================
// DetectionControl.h  —  Pause / resume the detection pipeline
// ============================================================
#pragma once
#include <Arduino.h>

void        detectionSetPaused(bool paused);
bool        detectionIsPaused();
const char *detectionStateName();   // "RUNNING" or "PAUSED"
