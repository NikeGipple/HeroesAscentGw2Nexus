// ArcIntegration.cpp
#include "ArcIntegration.h"
#include "Globals.h"
#include <string>
#include <thread>

extern AddonAPI* APIDefs;

/* === Callback per gli eventi di combattimento === */
void OnArcCombat(void* data, const char* sourceArea) {
    if (!data) return;
    EvCombatData* e = static_cast<EvCombatData*>(data);
    if (!e || !e->ev) return;

    const char* src = (e->src && e->src->name && strlen(e->src->name)) ? e->src->name : "(unknown)";
    const char* dst = (e->dst && e->dst->name && strlen(e->dst->name)) ? e->dst->name : "(unknown)";
    const char* skill = (e->skillname && strlen(e->skillname)) ? e->skillname : "(no skill)";

    // Esempio semplice: logga solo gli eventi di danno positivi
    if (e->ev->value > 0 && e->src && e->src->self == 1) {
        char msg[512];
        sprintf_s(msg,
            "[%s] %s hit %s for %d dmg (Skill: %s - ID: %u)",
            sourceArea,
            src,
            dst,
            e->ev->value,
            skill,
            e->ev->skillid
        );

        APIDefs->Log(ELogLevel_INFO, "ArcIntegration", msg);
    }
}

/* === Inizializzazione ArcDPS Integration === */
void InitArcIntegration(AddonAPI* api) {
    if (!api) return;
    APIDefs = api;

    api->Log(ELogLevel_INFO, "ArcIntegration", "Initializing ArcDPS combat event hooks...");

    // Prova a collegarsi direttamente agli eventi se ArcDPS è caricato
    try {
        api->Events.Subscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", [](void* data) {
            OnArcCombat(data, "LOCAL");
            });

        api->Events.Subscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW", [](void* data) {
            OnArcCombat(data, "SQUAD");
            });

        api->Log(ELogLevel_INFO, "ArcIntegration", "ArcDPS event subscriptions complete.");
    }
    catch (...) {
        api->Log(ELogLevel_WARNING, "ArcIntegration", "ArcDPS event system not available (is ArcDPS loaded?).");
    }
}
