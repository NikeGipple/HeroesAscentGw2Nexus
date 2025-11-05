#pragma once
#include <cstdint>
#include "nexus/Nexus.h"

struct ArcDPSAgent {
    uint64_t ID;
    char     Name[64];
    uint32_t Profession;
    uint32_t Elite;
    uint16_t Team;
    uint16_t Subgroup;
    uint16_t Level;
};

void InitArcIntegration(AddonAPI* api);
