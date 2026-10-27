#pragma once

#include <stdint.h>

#if EPDPARTIAL
void SetBatteryMeasurementPower(bool on);
void PowerOffPartialDisplay();
bool UpdatePartialDisplay(const uint8_t *framebuffer, bool morningRefresh);
#endif
