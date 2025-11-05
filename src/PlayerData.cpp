#include "PlayerData.h"
#include <cstring>

RealTimeData* RTAPIData = nullptr;
PlayerSnapshot LastSnapshot{};
uint32_t PlayerLevel = 0;
std::string PlayerName;

void InitPlayerData(AddonAPI* api) {
    if (!api) return;
    RTAPIData = (RealTimeData*)api->DataLink.Get(DL_RTAPI);
    if (RTAPIData && std::strlen(RTAPIData->CharacterName) > 0)
        PlayerName = RTAPIData->CharacterName;
}

bool HasChanged(const RealTimeData* d) {
    if (!d) return false;
    return d->MapID != LastSnapshot.MapID || d->CharacterState != LastSnapshot.CharacterState;
}
