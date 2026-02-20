// Logger.h
#pragma once
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