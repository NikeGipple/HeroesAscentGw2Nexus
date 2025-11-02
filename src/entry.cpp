#include <Windows.h>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <thread>
#include <cmath>
#include <algorithm>
#include <winhttp.h>
#include "nexus/Nexus.h"
#include "mumble/Mumble.h"
#include "RTAPI/RTAPI.h"
#include "imgui/imgui.h"

#pragma comment(lib, "winhttp.lib")

/* === Globali === */
AddonDefinition AddonDef = {};
AddonAPI* APIDefs = nullptr;
Mumble::Data* MumbleLink = nullptr;
RealTimeData* RTAPIData = nullptr;
HMODULE hSelf = nullptr;

// Lingue caricate
std::map<std::string, std::string> Translations; // UI
std::map<std::string, std::pair<std::string, std::string>> Violations; // codice → {titolo, descrizione}
std::string CurrentLang = "en";

// Stato server
std::string ServerStatus;
ImVec4 ServerColor = ImVec4(1, 1, 0, 1);
std::string LastViolationTitle;
std::string LastViolationDesc;
std::string LastViolationCode; // codice violazione attuale

/* === Snapshot dati giocatore === */
struct PlayerSnapshot {
    std::string Name;
    uint32_t MapID = 0;
    uint32_t CharacterState = 0;
    float Position[3] = { 0 };
};
PlayerSnapshot LastSnapshot;

/* === Utility: Lettura file === */
std::string ReadFileToString(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

/* === Caricamento lingua interfaccia === */
void LoadLanguage(const std::string& lang) {
    Translations.clear();

    char dllPath[MAX_PATH];
    GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
    std::string basePath = std::string(dllPath);
    basePath = basePath.substr(0, basePath.find_last_of("\\/"));

    std::string path = basePath + "\\HeroesAscentGw2Nexus\\locales\\" + lang + ".json";

    std::ifstream file(path);
    if (!file.is_open()) {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "HeroesAscent", ("Missing language file: " + path).c_str());
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        size_t keyStart = line.find('"');
        if (keyStart == std::string::npos) continue;
        size_t keyEnd = line.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) continue;

        size_t valStart = line.find('"', keyEnd + 1);
        if (valStart == std::string::npos) continue;
        size_t valEnd = line.find('"', valStart + 1);
        if (valEnd == std::string::npos) continue;

        std::string key = line.substr(keyStart + 1, keyEnd - keyStart - 1);
        std::string val = line.substr(valStart + 1, valEnd - valStart - 1);
        Translations[key] = val;
    }
    file.close();
}

/* === Caricamento violazioni === */
void LoadViolations(const std::string& lang) {
    Violations.clear();

    char dllPath[MAX_PATH];
    GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
    std::string basePath = std::string(dllPath);
    basePath = basePath.substr(0, basePath.find_last_of("\\/"));

    std::string path = basePath + "\\HeroesAscentGw2Nexus\\violations\\" + lang + ".json";

    std::ifstream file(path);
    if (!file.is_open()) {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "HeroesAscent", ("Missing violations file: " + path).c_str());
        return;
    }

    std::string line, code, title, description;
    while (std::getline(file, line)) {
        size_t codeStart = line.find('"');
        if (codeStart == std::string::npos) continue;
        size_t codeEnd = line.find('"', codeStart + 1);
        if (codeEnd == std::string::npos) continue;
        std::string token = line.substr(codeStart + 1, codeEnd - codeStart - 1);

        if (token.rfind("RULE_", 0) == 0) {
            code = token;
        }
        else if (line.find("\"title\"") != std::string::npos) {
            size_t valStart = line.find('"', line.find(':') + 1);
            size_t valEnd = line.find_last_of('"');
            title = line.substr(valStart + 1, valEnd - valStart - 1);
        }
        else if (line.find("\"description\"") != std::string::npos) {
            size_t valStart = line.find('"', line.find(':') + 1);
            size_t valEnd = line.find_last_of('"');
            description = line.substr(valStart + 1, valEnd - valStart - 1);
            if (!code.empty()) {
                Violations[code] = { title, description };
                code.clear();
                title.clear();
                description.clear();
            }
        }
    }

    file.close();
}

/* === Traduttore === */
const char* T(const std::string& key) {
    if (Translations.find(key) != Translations.end())
        return Translations[key].c_str();
    return key.c_str();
}

