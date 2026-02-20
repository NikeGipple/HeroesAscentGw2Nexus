// Localization.h
#pragma once
#include <string>
#include <map>
#include "nexus/Nexus.h"

// === Dati globali ===
extern std::string CurrentLang;
extern std::map<std::string, std::string> Translations;
extern std::map<std::string, std::pair<std::string, std::string>> Violations;

// === Funzioni principali ===
void InitLocalization(AddonAPI* api);
void LoadLanguage(const std::string& lang);
void LoadViolations(const std::string& lang);
const char* T(const std::string& key);
std::string GetAddonBasePath();

bool TryGetViolation(const std::string& code, std::string& outTitle, std::string& outDesc);