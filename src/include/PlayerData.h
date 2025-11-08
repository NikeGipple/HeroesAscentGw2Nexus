// PlayerData.h
#pragma once
#include <cstdint>
#include <string>
#include "RTAPI/RTAPI.h"
#include "nexus/Nexus.h"

// Dati RTAPI condivisi
extern RealTimeData* RTAPIData;

// Snapshot per rilevare cambi
struct PlayerSnapshot {
    std::string Name;
    uint32_t MapID = 0;
    uint32_t CharacterState = 0;
    float Position[3] = { 0,0,0 };
};
extern PlayerSnapshot LastSnapshot;

// Dati base del player mostrati in UI
extern uint32_t PlayerLevel;
extern std::string PlayerName;

void InitPlayerData(AddonAPI* api);
bool HasChanged(const RealTimeData* data);
