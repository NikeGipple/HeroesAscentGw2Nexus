#include "Localization.h"
#include <Windows.h>
#include <fstream>
#include <string>
#include <map>
#include "json/json.hpp"
#include "nexus/Nexus.h"


using json = nlohmann::json;

/* === Variabili globali === */
std::string CurrentLang = "en";
std::map<std::string, std::string> Translations;
std::map<std::string, std::pair<std::string, std::string>> Violations;
extern AddonAPI* APIDefs;

/* === Utility base path === */
extern "C" IMAGE_DOS_HEADER __ImageBase; // serve per ottenere il path reale della DLL

std::string GetAddonBasePath() {
    char dllPath[MAX_PATH];
    GetModuleFileNameA((HINSTANCE)&__ImageBase, dllPath, MAX_PATH);
    std::string basePath(dllPath);
    basePath = basePath.substr(0, basePath.find_last_of("\\/"));

    // Forza sempre il percorso alla sottocartella del modulo
    basePath += "\\HeroesAscentGw2Nexus";


    return basePath;
}


/* === Inizializzazione === */
void InitLocalization(AddonAPI* api) {
    APIDefs = api;
    if (APIDefs)
        APIDefs->Log(ELogLevel_INFO, "Localization", "Initializing localization system...");
    LoadLanguage(CurrentLang);
    LoadViolations(CurrentLang);
}

/* === Caricamento traduzioni === */
void LoadLanguage(const std::string& lang) {
    Translations.clear();

    std::string path = GetAddonBasePath() + "\\locales\\" + lang + ".json";
    if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Localization", ("Loading language file: " + path).c_str());

    std::ifstream file(path);
    if (!file.is_open()) {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Localization", ("Language file not found: " + path).c_str());

        if (lang != "en") {
            if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Localization", "Falling back to English...");
            LoadLanguage("en");
        }
        return;
    }

    try {
        json data = json::parse(file);
        for (auto it = data.begin(); it != data.end(); ++it) {
            Translations[it.key()] = it.value().get<std::string>();
        }
        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "Localization", ("Loaded " + std::to_string(Translations.size()) + " translations").c_str());
    }
    catch (const std::exception& e) {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Localization", ("Failed to parse " + path + ": " + e.what()).c_str());
    }

    file.close();
}

/* === Caricamento violazioni === */
void LoadViolations(const std::string& lang) {
    Violations.clear();
    std::string path = GetAddonBasePath() + "\\violations\\" + lang + ".json";

    if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Localization", ("Loading violations file: " + path).c_str());

    std::ifstream file(path);
    if (!file.is_open()) {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Localization", ("No violations file found: " + path).c_str());
        return;
    }

    try {
        json data = json::parse(file);
        for (auto it = data.begin(); it != data.end(); ++it) {
            std::string code = it.key();
            if (data[code].contains("title") && data[code].contains("description")) {
                Violations[code] = {
                    data[code]["title"].get<std::string>(),
                    data[code]["description"].get<std::string>()
                };
            }
        }
        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "Localization", ("Loaded " + std::to_string(Violations.size()) + " violations").c_str());
    }
    catch (const std::exception& e) {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Localization", ("Failed to parse violations: " + std::string(e.what())).c_str());
    }

    file.close();
}

/* === Traduttore === */
const char* T(const std::string& key) {
    auto it = Translations.find(key);
    if (it != Translations.end()) return it->second.c_str();

    if (APIDefs) {
        std::string msg = "Missing key: " + key;
        APIDefs->Log(ELogLevel_WARNING, "Localization", msg.c_str());
    }

    return key.c_str();
}
