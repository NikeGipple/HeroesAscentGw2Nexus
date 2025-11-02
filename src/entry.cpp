#include <Windows.h>
#include <string>
#include "nexus/Nexus.h"
#include "mumble/Mumble.h"
#include "RTAPI/RTAPI.h"
#include "imgui/imgui.h"

/* === Globali === */
AddonDefinition AddonDef = {};
AddonAPI* APIDefs = nullptr;
Mumble::Data* MumbleLink = nullptr;
RealTimeData* RTAPIData = nullptr;

/* === Entry Point DLL === */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    return TRUE;
}

/* === Addon Definition === */
extern "C" __declspec(dllexport) AddonDefinition* GetAddonDef() {
    AddonDef.Signature = -987654321;
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "HeroesAscentGw2Nexus";
    AddonDef.Version.Major = 2;
    AddonDef.Version.Minor = 1;
    AddonDef.Version.Build = 0;
    AddonDef.Version.Revision = 0;
    AddonDef.Author = "NikeGipple";
    AddonDef.Description = "Real-time downed state detection via RTAPI";
    AddonDef.Flags = EAddonFlags_None;

    /* === Addon Load === */
    AddonDef.Load = [](AddonAPI* aApi) {
        APIDefs = aApi;

        // Setup ImGui context e allocatori
        ImGui::SetCurrentContext((ImGuiContext*)aApi->ImguiContext);
        ImGui::SetAllocatorFunctions(
            (void* (*)(size_t, void*))aApi->ImguiMalloc,
            (void(*)(void*, void*))aApi->ImguiFree);

        // Collega i DataLink
        MumbleLink = (Mumble::Data*)aApi->DataLink.Get("DL_MUMBLE_LINK");
        RTAPIData = (RealTimeData*)aApi->DataLink.Get(DL_RTAPI);

        // Log iniziale
        aApi->Log(ELogLevel_INFO, "HeroesAscent", "Addon caricato: RTAPI connesso, overlay attivo.");

        // === Renderer principale ===
        aApi->Renderer.Register(ERenderType_Render, []() {
            if (!APIDefs)
                return;

            // Se RTAPI non è ancora pronto, prova a ricollegarlo
            if (!RTAPIData)
                RTAPIData = (RealTimeData*)APIDefs->DataLink.Get(DL_RTAPI);

            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(460, 300), ImGuiCond_FirstUseEver);

            ImGui::Begin("HeroesAscent Downed Tracker (RTAPI)", nullptr,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings);

            // === Nome personaggio ===
            std::string playerName = "Nessun nome rilevato";
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

            ImGui::Text("Personaggio: %s", playerName.c_str());
            ImGui::Separator();

            // === Stato in tempo reale (RTAPI) ===
            if (RTAPIData && RTAPIData->GameBuild != 0) {
                uint32_t cs = RTAPIData->CharacterState;
                bool isAlive = cs & CS_IsAlive;
                bool isDowned = cs & CS_IsDowned;
                bool inCombat = cs & CS_IsInCombat;

                const char* stato = "Sconosciuto";
                ImVec4 color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

                if (isDowned && !isAlive) {
                    stato = "MORTO";
                    color = ImVec4(1, 0.3f, 0.3f, 1);
                }
                else if (isDowned) {
                    stato = "A TERRA (DOWNED)";
                    color = ImVec4(1, 0.6f, 0.1f, 1);
                }
                else if (inCombat) {
                    stato = "IN COMBATTIMENTO";
                    color = ImVec4(0.3f, 1, 0.3f, 1);
                }
                else {
                    stato = "NORMALE";
                }

                ImGui::TextColored(color, "Stato: %s", stato);


            }
            else {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "RTAPI non disponibile o non attiva...");
            }

            ImGui::End();
            });
        };

    /* === Addon Unload === */
    AddonDef.Unload = []() {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, "HeroesAscent", "Addon scaricato correttamente.");
        }
        };

    return &AddonDef;
}
