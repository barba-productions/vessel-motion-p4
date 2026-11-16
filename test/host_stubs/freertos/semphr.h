#pragma once
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline void xSemaphoreTake(SemaphoreHandle_t handle, int timeout) { (void)handle; (void)timeout; }
static inline void xSemaphoreGive(SemaphoreHandle_t handle) { (void)handle; }
