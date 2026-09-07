// FreeRTOS shim for native tests
#pragma once
#include <cstdint>

typedef void * QueueHandle_t;
typedef void * SemaphoreHandle_t;
typedef void * TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

#define pdPASS            1
#define pdFAIL            0
#define pdTRUE            1
#define pdFALSE           0
#define portMAX_DELAY     0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) (ms)

inline void vTaskDelay(uint32_t) {}
inline void vTaskDelayUntil(uint32_t *, uint32_t) {}
inline void vTaskDelete(void *) {}
inline uint32_t xTaskGetTickCount() { return 0; }
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t,
                                          void *, uint32_t, TaskHandle_t *, BaseType_t)
{ return pdPASS; }
inline QueueHandle_t xQueueCreate(uint32_t, uint32_t) {
  static int dummy;
  return &dummy;
}
inline QueueHandle_t xQueueCreateStatic(uint32_t, uint32_t, void *buf, void *) {
  (void)buf; static int dummy; return &dummy;
}
inline BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t) { return pdPASS; }
inline BaseType_t xQueueGenericSend(QueueHandle_t, const void *, TickType_t, BaseType_t) { return pdPASS; }
inline BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t) { return pdPASS; }
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int dummy; return &dummy; }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { static int dummy; return &dummy; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdPASS; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdPASS; }

struct StaticQueue_t { int dummy; };
