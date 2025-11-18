// Globals.cpp
#include "Globals.h"
#include <string>
#include "UIColors.h"

AddonAPI* APIDefs = nullptr;
RealTimeData* RTAPIData = nullptr;

std::string LastViolationTitle;
std::string LastViolationDesc;
std::string LastViolationCode;

std::string LastServerResponse;
std::string CharacterStatus = "";
ImVec4 CharacterColor = ColorGray;

