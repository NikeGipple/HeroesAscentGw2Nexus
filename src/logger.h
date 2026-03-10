// Logger.h
#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
#include "ArcIntegration.h"
#include "Globals.h"

// Funzione principale
void LogArcEvent(struct EvCombatData* e, const char* sourceArea);
void LogArcEventCompact(struct EvCombatData* e, const char* sourceArea);
void LogArcEventUltraCompact(struct EvCombatData* e, const char* sourceArea);

// Funzioni helper per decodifica enum
const char* DecodeStateChange(uint8_t sc);
const char* DecodeActivation(uint8_t act);
const char* DecodeBuffRemove(uint8_t br);
const char* DecodeIFF(uint8_t iff);

bool TryGetViolation(const std::string& code, std::string& outTitle, std::string& outDesc);


// ============================================================
// PROBE (ring buffer ArcDPS) - WIDE DEBUG
// ============================================================
void ProbeSetEnabled(bool enabled);
bool ProbeIsEnabled();

void ProbeSetWindowMs(uint32_t ms);     // es: 20000
void ProbeSetMaxEvents(size_t n);       // es: 5000

void ProbePush(struct EvCombatData* e, const char* area);
void ProbeDump(const char* reason);
void ProbeClear();

void ProbeAutoDumpIfInteresting(struct EvCombatData* e, const char* area);