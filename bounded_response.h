// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>

class BoundedResponse : public Stream {
public:
    BoundedResponse(uint8_t *buffer, size_t capacity) : data(buffer), capacity(capacity) {}
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t *bytes, size_t length) override {
        if (length > capacity - used) return 0;
        memcpy(data + used, bytes, length);
        used += length;
        return length;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    uint8_t *data;
    size_t capacity, used = 0;
};
