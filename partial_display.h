// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <stdint.h>

#if EPDPARTIAL
void SetBatteryMeasurementPower(bool on);
void PowerOffPartialDisplay();
bool UpdatePartialDisplay(const uint8_t *framebuffer, bool morningRefresh);
bool SaveWeatherImage();
bool LoadWeatherImage(uint8_t *framebuffer);
bool LoadDisplayedImage(uint8_t *framebuffer);
#endif
