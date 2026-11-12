// SPDX-License-Identifier: GPL-3.0-only
#include "battery_store.h"
#include <SPIFFS.h>

namespace {
constexpr uint32_t Magic = 0x42545243;
const char *const Paths[] = {"/battery-trace0.bin", "/battery-trace1.bin"};
struct Header { uint32_t magic, generation, checksum; };
BatteryHistory scratch;

uint32_t Checksum(const BatteryHistory &history)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(&history);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(history); ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

bool ReadSlot(int slot, BatteryHistory &history, uint32_t &generation)
{
    File file = SPIFFS.open(Paths[slot], "r");
    Header header{};
    bool ok = file && file.size() == sizeof(header) + sizeof(history) &&
              file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
              file.read(reinterpret_cast<uint8_t *>(&history), sizeof(history)) == sizeof(history);
    file.close();
    if (!ok || header.magic != Magic || header.checksum != Checksum(history) || history.version != 1 ||
        history.floor < 3000 || history.floor > 4000 || history.count > BatteryHistoryCapacity ||
        history.learnedCount > BatteryHistoryCapacity) return false;
    generation = header.generation;
    return true;
}
}

bool LoadBatteryHistory(BatteryHistory &history, BatteryStore &store)
{
    store = BatteryStore{};
    for (int slot = 0; slot < 2; ++slot)
    {
        uint32_t generation;
        if (ReadSlot(slot, scratch, generation) &&
            (store.slot < 0 || int32_t(generation - store.generation) > 0))
        {
            history = scratch;
            store = {generation, slot};
        }
    }
    return store.slot >= 0;
}

bool SaveBatteryHistory(const BatteryHistory &history, BatteryStore &store)
{
    int next = store.slot == 0 ? 1 : 0;
    Header header{Magic, store.generation + 1, Checksum(history)};
    File file = SPIFFS.open(Paths[next], "w");
    bool ok = file && file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
              file.write(reinterpret_cast<const uint8_t *>(&history), sizeof(history)) == sizeof(history);
    if (file) file.flush();
    file.close();
    uint32_t generation;
    ok = ok && ReadSlot(next, scratch, generation) && generation == header.generation &&
         Checksum(scratch) == header.checksum;
    if (ok) store = {generation, next};
    return ok;
}