/* === Controllo cambiamenti === */
bool HasChanged(const RealTimeData* data) {
    if (!data) return false;
    if (data->MapID != LastSnapshot.MapID) return true;
    if (data->CharacterState != LastSnapshot.CharacterState) return true;
    return false;
}

/* === Invio aggiornamento al server === */
void SendPlayerUpdate() {
    if (!RTAPIData) return;

    std::ostringstream payload;
    payload << "{"
        << "\"name\":\"" << RTAPIData->CharacterName << "\","
        << "\"map\":" << RTAPIData->MapID << ","
        << "\"state\":" << RTAPIData->CharacterState
        << "}";

    HINTERNET hSession = WinHttpOpen(L"HeroesAscent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (hSession) {
        HINTERNET hConnect = WinHttpConnect(hSession, L"heroesascentserver.onrender.com",
            INTERNET_DEFAULT_HTTPS_PORT, 0);

        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST",
                L"/update", NULL, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

            if (hRequest) {
                std::wstring headers = L"Content-Type: application/json\r\n";
                std::string body = payload.str();

                BOOL bResults = WinHttpSendRequest(hRequest,
                    headers.c_str(), -1L,
                    (LPVOID)body.c_str(), body.length(),
                    body.length(), 0);

                if (bResults && WinHttpReceiveResponse(hRequest, NULL)) {
                    DWORD dwSize = 0;
                    std::string response;
                    do {
                        WinHttpQueryDataAvailable(hRequest, &dwSize);
                        if (dwSize == 0) break;
                        std::vector<char> buffer(dwSize + 1);
                        DWORD dwDownloaded = 0;
                        WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded);
                        buffer[dwDownloaded] = '\0';
                        response += buffer.data();
                    } while (dwSize > 0);

                    // Pulisce la risposta
                    response.erase(std::remove_if(response.begin(), response.end(),
                        [](unsigned char c) { return c == '\n' || c == '\r' || c == ' '; }),
                        response.end());

                    // Analisi risposta
                    if (response.find("\"rules_valid\":false") != std::string::npos) {
                        ServerStatus = T("ui.violation_detected");
                        ServerColor = ImVec4(1, 0.4f, 0.4f, 1);

                        size_t codeStart = response.find("\"violation_code\":\"");
                        if (codeStart != std::string::npos) {
                            codeStart += 18;
                            size_t codeEnd = response.find('"', codeStart);
                            std::string code = response.substr(codeStart, codeEnd - codeStart);

                            LastViolationCode = code;
                            if (Violations.find(code) != Violations.end()) {
                                LastViolationTitle = Violations[code].first;
                                LastViolationDesc = Violations[code].second;
                            }
                            else {
                                LastViolationTitle = code;
                                LastViolationDesc = T("ui.unknown_violation");
                            }
                        }
                    }
                    else {
                        ServerStatus = T("ui.rules_respected");
                        ServerColor = ImVec4(0.3f, 1, 0.3f, 1);
                        LastViolationTitle.clear();
                        LastViolationDesc.clear();
                        LastViolationCode.clear();
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }
}

/* === Entry Point DLL === */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) hSelf = hModule;
    return TRUE;
}

