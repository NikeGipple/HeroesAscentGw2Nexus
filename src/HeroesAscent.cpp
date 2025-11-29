// HeroesAscent.cpp
#include <Windows.h>
#include "imgui/imgui.h"
#include "RTAPI/RTAPI.h"
#include <string>
#include <thread>
#include "Localization.h"
#include "Network.h"
#include "UIColors.h"
#include "PlayerData.h"
#include "Globals.h"
#include <ArcIntegration.h>
#include "PlayerEventType.h"

using namespace ImGui;

/* === Variabili globali principali === */
AddonDefinition AddonDef{};


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

        //LoadLanguage(CurrentLang);
        //LoadViolations(CurrentLang);

        InitNetwork(aApi);
        InitArcIntegration(aApi);

        AccountToken = LoadAccountToken();
        if (!AccountToken.empty()) {
            RegistrationStatus = T("ui.registration_already");
            RegistrationColor = ColorSuccess;

            static bool tokenCheckDone = false;

            aApi->Renderer.Register(ERenderType_Render, []() {
                if (!RTAPIData || RTAPIData->AccountName[0] == '\0') return;
                static bool tokenChecked = false;
                if (!tokenChecked) {
                    std::thread(CheckAccountToken).detach();
                    tokenChecked = true;
                }
                });

        }


        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, "Network", "Calling CheckServerStatus() after InitNetwork...");
            CheckServerStatus(); 
        }
        else {
            OutputDebugStringA("[HeroesAscent] APIDefs is NULL — cannot perform initial /api/status check.\n");
        }

        // === Renderer ===
        aApi->Renderer.Register(ERenderType_Render, []() {
            if (!APIDefs) return;
            if (!RTAPIData)
                RTAPIData = (RealTimeData*)APIDefs->DataLink.Get(DL_RTAPI);

            static uint64_t lastTick = 0;
            static RealTimeData lastSnapshot{};
            static bool snapshotInit = false;
            static bool firstLoginSent = false;
            static std::string lastCharacterName = "";

            uint64_t now = GetTickCount64();

            if (now - lastTick >= 200) {
                lastTick = now;

                if (RTAPIData && RTAPIData->GameBuild != 0) {
                    if (!snapshotInit) {
                        lastSnapshot = *RTAPIData;
                        snapshotInit = true;
                    }

                    // Stato attuale
                    const uint32_t csNow = RTAPIData->CharacterState;
                    const bool nowAlive = (csNow & CS_IsAlive) != 0;
                    const bool nowDowned = (csNow & CS_IsDowned) != 0;
                    const bool nowDead = (!nowAlive && !nowDowned);
                    const uint32_t mapNow = RTAPIData->MapID;
                    const uint32_t mountNow = RTAPIData->MountIndex;
                    const bool glidingNow = (csNow & CS_IsGliding) != 0;
                    const uint32_t levelNow = RTAPIData->CharacterLevel;
                    const uint32_t groupNow = RTAPIData->GroupType;
                    const uint32_t groupcountNow = RTAPIData->GroupMemberCount;

                    // Stato precedente
                    const uint32_t csPrev = lastSnapshot.CharacterState;
                    const bool prevAlive = (csPrev & CS_IsAlive) != 0;
                    const bool prevDowned = (csPrev & CS_IsDowned) != 0;
                    const bool prevDead = (!prevAlive && !prevDowned);
                    const uint32_t mapPrev = lastSnapshot.MapID;
                    const uint32_t mountPrev = lastSnapshot.MountIndex;
                    const bool glidingPrev = (csPrev & CS_IsGliding) != 0;
                    const uint32_t levelPrev = lastSnapshot.CharacterLevel;
                    const uint32_t groupPrev = lastSnapshot.GroupType;
                    const uint32_t groupcountPrev = lastSnapshot.GroupMemberCount;


                    // Nome corrente del personaggio 
                    const std::string currentName =
                        (RTAPIData->CharacterName && RTAPIData->CharacterName[0] != '\0')
                        ? RTAPIData->CharacterName
                        : "";

                    // === LOGIN  ===
                    if (RTAPIData->GameState == GS_CharacterSelection ||
                        RTAPIData->GameState == GS_CharacterCreation)
                    {
                        firstLoginSent = false;
                    }

                    if (!firstLoginSent &&
                        RTAPIData->GameState == GS_Gameplay &&
                        !currentName.empty())
                    {
                        SendPlayerUpdate(PlayerEventType::LOGIN);
                        firstLoginSent = true;
                        lastCharacterName = currentName;
                        lastSnapshot = *RTAPIData;
                    }


                    // === DOWNED ===
                    else if (nowDowned && !prevDowned) {
                        SendPlayerUpdate(PlayerEventType::DOWNED);
                        lastSnapshot.CharacterState = csNow;
                    }
                    // === DEAD ===
                    else if (nowDead && !prevDead) {
                        SendPlayerUpdate(PlayerEventType::DEAD);
                        lastSnapshot.CharacterState = csNow;
                    }
                    // === RESPAWN ===
                    else if (nowAlive && !prevAlive) {
                        SendPlayerUpdate(PlayerEventType::RESPAWN);
                        lastSnapshot.CharacterState = csNow;
                    }
                    // === MAP CHANGED ===
                    else if (mapNow != mapPrev) {
                        SendPlayerUpdate(PlayerEventType::MAP_CHANGED);
                        lastSnapshot.MapID = mapNow;
                    }

                    // === MOUNT CHANGED ===
                    else if (mountNow != mountPrev) {
                        SendPlayerUpdate(PlayerEventType::MOUNT_CHANGED);
                        lastSnapshot.MountIndex = mountNow;
                    }
                    else if (levelNow != levelPrev && levelNow > 0) {
                        SendPlayerUpdate(PlayerEventType::LEVEL_UP);
                        lastSnapshot.CharacterLevel = levelNow;
                    }
                    // === GROUP  ===
                    else if ((groupNow != 0 && groupPrev == 0) || (groupcountNow != groupcountPrev)) {
                        SendPlayerUpdate(PlayerEventType::GROUP);
                        lastSnapshot.GroupType = groupNow;
                        lastSnapshot.GroupMemberCount = groupcountNow;
                    }
                    // === Gliding ===
                    else if (glidingNow && !glidingPrev) {
                        SendPlayerUpdate(PlayerEventType::GLIDING);
                        lastSnapshot.CharacterState = csNow;
                    }
                }
            }

            ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("HeroesAscent Assistant", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

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
                            CheckServerStatus();

                            if (!LastViolationCode.empty() && Violations.find(LastViolationCode) != Violations.end()) {
                                LastViolationTitle = Violations[LastViolationCode].first;
                                LastViolationDesc = Violations[LastViolationCode].second;
                            }
                            // === Ritraduzione delle violazioni senza codice ===
                            else if (LastViolationType == ViolationType::CharacterNotFound) {
                                LastViolationTitle = T("ui.character_not_found_title");
                                LastViolationDesc = T("ui.character_not_found_desc");
                            }
                            else if (LastViolationType == ViolationType::GenericViolation) {
                                LastViolationTitle = T("ui.unknown_violation_title");
                                LastViolationDesc = T("ui.unknown_violation_desc");
                            }

                            if (ServerStatus == T("ui.violation_detected") || ServerStatus == "Violation detected") {
                                ServerStatus = T("ui.violation_detected");
                            }
                            else if (ServerStatus == T("ui.rules_respected") || ServerStatus == "Rules respected") {
                                ServerStatus = T("ui.rules_respected");
                            }
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::Separator();

                /* === Registrazione === */
                ImGui::TextColored(ColorInfo, "%s", T("ui.registration_title"));

                // Sempre mostrare lo stato della registrazione
                if (!AccountToken.empty()) {
                    ImGui::TextColored(ColorSuccess, "%s", T("ui.registration_already"));
                    ImGui::Text("%s: %s", T("ui.registration_token"), AccountToken.c_str());
                }
                else {
                    ImGui::TextColored(RegistrationColor, "%s", RegistrationStatus.c_str());
                }

                // Controllo se siamo nella schermata di selezione/creazione personaggio
                bool isAtCharacterSelect =
                    (RTAPIData &&
                        RTAPIData->GameState == GS_CharacterSelection);

                // SOLO qui mostro il campo input
                if (AccountToken.empty() && isAtCharacterSelect)
                {
                    static char apiKeyBuf[128] = { 0 };
                    static bool bufInit = false;
                    if (!bufInit) {
                        strncpy_s(apiKeyBuf, sizeof(apiKeyBuf), ApiKey.c_str(), _TRUNCATE);
                        bufInit = true;
                    }

                    ImVec2 inputPos = ImGui::GetCursorScreenPos();
                    ImGui::SetNextItemWidth(250);

                    bool inputActive = ImGui::InputText("##apikey", apiKeyBuf, IM_ARRAYSIZE(apiKeyBuf));
                    if (inputActive)
                        ApiKey = apiKeyBuf;

                    if (strlen(apiKeyBuf) == 0 && !ImGui::IsItemActive()) {
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        drawList->AddText(ImVec2(inputPos.x + 8, inputPos.y + 3),
                            ImColor(ColorGray),
                            T("ui.registration_placeholder"));
                    }

                    ImGui::SameLine();
                    if (ImGui::Button(T("ui.registration_button"))) {
                        RegistrationStatus = T("ui.registration_sending");
                        RegistrationColor = ColorInfo;
                        std::thread(SendRegistration).detach();
                    }
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
                }

                ImGui::TextColored(ServerColor, "%s", ServerStatus.c_str());

                /* === Stato del personaggio === */
                if (!CharacterStatus.empty()) {
                    std::string key = "ui.character_" + CharacterStatus;
                    ImGui::TextColored(CharacterColor, "%s: %s", T("ui.character_status"), T(key.c_str()));
                }

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
                        RTAPIData->CharacterPosition[2],
                        RTAPIData->CharacterPosition[1]);
                }
                else {
                    ImGui::TextColored(ColorWarning, "%s", T("ui.not_available"));
                }

                /* === Risposta grezza server === */
                ImGui::Separator();
                ImGui::TextColored(ColorInfo, "%s", T("ui.server_raw_response"));

                // Se la risposta è vuota, mostriamo un placeholder
                std::string displayResponse = LastServerResponse.empty()
                    ? "[Waiting for server response...]"
                    : LastServerResponse;

                ImGui::InputTextMultiline("##response",
                    (char*)displayResponse.c_str(),
                    displayResponse.size() + 1,
                    ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 6),
                    ImGuiInputTextFlags_ReadOnly);


                ImGui::Separator();
                ImGui::Text("version 0.19");
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
