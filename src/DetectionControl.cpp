// ============================================================
// DetectionControl.cpp
// ============================================================
#include "DetectionControl.h"
#include "Globals.h"

volatile bool g_detectionPaused = false;

void detectionSetPaused(bool paused)
{
  g_detectionPaused = paused;
}

bool detectionIsPaused()
{
  return g_detectionPaused;
}

const char *detectionStateName()
{
  return g_detectionPaused ? "PAUSED" : "RUNNING";
}
