#pragma once
#include <cstdint>
#include "nexus/Nexus.h"

/* === Strutture base di ArcDPS (come da demo ufficiale) === */
struct cbtevent {
    uint64_t time;
    uint64_t src_agent;
    uint64_t dst_agent;
    int32_t value;
    int32_t buff_dmg;
    uint32_t overstack_value;
    uint32_t skillid;
    uint16_t src_instid;
    uint16_t dst_instid;
    uint16_t src_master_instid;
    uint16_t dst_master_instid;
    uint8_t iff;
    uint8_t buff;
    uint8_t result;
    uint8_t is_activation;
    uint8_t is_buffremove;
    uint8_t is_ninety;
    uint8_t is_fifty;
    uint8_t is_moving;
    uint8_t is_statechange;
    uint8_t is_flanking;
    uint8_t is_shields;
    uint8_t is_offcycle;
    uint8_t pad61;
    uint8_t pad62;
    uint8_t pad63;
    uint8_t pad64;
};

struct ag {
    char* name;        // Nome agente (UTF-8)
    uintptr_t id;      // Identificatore univoco
    uint32_t prof;     // Professione
    uint32_t elite;    // Elite spec
    uint32_t self;     // 1 = se stesso
    uint16_t team;     // ID team
};

struct EvCombatData {
    cbtevent* ev;
    ag* src;
    ag* dst;
    char* skillname;
    uint64_t id;
    uint64_t revision;
};

/* === Funzione di inizializzazione === */
void InitArcIntegration(AddonAPI* api);
