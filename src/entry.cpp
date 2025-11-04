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

/* === Colori UI === */
ImVec4 ColorNeutral = ImVec4(1, 1, 0, 1);
ImVec4 ColorSuccess = ImVec4(0.3f, 1, 0.3f, 1);
ImVec4 ColorError = ImVec4(1, 0.4f, 0.4f, 1);
ImVec4 ColorWarning = ImVec4(1, 0.6f, 0.1f, 1);
ImVec4 ColorGray = ImVec4(0.7f, 0.7f, 0.7f, 1);
ImVec4 ColorInfo = ImVec4(0.3f, 0.8f, 1, 1);

/* === Variabili di stato === */
std::string ApiKey = "";
std::string AccountToken = "";
std::string RegistrationStatus = "Not registered";
ImVec4 RegistrationColor = ColorNeutral;

// Lingue caricate
std::map<std::string, std::string> Translations;
std::map<std::string, std::pair<std::string, std::string>> Violations;
std::string CurrentLang = "en";

// Stato server
std::string ServerStatus;
ImVec4 ServerColor = ColorNeutral;
std::string LastViolationTitle;
std::string LastViolationDesc;
std::string LastViolationCode;
std::string LastServerResponse;

/* === Snapshot dati giocatore === */
struct PlayerSnapshot {
    std::string Name;
    uint32_t MapID = 0;
    uint32_t CharacterState = 0;
    float Position[3] = { 0 };
};
PlayerSnapshot LastSnapshot;

/* === Utility base path === */
std::string GetAddonBasePath() {
    char dllPath[MAX_PATH];
    GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
    std::string basePath = std::string(dllPath);
    basePath = basePath.substr(0, basePath.find_last_of("\\/"));
    return basePath + "\\HeroesAscentGw2Nexus";
}

void SaveAccountToken(const std::string& token) {
    std::string path = GetAddonBasePath() + "\\accounttoken";
    std::ofstream out(path, std::ios::trunc);
    if (out.is_open()) {
        out << token;
        out.close();
    }
}

std::string LoadAccountToken() {
    std::string path = GetAddonBasePath() + "\\accounttoken";
    std::ifstream in(path);
    if (!in.is_open()) return "";
    std::string token;
    std::getline(in, token);
    in.close();
    return token;
}

