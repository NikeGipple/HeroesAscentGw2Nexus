#include <Windows.h>
#include <string>
#include <thread>
#include "Localization.h"
#include "Network.h"
#include "imgui/imgui.h"
#include "UIColors.h"
#include "RTAPI/RTAPI.h"

using namespace ImGui;

/* === Variabili globali principali === */
AddonDefinition AddonDef{};
AddonAPI* APIDefs = nullptr;
static RealTimeData* RTAPIData = nullptr;

/* === Variabili di test registrazione === */
extern std::string ApiKey;
extern std::string AccountToken;
extern std::string RegistrationStatus;
extern ImVec4 RegistrationColor;
void SendRegistration();

/* === Variabili stato server e violazioni === */
extern std::string ServerStatus;
extern ImVec4 ServerColor;
extern std::string LastViolationTitle;
extern std::string LastViolationDesc;
extern std::string LastServerResponse;

/* === Entry Point Addon === */
extern "C" __declspec(dllexport) AddonDefinition* GetAddonDef() {
    AddonDef.Signature = -987654321;
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "HeroesAscentGw2Nexus";
    AddonDef.Version.Major = 4;
    AddonDef.Version.Minor = 13;
    AddonDef.Author = "NikeGipple";
    AddonDef.Description = "Heroes Ascent Assistant";
    AddonDef.Flags = EAddonFlags_None;

    AddonDef.Load = [](AddonAPI* aApi) {
        APIDefs = aApi;

        // === Setup ImGui ===
        ImGui::SetCurrentContext((ImGuiContext*)aApi->ImguiContext);
        ImGui::SetAllocatorFunctions(
            (void* (*)(size_t, void*))aApi->ImguiMalloc,
            (void (*)(void*, void*))aApi->ImguiFree
        );

        // === Moduli principali ===
        InitLocalization(aApi);
        LoadLanguage(CurrentLang);
        LoadViolations(CurrentLang);
        InitNetwork(aApi);

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, "Network", "Calling CheckServerStatus() after InitNetwork...");
            CheckServerStatus(); 
        }
        else {
            OutputDebugStringA("[HeroesAscent] APIDefs is NULL — cannot perform initial /api/status check.\n");
        }

        RTAPIData = (RealTimeData*)aApi->DataLink.Get(DL_RTAPI);

        // === Renderer ===
        aApi->Renderer.Register(ERenderType_Render, []() {
            if (!APIDefs) return;
            if (!RTAPIData)
                RTAPIData = (RealTimeData*)APIDefs->DataLink.Get(DL_RTAPI);

            ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("HeroesAscent - Full Module", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

                /* === Selettore lingua === */
                ImGui::TextColored(ColorInfo, "%s:", T("ui.language"));
                ImGui::SameLine();
                static const char* langs[] = { "English", "Italiano" };
                static int currentLangIdx = (CurrentLang == "it") ? 1 : 0;
                if (ImGui::BeginCombo("##lang", langs[currentLangIdx])) {
                    for (int i = 0; i < IM_ARRAYSIZE(langs); i++) {
                        bool selected = (currentLangIdx == i);
                        if (ImGui::Selectable(langs[i], selected)) {
                            currentLangIdx = i;
                            CurrentLang = (i == 0) ? "en" : "it";
                            LoadLanguage(CurrentLang);
                            LoadViolations(CurrentLang);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::Separator();

                /* === Registrazione === */
                ImGui::TextColored(ColorInfo, "%s", T("ui.registration_title"));
                static char apiKeyBuf[128] = { 0 };
                static bool bufInit = false;
                if (!bufInit) {
                    strncpy_s(apiKeyBuf, sizeof(apiKeyBuf), ApiKey.c_str(), _TRUNCATE);
                    bufInit = true;
                }

                if (AccountToken.empty()) {
                    if (ImGui::InputText("##apikey", apiKeyBuf, IM_ARRAYSIZE(apiKeyBuf)))
                        ApiKey = apiKeyBuf;

                    ImGui::SameLine();
                    if (ImGui::Button(T("ui.registration_button"))) {
                        RegistrationStatus = "Sending...";
                        RegistrationColor = ImVec4(1, 1, 0, 1);
                        std::thread(SendRegistration).detach();
                    }

                    ImGui::SameLine();
                    ImGui::TextColored(RegistrationColor, "%s", RegistrationStatus.c_str());
                }
                else {
                    ImGui::TextColored(ColorSuccess, "%s", T("ui.registration_already"));
                    ImGui::Text("%s: %s", T("ui.registration_token"), AccountToken.c_str());
                }

                ImGui::Separator();

                /* === Stato server === */
                if (ServerStatus.empty()) {
                    ServerStatus = T("ui.checking_server"); 
                    ServerColor = ColorInfo;                  
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_INFO, "HeroesAscent", "Performing initial /api/status check");
                        CheckServerStatus();
                    }
                    else {
                        // Messaggio visibile nel log Nexus, anche se l'API non è inizializzata correttamente
                        printf("[HeroesAscent][WARNING] APIDefs is NULL — cannot perform initial /api/status check.\n");
                    }
                }

                ImGui::TextColored(ServerColor, "%s", ServerStatus.c_str());

                /* === Violazioni === */
                if (!LastViolationTitle.empty()) {
                    ImGui::Separator();
                    ImGui::TextColored(ColorError, "%s", LastViolationTitle.c_str());

                    if (!LastViolationDesc.empty())
                        ImGui::TextWrapped("%s", LastViolationDesc.c_str());
                    else
                        ImGui::TextWrapped("%s", T("ui.unknown_violation"));
                }

                ImGui::Separator();

                /* === Stato personaggio === */
                if (RTAPIData && RTAPIData->GameBuild != 0) {
                    const char* name = (RTAPIData->CharacterName && RTAPIData->CharacterName[0] != '\0')
                        ? RTAPIData->CharacterName : "N/A";

                    ImGui::Text("%s: %s", T("ui.character"), name);

                    uint32_t cs = RTAPIData->CharacterState;
                    bool isAlive = (cs & CS_IsAlive) != 0;
                    bool isDowned = (cs & CS_IsDowned) != 0;
                    bool inCombat = (cs & CS_IsInCombat) != 0;

                    const char* stato = T("ui.state.normal");
                    ImVec4 color = ColorGray;
                    if (!isAlive && isDowned) {
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

                    ImGui::TextColored(color, "%s: %s", T("ui.status"), stato);
                    ImGui::Text("%s: %u", T("ui.map"), RTAPIData->MapID);
                    ImGui::Text("%s: %.2f, %.2f, %.2f",
                        T("ui.position"),
                        RTAPIData->CharacterPosition[0],
                        RTAPIData->CharacterPosition[1],
                        RTAPIData->CharacterPosition[2]);
                }
                else {
                    ImGui::TextColored(ColorWarning, "%s", T("ui.not_available"));
                }

                /* === Risposta grezza server === */
                if (!LastServerResponse.empty()) {
                    ImGui::Separator();
                    ImGui::TextColored(ColorInfo, "%s", T("ui.server_raw_response"));
                    ImGui::InputTextMultiline("##response",
                        (char*)LastServerResponse.c_str(),
                        LastServerResponse.size() + 1,
                        ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 6),
                        ImGuiInputTextFlags_ReadOnly);
                }

                ImGui::Separator();
                ImGui::Text("Status OK - Localization + Registration + RTAPI + Violations active.");
            }
            ImGui::End();
            });
        };

    AddonDef.Unload = []() {
        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "HeroesAscent", "Addon unloaded (Full Module).");
        };

    return &AddonDef;
}
