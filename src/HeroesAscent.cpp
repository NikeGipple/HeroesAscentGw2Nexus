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
bool FirstLoginSent = false;

/* === Variabili di  registrazione === */
extern std::string ApiKey;
extern std::string AccountToken;
extern std::string RegistrationStatus;
extern ImVec4 RegistrationColor;
void SendRegistration();

bool WelcomeScreenShown = false;

/* === Variabili stato server e violazioni === */
extern std::string ServerStatus;
extern ImVec4 ServerColor;
extern std::string LastViolationTitle;
extern std::string LastViolationDesc;
extern std::string LastServerResponse;

// Helper: controlla se il personaggio è morto
inline bool IsDead(const RealTimeData* d)
{
    const bool alive = (d->CharacterState & CS_IsAlive) != 0;
    const bool downed = (d->CharacterState & CS_IsDowned) != 0;

    // TRUE if ANY non-alive state
    return (!alive);
}


static void ResetSelectedCharacterState(
    std::string& lastCharacterName,
    RealTimeData& lastSnapshot,
    bool& snapshotInit)
{
    std::scoped_lock lk(gStateMx);

    CharacterStatus.clear();
    CharacterColor = ColorGray;
    FirstLoginSent = false;
    LoginDeadCheckPending = false;
    PlayerBelow50HP = false;
    lastCharacterName.clear();
    snapshotInit = false;
    lastSnapshot = *RTAPIData;
}

