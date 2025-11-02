#include <Windows.h>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
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
std::map<std::string, std::string> Translations;
std::string CurrentLang = "en";

// Stato server
std::string ServerStatus = "Checking...";
ImVec4 ServerColor = ImVec4(1, 1, 0, 1);

/* === Utility: Caricamento file JSON (parser minimale) === */
void LoadLanguage(const std::string& lang) {
    Translations.clear();

    char dllPath[MAX_PATH];
    GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
    std::string basePath = std::string(dllPath);
    basePath = basePath.substr(0, basePath.find_last_of("\\/"));

    std::string path = basePath + "\\HeroesAscentGw2Nexus\\locales\\" + lang + ".json";

    if (APIDefs)
        APIDefs->Log(ELogLevel_INFO, "HeroesAscent", ("Loading language file: " + path).c_str());

    std::ifstream file(path);
    if (!file.is_open()) {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "HeroesAscent", ("Could not find language file: " + path).c_str());
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

    if (APIDefs)
        APIDefs->Log(ELogLevel_INFO, "HeroesAscent", ("Language loaded: " + lang).c_str());
}

/* === Funzione helper === */
const char* T(const std::string& key) {
    if (Translations.find(key) != Translations.end())
        return Translations[key].c_str();
    return key.c_str();
}

/* === Controllo Server === */
std::string CheckServerStatus() {
    std::string result = "unknown";

    HINTERNET hSession = WinHttpOpen(L"HeroesAscent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (hSession) {
        HINTERNET hConnect = WinHttpConnect(hSession, L"heroesascentserver.onrender.com",
            INTERNET_DEFAULT_HTTPS_PORT, 0);

        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
                L"/", NULL, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE);

            if (hRequest && WinHttpSendRequest(hRequest,
                WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {

                if (WinHttpReceiveResponse(hRequest, NULL)) {
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

                    result = response;
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }

    return result;
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
    AddonDef.Version.Major = 2;
    AddonDef.Version.Minor = 5;
    AddonDef.Version.Build = 0;
    AddonDef.Version.Revision = 0;
    AddonDef.Author = "NikeGipple";
    AddonDef.Description = "HeroesAscent Assistant with multilingual and server connection support";
    AddonDef.Flags = EAddonFlags_None;

    /* === Load === */
    AddonDef.Load = [](AddonAPI* aApi) {
        APIDefs = aApi;

        ImGui::SetCurrentContext((ImGuiContext*)aApi->ImguiContext);
        ImGui::SetAllocatorFunctions(
            (void* (*)(size_t, void*))aApi->ImguiMalloc,
            (void(*)(void*, void*))aApi->ImguiFree);

        MumbleLink = (Mumble::Data*)aApi->DataLink.Get("DL_MUMBLE_LINK");
        RTAPIData = (RealTimeData*)aApi->DataLink.Get(DL_RTAPI);

        LoadLanguage(CurrentLang);

        // Controlla server
        std::string response = CheckServerStatus();
        aApi->Log(ELogLevel_INFO, "HeroesAscent", ("Server response: " + response).c_str());

        if (response.find("\"status\":\"ok\"") != std::string::npos) {
            ServerStatus = "Server online";
            ServerColor = ImVec4(0.3f, 1, 0.3f, 1);
        }
        else {
            ServerStatus = "Server offline";
            ServerColor = ImVec4(1, 0.3f, 0.3f, 1);
        }

        aApi->Renderer.Register(ERenderType_Render, []() {
            if (!APIDefs) return;
            if (!RTAPIData)
                RTAPIData = (RealTimeData*)APIDefs->DataLink.Get(DL_RTAPI);

            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(480, 340), ImGuiCond_FirstUseEver);
            ImGui::Begin("HeroesAscent Assistant", nullptr,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

            // === Server Status ===
            ImGui::TextColored(ServerColor, "%s", ServerStatus.c_str());
            ImGui::Separator();

            // === Selettore lingua ===
            ImGui::Text("%s:", "Language");
            ImGui::SameLine();
            static const char* langs[] = { "English", "Italiano" };
            static int currentLang = 0;
            if (ImGui::BeginCombo("##lang", langs[currentLang])) {
                for (int i = 0; i < IM_ARRAYSIZE(langs); i++) {
                    bool selected = (currentLang == i);
                    if (ImGui::Selectable(langs[i], selected)) {
                        currentLang = i;
                        CurrentLang = (i == 0) ? "en" : "it";
                        LoadLanguage(CurrentLang);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            // === Nome personaggio ===
            std::string playerName = "N/A";
            if (RTAPIData && strlen(RTAPIData->CharacterName) > 0)
                playerName = RTAPIData->CharacterName;
            else if (MumbleLink && MumbleLink->Identity && wcslen(MumbleLink->Identity) > 0) {
                char jsonUtf8[2048];
                WideCharToMultiByte(CP_UTF8, 0, MumbleLink->Identity, -1,
                    jsonUtf8, sizeof(jsonUtf8), nullptr, nullptr);
                const char* start = strstr(jsonUtf8, "\"name\":\"");
                if (start) {
                    start += 8;
                    const char* end = strchr(start, '"');
                    if (end) playerName.assign(start, end - start);
                }
            }

            ImGui::Text("%s: %s", T("ui.character"), playerName.c_str());
            ImGui::Separator();

            // === Stato ===
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

                ImGui::TextColored(color, "%s: %s", T("ui.status"), stato);

                ImGui::Separator();
                ImGui::Text("%s: %u | Type: %u", T("ui.map"), RTAPIData->MapID, RTAPIData->MapType);
                ImGui::Text("%s: X %.2f | Y %.2f | Z %.2f",
                    T("ui.position"),
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

    /* === Unload === */
    AddonDef.Unload = []() {
        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "HeroesAscent", "Addon unloaded.");
        };

    return &AddonDef;
}
