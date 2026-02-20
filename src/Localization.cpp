// Localization.cpp
#include "Localization.h"
#include <Windows.h>
#include <fstream>
#include <string>
#include <map>
#include "json/json.hpp"
#include "nexus/Nexus.h"
#include "Globals.h"
#include "Network.h"
#include <mutex>

static std::mutex gLocMx;


using json = nlohmann::json;

/* === Variabili globali === */
std::string CurrentLang = "en";
std::map<std::string, std::string> Translations;
std::map<std::string, std::pair<std::string, std::string>> Violations;

/* === Utility base path === */
extern "C" IMAGE_DOS_HEADER __ImageBase; // serve per ottenere il path reale della DLL

std::string GetAddonBasePath() {
    char dllPath[MAX_PATH];
    GetModuleFileNameA((HINSTANCE)&__ImageBase, dllPath, MAX_PATH);
    std::string basePath(dllPath);
    basePath = basePath.substr(0, basePath.find_last_of("\\/"));

    // Forza sempre il percorso alla sottocartella del modulo
    basePath += "\\HeroesAscentGw2Nexus";

    /*if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Localization", ("Base path: " + basePath).c_str());*/


    return basePath;
}


/* === Inizializzazione === */
void InitLocalization(AddonAPI* api) {
    APIDefs = api;
    LoadLanguage(CurrentLang);
    LoadViolations(CurrentLang);
}

/* === Caricamento traduzioni === */
void LoadLanguage(const std::string& lang) {
    // Translations.clear();

    std::string path = GetAddonBasePath() + "\\locales\\" + lang + ".json";

    std::ifstream file(path);
    if (!file.is_open()) {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Localization", ("Language file not found: " + path).c_str());

        if (lang != "en") {
            if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Localization", "Falling back to English...");
            LoadLanguage("en");
        }
        return;
    }

    std::map<std::string, std::string> TranslationsTmp;

    try {
        json data = json::parse(file);
        for (auto it = data.begin(); it != data.end(); ++it) {
            TranslationsTmp[it.key()] = it.value().get<std::string>();
        }
    }
    catch (const std::exception& e) {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Localization", ("Failed to parse " + path + ": " + e.what()).c_str());

    }

    {
        std::scoped_lock lk(gLocMx);
        Translations.swap(TranslationsTmp);
    }

}

/* === Caricamento violazioni === */
void LoadViolations(const std::string& lang) {
    // Violations.clear();
    std::string path = GetAddonBasePath() + "\\violations\\" + lang + ".json";

    if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Localization", ("Loading violations file: " + path).c_str());

    std::ifstream file(path);
    if (!file.is_open()) {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Localization", ("No violations file found: " + path).c_str());
        return;
    }

    std::map<std::string, std::pair<std::string, std::string>> ViolationsTmp;

    try {
        json data = json::parse(file);
        for (auto it = data.begin(); it != data.end(); ++it) {
            std::string code = it.key();
            if (data[code].contains("title") && data[code].contains("description")) {
                ViolationsTmp[code] = {
                    data[code]["title"].get<std::string>(),
                    data[code]["description"].get<std::string>()
                };
            }
        }
    }
    catch (const std::exception& e) {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Localization", 
                ("Failed to parse violations: " + std::string(e.what())).c_str());
    }

    {
        std::scoped_lock lk(gLocMx);
        Violations.swap(ViolationsTmp);
    }
}

/* === Traduttore === */
const char* T(const std::string& key) {
    thread_local std::string tls;

    {
        std::scoped_lock lk(gLocMx);
        auto it = Translations.find(key);
        if (it != Translations.end()) {
            tls = it->second;       
            return tls.c_str();
        }
    }

    tls = key; 
    return tls.c_str();
}

bool TryGetViolation(const std::string& code, std::string& outTitle, std::string& outDesc) {
    std::scoped_lock lk(gLocMx);
    auto it = Violations.find(code);
    if (it == Violations.end()) return false;
    outTitle = it->second.first;
    outDesc = it->second.second;
    return true;
}