/* === Addon Definition === */
extern "C" __declspec(dllexport) AddonDefinition* GetAddonDef() {
    AddonDef.Signature = -987654321;
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "HeroesAscentGw2Nexus";
    AddonDef.Version.Major = 3;
    AddonDef.Version.Minor = 6;
    AddonDef.Author = "NikeGipple";
    AddonDef.Description = "HeroesAscent Assistant with multilingual violations and UI";
    AddonDef.Flags = EAddonFlags_None;

    AddonDef.Load = [](AddonAPI* aApi) {
        APIDefs = aApi;

        ImGui::SetCurrentContext((ImGuiContext*)aApi->ImguiContext);
        ImGui::SetAllocatorFunctions(
            (void* (*)(size_t, void*))aApi->ImguiMalloc,
            (void(*)(void*, void*))aApi->ImguiFree);

        MumbleLink = (Mumble::Data*)aApi->DataLink.Get("DL_MUMBLE_LINK");
        RTAPIData = (RealTimeData*)aApi->DataLink.Get(DL_RTAPI);

        LoadLanguage(CurrentLang);
        LoadViolations(CurrentLang);
        ServerStatus = T("ui.checking_server");

        aApi->Renderer.Register(ERenderType_Render, []() {
            if (!APIDefs) return;
            if (!RTAPIData)
                RTAPIData = (RealTimeData*)APIDefs->DataLink.Get(DL_RTAPI);

            static uint64_t lastCheck = 0;
            uint64_t now = GetTickCount64();
            if (now - lastCheck > 200) {
                if (HasChanged(RTAPIData)) {
                    LastSnapshot.MapID = RTAPIData->MapID;
                    LastSnapshot.CharacterState = RTAPIData->CharacterState;
                    std::thread(SendPlayerUpdate).detach();
                }
                lastCheck = now;
            }

            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(500, 420), ImGuiCond_FirstUseEver);
            ImGui::Begin("HeroesAscent Assistant", nullptr,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

            // === Selettore lingua (ora in alto) ===
            ImGui::Text("%s:", "Language");
            ImGui::SameLine();
            static const char* langs[] = { "English", "Italiano" };
            static int currentLang = (CurrentLang == "it") ? 1 : 0;
            if (ImGui::BeginCombo("##lang", langs[currentLang])) {
                for (int i = 0; i < IM_ARRAYSIZE(langs); i++) {
                    bool selected = (currentLang == i);
                    if (ImGui::Selectable(langs[i], selected)) {
                        currentLang = i;
                        CurrentLang = (i == 0) ? "en" : "it";
                        LoadLanguage(CurrentLang);
                        LoadViolations(CurrentLang);

                        if (!LastViolationCode.empty() && Violations.find(LastViolationCode) != Violations.end()) {
                            LastViolationTitle = Violations[LastViolationCode].first;
                            LastViolationDesc = Violations[LastViolationCode].second;
                        }

                        if (ServerColor.x == 1 && ServerColor.y == 0.4f)
                            ServerStatus = T("ui.violation_detected");
                        else if (ServerColor.x == 0.3f && ServerColor.y == 1)
                            ServerStatus = T("ui.rules_respected");
                        else
                            ServerStatus = T("ui.checking_server");
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            // === Stato server e violazioni ===
            ImGui::TextColored(ServerColor, "%s", ServerStatus.c_str());
            if (!LastViolationTitle.empty()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", LastViolationTitle.c_str());
                ImGui::TextWrapped("%s", LastViolationDesc.c_str());
            }

            ImGui::Separator();

            std::string playerName = "N/A";
            if (RTAPIData && strlen(RTAPIData->CharacterName) > 0)
                playerName = RTAPIData->CharacterName;

            ImGui::Text("%s: %s", T("ui.character"), playerName.c_str());

            if (RTAPIData && RTAPIData->GameBuild != 0) {
                uint32_t cs = RTAPIData->CharacterState;
                bool isAlive = cs & CS_IsAlive;
                bool isDowned = cs & CS_IsDowned;
                bool inCombat = cs & CS_IsInCombat;

                const char* stato = T("ui.state.normal");
                ImVec4 color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

                if (isDowned && !isAlive) {
                    stato = T("ui.state.dead");
                    color = ImVec4(1, 0.3f, 0.3f, 1);
                }
                else if (isDowned) {
                    stato = T("ui.state.downed");
                    color = ImVec4(1, 0.6f, 0.1f, 1);
                }
                else if (inCombat) {
                    stato = T("ui.state.combat");
                    color = ImVec4(0.3f, 1, 0.3f, 1);
                }

                ImGui::Separator();
                ImGui::TextColored(color, "%s: %s", T("ui.status"), stato);
                ImGui::Text("%s: %u | Type: %u", T("ui.map"), RTAPIData->MapID, RTAPIData->MapType);
                ImGui::Text("%s: %.2f, %.2f, %.2f", T("ui.position"),
                    RTAPIData->CharacterPosition[0],
                    RTAPIData->CharacterPosition[1],
                    RTAPIData->CharacterPosition[2]);
            }
            else {
                ImGui::TextColored(ImVec4(1, 0.5f, 0.2f, 1), "%s", T("ui.not_available"));
            }

            ImGui::End();
            });
        };

    AddonDef.Unload = []() {
        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "HeroesAscent", "Addon unloaded.");
        };

    return &AddonDef;
}