static void DrawLanguageSelector(const char* comboId, float width)
{
    static const char* langShort[] = { "EN", "IT" };
    static const char* langCodes[] = { "en", "it" };

    // CurrentLang è la verità: niente static langIdx qui, così non si desincronizza
    int langIdx = (CurrentLang == "it") ? 1 : 0;

    ImGui::SetNextItemWidth(width);

    if (ImGui::BeginCombo(comboId, langShort[langIdx])) {
        for (int i = 0; i < 2; ++i) {
            bool selected = (i == langIdx);

            if (ImGui::Selectable(langShort[i], selected)) {
                CurrentLang = langCodes[i];

                LoadLanguage(CurrentLang);
                LoadViolations(CurrentLang);
                CheckServerStatus();
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}


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

        std::string loadedToken = LoadAccountToken();
        bool shouldRegisterTokenCheck = false;

        {
            std::scoped_lock lk(gStateMx);
            AccountToken = loadedToken;

            if (!AccountToken.empty()) {
                RegistrationStatus = T("ui.registration_already");
                RegistrationColor = ColorSuccess;
                shouldRegisterTokenCheck = true;
            }
        }

        if (shouldRegisterTokenCheck) {
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
            static std::string lastCharacterName = "";

            uint64_t now = GetTickCount64();

            if (now - lastTick >= 200) {
                lastTick = now;

                if (RTAPIData && RTAPIData->GameBuild != 0) {

                    // === RESET AUTOMATICO DEL FLAG HP<50% DOPO 3 SECONDI ===
                    if (PlayerBelow50HP) {
                        uint64_t now2 = GetTickCount64();
                        if (now2 - PlayerBelow50HP_Time >= 3000) {
                            PlayerBelow50HP = false;
                        }
                    }

                    // Inizializza snapshot
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


                    // === LOGIN ===
                    if (!FirstLoginSent &&
                        RTAPIData->GameState == GS_Gameplay &&
                        !currentName.empty())
                    {
                        SendPlayerUpdate(PlayerEventType::LOGIN);
                        FirstLoginSent = true;
                        lastCharacterName = currentName;
                        lastSnapshot = *RTAPIData;

                        // Attiva il controllo morte post-login
                        LoginDeadCheckPending = true;

                        return;
                    }

                    // === DEAD AL LOGIN (frame successivo) ===
                    if (LoginDeadCheckPending) {
                        LoginDeadCheckPending = false;

                        if (IsDead(RTAPIData)) {
                            SendPlayerUpdate(PlayerEventType::DEAD);
                        }

                    }

                    // === LOGOUT ===
                    if (FirstLoginSent &&
                        lastSnapshot.GameState == GS_Gameplay &&
                        RTAPIData->GameState == GS_CharacterSelection)
                    {
                        SendPlayerUpdate(PlayerEventType::LOGOUT);

                        FirstLoginSent = false;
                        lastCharacterName = "";

                        {
                            std::scoped_lock lk(gStateMx);
                            CharacterStatus.clear();
                            CharacterColor = ColorGray;
                            LastViolationTitle.clear();
                            LastViolationDesc.clear();
                        }

                        lastSnapshot = *RTAPIData;
                        return;
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

                    // === LEVEL  ===
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

            // --- snapshot thread-safe per UI ---
            std::string serverStatusLocal, tokenLocal, regStatusLocal, apiKeyLocal;
            std::string violTitleLocal, violDescLocal, lastRespLocal, charStatusLocal;
            ImVec4 serverColorLocal, regColorLocal, charColorLocal;

            {
                std::scoped_lock lk(gStateMx);
                serverStatusLocal = ServerStatus;
                serverColorLocal = ServerColor;

                tokenLocal = AccountToken;

                regStatusLocal = RegistrationStatus;
                regColorLocal = RegistrationColor;

                apiKeyLocal = ApiKey;

                violTitleLocal = LastViolationTitle;
                violDescLocal = LastViolationDesc;

                lastRespLocal = LastServerResponse;

                charStatusLocal = CharacterStatus;
                charColorLocal = CharacterColor;
            }

            if (ImGui::Begin("HeroesAscent Assistant", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

                // --- modal WELCOME start ---
                static bool wasAtCharacterSelect = false;
                static bool dismissedThisVisit = false;

                const bool isAtCharacterSelect = (RTAPIData && RTAPIData->GameState == GS_CharacterSelection);
                const bool notRegistered = tokenLocal.empty();

                // Edge: quando ENTRO nel char select, resetto lo stato della visita
                if (isAtCharacterSelect && !wasAtCharacterSelect) {
                    dismissedThisVisit = false;
                }

                // Aggiorno edge detector
                wasAtCharacterSelect = isAtCharacterSelect;

                // Condizione: mostra finché non è stato dismissato in questa visita
                if (isAtCharacterSelect && notRegistered && !dismissedThisVisit) {

                    ImVec2 display = ImGui::GetIO().DisplaySize;

                    auto ClampF = [](float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; };

                    const float minW = 720.0f;
                    const float minH = 300.0f;
                    const float maxW = display.x * 0.90f;
                    const float maxH = display.y * 0.80f;

                    float w = ClampF(display.x * 0.55f, minW, maxW);
                    float h = ClampF(display.y * 0.35f, minH, maxH);

                    // Limite aspect ratio per non esplodere su 21:9
                    const float maxAspect = 1.85f; // 16:9 = 1.78, quindi qui resta generosa ma non "panoramica"
                    if (w / h > maxAspect)
                        w = h * maxAspect;

                    // se dopo il cap la width scende sotto minW, alza un filo l'altezza
                    if (w < minW) {
                        w = minW;
                        h = ClampF(w / maxAspect, minH, maxH);
                    }

                    ImVec2 popupSize(w, h);

                    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                    ImGui::SetNextWindowSize(popupSize, ImGuiCond_Always);

                    ImGui::OpenPopup("###HA_Welcome");
                    std::string popupTitle = std::string(T("ui.welcome_title")) + "###HA_Welcome";

                    ImGuiWindowFlags flags =
                        ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse;

                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));

                    if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, flags))
                    {
                        const float footerReserve =
                            ImGui::GetTextLineHeightWithSpacing() +
                            ImGui::GetFrameHeightWithSpacing() +
                            6.0f;

                        // language selector 
                        {
                            const float comboW = 60.0f;

                            float y = ImGui::GetCursorPosY();
                            float x = ImGui::GetWindowContentRegionMax().x - comboW;

                            ImGui::SetCursorPos(ImVec2(x, y));
                            DrawLanguageSelector("##langselector_popup", comboW);

                            // IMPORTANT: scendo sotto l'altezza del combo e torno a sinistra
                            ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x,
                                y + ImGui::GetFrameHeightWithSpacing()));
                        }

                        // --- buffer input SOLO per popup ---
                        static char apiKeyBuf[128] = { 0 };
                        static bool bufInit = false;
                        static bool popupWasOpen = false;

                        // Detect open edge: quando il popup si apre, inizializzo buffer da ApiKey
                        const bool popupIsOpen = ImGui::IsPopupOpen("###HA_Welcome");
                        if (popupIsOpen && !popupWasOpen) {
                            bufInit = false; // forza reload
                        }
                        popupWasOpen = popupIsOpen;

                        ImGui::BeginChild("##welcome_body", ImVec2(0, -footerReserve), false);

                        ImGui::TextWrapped("%s", T("ui.registration_welcome_charselect"));
                        ImGui::Dummy(ImVec2(0, 8));

                        // Stato registrazione (sempre visibile nel popup)
                        // ImGui::TextColored(ColorInfo, "%s", T("ui.registration_title"));
                        // ImGui::TextColored(regColorLocal, "%s", regStatusLocal.c_str());
                        // ImGui::Dummy(ImVec2(0, 6));

                        // Input + bottone SOLO se non registrato 
                        if (tokenLocal.empty())
                        {
                            if (!bufInit) {
                                strncpy_s(apiKeyBuf, sizeof(apiKeyBuf), apiKeyLocal.c_str(), _TRUNCATE);
                                bufInit = true;
                            }

                            // calcolo larghezza bottone in base al testo + padding
                            const char* btnLabel = T("ui.registration_button");
                            const char* phLabel = T("ui.registration_placeholder");

                            const ImGuiStyle& st = ImGui::GetStyle();
                            float btnW = ImGui::CalcTextSize(btnLabel).x + st.FramePadding.x * 2.0f;

                            // spazio disponibile nella riga
                            float avail = ImGui::GetContentRegionAvail().x;
                            float inputW = avail - btnW - st.ItemSpacing.x;
                            if (inputW < 200.0f) inputW = 200.0f;

                            ImVec2 inputPos = ImGui::GetCursorScreenPos();
                            ImGui::SetNextItemWidth(inputW);

                            bool inputActive = ImGui::InputText("##apikey_popup", apiKeyBuf, IM_ARRAYSIZE(apiKeyBuf));
                            if (inputActive) {
                                std::scoped_lock lk(gStateMx);
                                ApiKey = apiKeyBuf;
                            }

                            // placeholder se vuoto e non attivo
                            if (strlen(apiKeyBuf) == 0 && !ImGui::IsItemActive()) {
                                ImDrawList* drawList = ImGui::GetWindowDrawList();
                                drawList->AddText(ImVec2(inputPos.x + 8, inputPos.y + 3), ImColor(ColorGray), phLabel);
                            }

                            ImGui::SameLine();

                            if (ImGui::Button(btnLabel, ImVec2(btnW, 0)))
                            {
                                regStatusLocal = T("ui.registration_sending");
                                regColorLocal = ColorInfo;

                                {
                                    std::scoped_lock lk(gStateMx);
                                    RegistrationStatus = regStatusLocal;
                                    RegistrationColor = regColorLocal;
                                }

                                std::thread(SendRegistration).detach();
                            }

                            // --- FEEDBACK sotto input 
                            {
                                ImGui::Dummy(ImVec2(0, 6));
                                ImGui::TextDisabled("%s", T("ui.server_raw_response"));

                                // Messaggio effettivo (colorato come già fai)
                                if (!regStatusLocal.empty()) {
                                    ImGui::TextColored(regColorLocal, "%s", regStatusLocal.c_str());
                                }
                            }

                        }

                        ImGui::EndChild();

                        // ----- FOOTER -----
                        ImGui::Separator();
                        ImGui::Dummy(ImVec2(0.0f, 6.0f));

                        const char* skipLabel = T("ui.skip");
                        const ImGuiStyle& st = ImGui::GetStyle();

                        // larghezza ideale in base al testo (più padding)
                        float btnW = ImGui::CalcTextSize(skipLabel).x + st.FramePadding.x * 2.0f;

                        // (opzionale) minimo estetico
                        btnW = (btnW < 140.0f) ? 140.0f : btnW;

                        // allineamento a destra nello spazio disponibile
                        float avail = ImGui::GetContentRegionAvail().x;
                        float x = ImGui::GetCursorPosX() + (avail - btnW);
                        if (x < ImGui::GetCursorPosX()) x = ImGui::GetCursorPosX();
                        ImGui::SetCursorPosX(x);

                        if (ImGui::Button(skipLabel, ImVec2(btnW, 0))) {
                            dismissedThisVisit = true;
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::Dummy(ImVec2(0.0f, 6.0f));
                        ImGui::EndPopup();
                    }

                    ImGui::PopStyleVar(3);
                }
                /* === modal end === */

                /* === HEADER === */

                // Imposta inizio riga
                ImGui::BeginGroup();

                /* === Stato server === */
                if (serverStatusLocal.empty()) {
                    serverStatusLocal = T("ui.checking_server");
                    serverColorLocal = ColorInfo;

                    {
                        std::scoped_lock lk(gStateMx);
                        ServerStatus = serverStatusLocal;
                        ServerColor = serverColorLocal;
                    }

                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_INFO, "HeroesAscent", "Performing initial /api/status check");
                        CheckServerStatus();
                    }
                }

                ImGui::TextColored(serverColorLocal, "%s", serverStatusLocal.c_str());

                /* === Stato del personaggio === */
                if (!charStatusLocal.empty()) {
                    std::string key = "ui.character_" + charStatusLocal;
                    ImGui::TextColored(charColorLocal, "%s", T(key.c_str()));
                    /*ImGui::TextColored(CharacterColor, "%s: %s", T("ui.character_status"), T(key.c_str()));*/
                }

                ImGui::EndGroup();

                /* === Selettore lingua === */

                ImGui::SameLine();
                const float comboW = 60.0f;
                float rightEdge = ImGui::GetWindowContentRegionMax().x;
                ImGui::SetCursorPosX(rightEdge - comboW);
                DrawLanguageSelector("##langselector_main", comboW);

                ImGui::Dummy(ImVec2(0, 4));
                ImGui::Separator();

                /* === Registrazione === */
                ImGui::TextColored(ColorInfo, "%s", T("ui.registration_title"));

                // Sempre mostrare lo stato della registrazione
                if (!tokenLocal.empty()) {
                    ImGui::TextColored(ColorSuccess, "%s", T("ui.registration_already"));
                    ImGui::Text("%s: %s", T("ui.registration_token"), tokenLocal.c_str());
                }
                else {
                    ImGui::TextColored(regColorLocal, "%s", regStatusLocal.c_str());
                }


                // SOLO qui mostro il campo input
                /*
                if (tokenLocal.empty() && isAtCharacterSelect)
                {
                    static char apiKeyBuf[128] = { 0 };
                    static bool bufInit = false;
                    if (!bufInit) {
                        strncpy_s(apiKeyBuf, sizeof(apiKeyBuf), apiKeyLocal.c_str(), _TRUNCATE);
                        bufInit = true;
                    }

                    ImVec2 inputPos = ImGui::GetCursorScreenPos();
                    ImGui::SetNextItemWidth(250);

                    bool inputActive = ImGui::InputText("##apikey", apiKeyBuf, IM_ARRAYSIZE(apiKeyBuf));
                    if (inputActive) {
                        std::scoped_lock lk(gStateMx);
                        ApiKey = apiKeyBuf;
                    }

                    if (strlen(apiKeyBuf) == 0 && !ImGui::IsItemActive()) {
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        drawList->AddText(ImVec2(inputPos.x + 8, inputPos.y + 3),
                            ImColor(ColorGray),
                            T("ui.registration_placeholder"));
                    }

                    ImGui::SameLine();
                    if (ImGui::Button(T("ui.registration_button"))) {

                        regStatusLocal = T("ui.registration_sending");
                        regColorLocal = ColorInfo;

                        {
                            std::scoped_lock lk(gStateMx);
                            RegistrationStatus = regStatusLocal;
                            RegistrationColor = regColorLocal;
                        }

                        std::thread(SendRegistration).detach();
                    }
                }
                */

                /* === Violazioni === */
                if (!violTitleLocal.empty()) {
                    ImGui::Separator();
                    ImGui::TextColored(ColorError, "%s", violTitleLocal.c_str());

                    if (!violDescLocal.empty())
                        ImGui::TextWrapped("%s", violDescLocal.c_str());
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
                std::string displayResponse = lastRespLocal.empty()
                    ? "[Waiting for server response...]"
                    : lastRespLocal;

                ImGui::InputTextMultiline("##response",
                    (char*)displayResponse.c_str(),
                    displayResponse.size() + 1,
                    ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 6),
                    ImGuiInputTextFlags_ReadOnly);


                ImGui::Separator();
                ImGui::Text("version 0.23");
            }
            ImGui::End();
            });
        };

    AddonDef.Unload = []() {
        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "HeroesAscent", "Addon unloaded (Full Module).");

        bool firstLoginSentLocal = false;
        bool below50Local = false;

        {
            std::scoped_lock lk(gStateMx);
            firstLoginSentLocal = FirstLoginSent;
            below50Local = PlayerBelow50HP;
        }

        if (firstLoginSentLocal) {
            if (below50Local) {
                SendPlayerUpdate(PlayerEventType::LOGOUT_LOW_HP);
            }
            else {
                SendPlayerUpdate(PlayerEventType::LOGOUT);
            }

            {
                std::scoped_lock lk(gStateMx);
                FirstLoginSent = false;
            }
        }
        };


    return &AddonDef;
}
