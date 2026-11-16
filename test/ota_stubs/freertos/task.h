#pragma once
#define pdPASS 1
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle);
void vTaskDelete(void *handle);