/* === Caricamento lingua e violazioni === */
void LoadLanguage(const std::string& lang) {
    Translations.clear();
    std::string path = GetAddonBasePath() + "\\locales\\" + lang + ".json";
    std::ifstream file(path);
    if (!file.is_open()) return;

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

void LoadViolations(const std::string& lang) {
    Violations.clear();
    std::string path = GetAddonBasePath() + "\\violations\\" + lang + ".json";
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line, code, title, description;
    while (std::getline(file, line)) {
        size_t codeStart = line.find('"');
        if (codeStart == std::string::npos) continue;
        size_t codeEnd = line.find('"', codeStart + 1);
        if (codeEnd == std::string::npos) continue;
        std::string token = line.substr(codeStart + 1, codeEnd - codeStart - 1);
        if (token.rfind("RULE_", 0) == 0) code = token;
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

/* === Registrazione con API key di ANet === */
void SendRegistration() {
    if (ApiKey.empty()) return;

    std::ostringstream payload;
    payload << "{"
        << "\"api_key\":\"" << ApiKey << "\""
        << "}";

    HINTERNET hSession = WinHttpOpen(L"HeroesAscent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (hSession) {
        HINTERNET hConnect = WinHttpConnect(hSession, L"heroesascent.org",
            INTERNET_DEFAULT_HTTPS_PORT, 0);

        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST",
                L"/api/register", NULL, WINHTTP_NO_REFERER,
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

                    response.erase(std::remove_if(response.begin(), response.end(),
                        [](unsigned char c) {
                            return c == '\n' || c == '\r' || c == ' ';
                        }),
                        response.end());

                    if (response.find("\"status\":\"ok\"") != std::string::npos) {
                        RegistrationStatus = T("ui.registration_success");
                        RegistrationColor = ColorSuccess;

                        size_t tokenStart = response.find("\"account_token\":\"");
                        if (tokenStart != std::string::npos) {
                            tokenStart += 17;
                            size_t tokenEnd = response.find('"', tokenStart);
                            AccountToken = response.substr(tokenStart, tokenEnd - tokenStart);
                            SaveAccountToken(AccountToken);
                        }
                    }
                    else {
                        RegistrationStatus = T("ui.registration_failed");
                        RegistrationColor = ColorError;
                    }
                }
                else {
                    RegistrationStatus = T("ui.registration_unreachable");
                    RegistrationColor = ColorError;
                }

                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }
}

/* === Controllo cambiamenti === */
bool HasChanged(const RealTimeData* data) {
    if (!data) return false;
    return data->MapID != LastSnapshot.MapID || data->CharacterState != LastSnapshot.CharacterState;
}

/* === Invio aggiornamento al server === */
void SendPlayerUpdate() {
    if (!RTAPIData || AccountToken.empty()) return;

    std::ostringstream payload;
    payload << "{"
        << "\"token\":\"" << AccountToken << "\","
        << "\"name\":\"" << RTAPIData->CharacterName << "\","
        << "\"map\":" << RTAPIData->MapID << ","
        << "\"state\":" << RTAPIData->CharacterState
        << "}";

    HINTERNET hSession = WinHttpOpen(L"HeroesAscent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        HINTERNET hConnect = WinHttpConnect(hSession, L"heroesascent.org",
            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST",
                L"/api/character/update", NULL, WINHTTP_NO_REFERER,
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

                    response.erase(std::remove_if(response.begin(), response.end(),
                        [](unsigned char c) { return c == '\n' || c == '\r' || c == ' '; }),
                        response.end());
                    LastServerResponse = response;

                    if (response.find("\"rules_valid\":false") != std::string::npos) {
                        ServerStatus = T("ui.violation_detected");
                        ServerColor = ColorError;

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
                        ServerColor = ColorSuccess;
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
    AddonDef.Version.Major = 4;
    AddonDef.Version.Minor = 3;
    AddonDef.Author = "NikeGipple";
    AddonDef.Description = "HeroesAscent Assistant with full translation, token persistence and color palette";
    AddonDef.Flags = EAddonFlags_None;

    AddonDef.Load = [](AddonAPI* aApi) {
        APIDefs = aApi;
        ImGui::SetCurrentContext((ImGuiContext*)aApi->ImguiContext);
        ImGui::SetAllocatorFunctions(
            (void* (*)(size_t, void*))aApi->ImguiMalloc,
            (void (*)(void*, void*))aApi->ImguiFree);

        MumbleLink = (Mumble::Data*)aApi->DataLink.Get("DL_MUMBLE_LINK");
        RTAPIData = (RealTimeData*)aApi->DataLink.Get(DL_RTAPI);

        AccountToken = LoadAccountToken();
        if (!AccountToken.empty()) {
            RegistrationStatus = T("ui.registration_already");
            RegistrationColor = ColorSuccess;
        }

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
            ImGui::SetNextWindowSize(ImVec2(500, 480), ImGuiCond_FirstUseEver);
            ImGui::Begin("HeroesAscent Assistant", nullptr,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

            // === Selettore lingua (in alto) ===
            ImGui::TextColored(ColorInfo, "%s:", T("ui.language"));
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

                        // Aggiorna testo descrizione violazione nella nuova lingua
                        if (!LastViolationCode.empty() && Violations.find(LastViolationCode) != Violations.end()) {
                            LastViolationTitle = Violations[LastViolationCode].first;
                            LastViolationDesc = Violations[LastViolationCode].second;
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();


            // === Sezione Registrazione ===
            if (AccountToken.empty()) {
                ImGui::TextColored(ColorInfo, "%s", T("ui.registration_title"));
                ImGui::InputText("##apikey", (char*)ApiKey.c_str(), 100,
                    ImGuiInputTextFlags_CallbackResize,
                    [](ImGuiInputTextCallbackData* data) -> int {
                        std::string* str = (std::string*)data->UserData;
                        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                            str->resize(data->BufTextLen);
                            data->Buf = (char*)str->c_str();
                        }
                        return 0;
                    },
                    &ApiKey);
                ImGui::SameLine();
                if (ImGui::Button(T("ui.registration_button"))) std::thread(SendRegistration).detach();
                ImGui::SameLine();
                ImGui::TextColored(RegistrationColor, "%s", RegistrationStatus.c_str());
            }
            else {
                ImGui::TextColored(ColorSuccess, "%s", T("ui.registration_already"));
                ImGui::Text("%s: %s", T("ui.registration_token"), AccountToken.c_str());
            }

            ImGui::Separator();

            // === Stato server e violazioni ===
            ImGui::TextColored(ServerColor, "%s", ServerStatus.c_str());
            if (!LastViolationTitle.empty()) {
                ImGui::Separator();

                // Titolo violazione
                ImGui::TextColored(ColorError, "%s", LastViolationTitle.c_str());

                // Descrizione (tradotta)
                if (!LastViolationDesc.empty()) {
                    ImGui::TextWrapped("%s", LastViolationDesc.c_str());
                }
                else {
                    // fallback di sicurezza: se la descrizione non esiste, mostra chiave localizzata
                    ImGui::TextWrapped("%s", T("ui.unknown_violation"));
                }
            }

            ImGui::Separator();

            // === Stato personaggio ===
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
                ImVec4 color = ColorGray;
                if (isDowned && !isAlive) {
                    stato = T("ui.state.dead");
                    color = ColorError;
                }
                else if (isDowned) {
                    stato = T("ui.state.downed");
                    color = ColorWarning;
                }
                else if (inCombat) {
                    stato = T("ui.state.combat");
                    color = ColorSuccess;
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
                ImGui::TextColored(ColorWarning, "%s", T("ui.not_available"));
            }

            // === Risposta API grezza ===
            if (!LastServerResponse.empty()) {
                ImGui::Separator();
                ImGui::TextColored(ColorInfo, "%s", T("ui.server_raw_response"));
                ImGui::InputTextMultiline("##response",
                    (char*)LastServerResponse.c_str(),
                    LastServerResponse.size() + 1,
                    ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 6),
                    ImGuiInputTextFlags_ReadOnly);
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
