// Globals.h
#pragma once
#include "nexus/Nexus.h"
#include "RTAPI/RTAPI.h"
#include <string>
#include "imgui/imgui.h"

extern AddonAPI* APIDefs;
extern RealTimeData* RTAPIData;

extern std::string LastViolationTitle;
extern std::string LastViolationDesc;
extern std::string LastViolationCode; 

extern std::string LastServerResponse;

extern std::string CharacterStatus;
extern ImVec4 CharacterColor;