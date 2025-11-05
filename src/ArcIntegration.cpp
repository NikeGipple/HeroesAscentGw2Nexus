#include "ArcIntegration.h"
#include <string>
#include "Globals.h"

extern uint32_t PlayerLevel;
extern std::string PlayerName;

void InitArcIntegration(AddonAPI* api) {
    if (!api) return;
    api->Log(ELogLevel_INFO, "ArcIntegration", "ArcDPS bridge initialized (stub).");
    // qui potrai fare api->Events.Subscribe(...) quando agganci ArcDPS
}
