// ArcIntegration.cpp
#include "ArcIntegration.h"
#include "Globals.h"
#include <string>
#include <thread>
#include <unordered_set>
#include "Network.h"
#include "Logger.h"
#include <Windows.h>

extern AddonAPI* APIDefs;

void RawDump(void* data, const char* area)
{
    if (!data) return;

    unsigned char* bytes = reinterpret_cast<unsigned char*>(data);
    const size_t dumpSize = 256; // aumenta se vuoi

    char buffer[4096];
    char* out = buffer;

    // Prima riga
    out += sprintf_s(out, buffer + sizeof(buffer) - out,
        "[RAW DUMP] area=%s ptr=%p\n",
        area, data);

    // Hex dump
    for (size_t i = 0; i < dumpSize; ++i) {

        if (i % 16 == 0) {
            out += sprintf_s(out, buffer + sizeof(buffer) - out,
                "\n%04zu: ", i);
        }

        out += sprintf_s(out, buffer + sizeof(buffer) - out,
            "%02X ", bytes[i]);
    }

    // Log finale
    APIDefs->Log(ELogLevel_DEBUG, "ArcDump", buffer);
}

void OnBuffApplied(uint32_t buffId, const char* buffName) {
    //char msg[256];
    //sprintf_s(msg, "[BUFF DETECTED] buffId=%u | name=%s",
    //    buffId,
    //    (buffName ? buffName : "(unknown)")
    //);
    //APIDefs->Log(ELogLevel_DEBUG, "ArcIntegration", msg);

    SendPlayerUpdate(PlayerEventType::BUFF_APPLIED, buffId, buffName);
}

