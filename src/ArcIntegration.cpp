// ArcIntegration.cpp
#include "ArcIntegration.h"
#include "Globals.h"
#include <string>
#include <thread>
#include <unordered_set>
#include "Network.h"

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

    // === Rilevazione Skill 6 (cura attiva) ===
    if (e->src && e->src->self == 1) {

        // Deve essere un evento di attivazione
        if (e->ev->is_activation != 0) {

            uint32_t skillId = e->ev->skillid;

            // Debug
            char logMsg[512];
            sprintf_s(logMsg,
                "[HEAL ACTIVATION CHECK] skillid: %u, skillname: %s",
                skillId,
                (e->skillname ? e->skillname : "(unknown)"));
            APIDefs->Log(ELogLevel_DEBUG, "ArcIntegration", logMsg);

            // Skill 6 ID list
            static std::unordered_set<uint32_t> HealSkillIDs = {
                9080,   // Guardian
                5503,   // Mesmer
                5857,   // Engineer
                5505,   // Elementalist
                12452,  // Ranger
                13064,  // Thief
                21750,  // Necromancer
                // Revenant → ti aggiungo gli ID se vuoi
            };

            if (HealSkillIDs.count(skillId)) {

                APIDefs->Log(ELogLevel_WARNING, "ArcIntegration",
                    "[VIOLATION] Player used healing skill 6!");

                SendPlayerUpdate(PlayerEventType::HEALING_USED);
            }
        }
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
