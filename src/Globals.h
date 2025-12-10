// Globals.h
#pragma once
#include "nexus/Nexus.h"
#include "RTAPI/RTAPI.h"
#include <string>
#include "imgui/imgui.h"

// API & RTAPI Base
extern AddonAPI* APIDefs;
extern RealTimeData* RTAPIData;

// Violazioni
extern std::string LastViolationTitle;
extern std::string LastViolationDesc;
extern std::string LastViolationCode;

enum class ViolationType {
    None,
    CharacterNotFound,
    GenericViolation
};

extern ViolationType LastViolationType;

// UI Stato personaggio
extern std::string CharacterStatus;
extern ImVec4 CharacterColor;

// Ultima risposta server 
extern std::string LastServerResponse;

// HP50 Status
extern bool PlayerBelow50HP;
extern uint64_t PlayerBelow50HP_Time;
extern bool LoginDeadCheckPending;
extern uint64_t LoginTimestamp;