/* === Callback per gli eventi di combattimento === */
void OnArcCombat(void* data, const char* sourceArea) {
    if (!data) return;
    EvCombatData* e = static_cast<EvCombatData*>(data);
    if (!e || !e->ev) return;

    // === Log SOLO quando il soggetto al 50% è il TUO personaggio ===
    if (e->ev->is_fifty == 1 && e->dst && e->dst->self == 1) {

        PlayerBelow50HP = true;
        PlayerBelow50HP_Time = GetTickCount64();

        //const char* skill = (e->skillname ? e->skillname : "(no skill)");

        //char msg[256];
        //sprintf_s(msg,
        //    "[HP50] PLAYER dropped below 50%% HP! Incoming skill=%s (id=%u)",
        //    skill,
        //    e->ev->skillid
        //);

        //APIDefs->Log(ELogLevel_INFO, "ArcIntegration", msg);
    }



    // === LOG RAW LEGGIBILE ===
    /*LogArcEvent(e, sourceArea);*/
    /*LogArcEventUltraCompact(e, sourceArea);*/

    const char* src = (e->src && e->src->name && strlen(e->src->name)) ? e->src->name : "(unknown)";
    const char* dst = (e->dst && e->dst->name && strlen(e->dst->name)) ? e->dst->name : "(unknown)";
    const char* skill = (e->skillname && strlen(e->skillname)) ? e->skillname : "(no skill)";

    // Esempio semplice: logga solo gli eventi di danno positivi
    //if (e->ev->value > 0 && e->src && e->src->self == 1) {
    //    char msg[512];
    //    sprintf_s(msg,
    //        "[%s] %s hit %s for %d dmg (Skill: %s - ID: %u)",
    //        sourceArea,
    //        src,
    //        dst,
    //        e->ev->value,
    //        skill,
    //        e->ev->skillid
    //    );

    //    APIDefs->Log(ELogLevel_INFO, "ArcIntegration", msg);
    //}

    // === Rilevazione Skill 6 (cura attiva) ===
    if (e->src && e->src->self == 1) {

        // Deve essere un evento di attivazione
        if (e->ev->is_activation != 0) {

            uint32_t skillId = e->ev->skillid;

            // Debug
            //char logMsg[512];
            //sprintf_s(logMsg,
            //    "[HEAL ACTIVATION CHECK] skillid: %u, skillname: %s",
            //    skillId,
            //    (e->skillname ? e->skillname : "(unknown)"));
            //APIDefs->Log(ELogLevel_DEBUG, "ArcIntegration", logMsg);

            // Skill 6 ID list
            static std::unordered_set<uint32_t> HealSkillIDs = {
                9102,   // Guardian Shelter
                21664,  // Guardian Litany of Wrath
                9083,   // Guardian Receive the Light!
                9158,   // Guardian Signet of Resolve

                26937,  // Revenant Enchanted Daggers
				29148,  // Revenant Ventari - Project Tranquility
                28427,  // Revenant Ventari's Will
                28219,  // Revenant Empowering Misery
                28219,  // Revenant Soothing Stone

				14401,  // Warrior Mending
                14402,  // Warrior To the Limit!
                14389,  // Warrior Healing Signet
                21815,  // Warrior Defiant Stance

                5834,   // Engineer Elixir H
                5802,   // Engineer Med Kit
                21659,  // Engineer A.E.D.
                30881,  // Engineer A.E.D.
                5980,   // Engineer Cleansing Burst
                5857,   // Engineer Healing Turret
                5857,   // Engineer Detonate Healing Turret

                12377,  // toolbelt - Blessing of Dwayna 
                29772,  // toolbelt - bandage from Med Kit
				6176,   // toolbelt - Regenerating Mist from Healing Turret

                31914,  // Ranger We Heal As One!
				21773,  // Ranger Water Spirit
                21776,  // Ranger - Water Spirit - Aqua Surge
                12483,  // Ranger Troll Unguent
                12489,  // Ranger Healing Spring

                13027,  // Thief Hide in Shadows
                13050,  // Thief Signet of Malice
                13021,  // Thief Withdraw
                21778,  // Thief Skelk Venom

                21656,  // Elementalist Arcane Brilliance
                5507,   // Elementalist Ether Renewal
                5503,   // Elementalist Signet of Restoration
				5569,   // Elementalist Glyph of Elemental Harmony
                34743,  // Glyph of Elemental Harmony(fire)
                34651,  // Glyph of Elemental Harmony(water)
                34724,  // Glyph of Elemental Harmony(air)
                34609,  // Glyph of Elemental Harmony(earth)

                10176,  // Mesmer Ether Feast
                10177,  // Mesmer Mirror
                10213,  // Mesmer Mantra of Recovery
                10214,  // Mesmer Power Return
                21750,  // Mesmer Signet of the Ether

                10548,  // Necromancer Consume Conditions
				10547,  // Necromancer Summon Blood Fiend
                10577,  // Necromancer Taste of Death
                21762,  // Necromancer Signet of Vampirism
                60625,  // Necromancer Signet of Vampirism
				10527,  // Necromancer Well of Blood
                10670,  // Necromancer Well of Blood

                12360,  // Human Prayer to Dwayna
                12440,  // Sylvari Healing Seed
            };

            if (HealSkillIDs.count(skillId)) {

                //APIDefs->Log(ELogLevel_WARNING, "ArcIntegration",
                //    "[VIOLATION] Player used healing skill 6!");

                SendPlayerUpdate(PlayerEventType::HEALING_USED);
            }
        }
    }


    // === Rilevazione BUFF applicati al giocatore ===
    bool isBuffApply =
        (e->ev->buff == 1 && e->ev->is_statechange == 0) ||
        (e->ev->is_statechange == 41);

    if (isBuffApply && e->ev->is_activation == 0)
    {
        const char* buffName = e->skillname ? e->skillname : "";
        uint32_t buffId = e->ev->skillid;

        // Filtra Nourishment / Enhancement per SELF
        if ((strcmp(buffName, "Nourishment") == 0 ||
            strcmp(buffName, "Enhancement") == 0) &&
            e->dst && e->dst->self == 1)
        {
            OnBuffApplied(buffId, buffName);
        }
        else if (buffId == 9283)  // Reinforced Armor → NON serve dst->self
        {
            OnBuffApplied(9283, "Reinforced Armor");
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
