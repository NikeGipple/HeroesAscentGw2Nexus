#include <Windows.h>
#include <string>
#include <sstream>
#include <cstdint>
#include "nexus/Nexus.h"
#include "mumble/Mumble.h"
#include "imgui/imgui.h"

/* === STRUCT ARC COMBAT === */

#pragma pack(push, 1)
struct cbtevent {
    uint64_t time;
    uint64_t src_agent;
    uint64_t dst_agent;
    int32_t value;
    int32_t buff_dmg;
    uint32_t overstack_value;
    uint32_t skillid;
    uint16_t src_instid;
    uint16_t dst_instid;
    uint16_t src_master_instid;
    uint16_t dst_master_instid;
    uint8_t iff;
    uint8_t buff;
    uint8_t result;
    uint8_t is_activation;
    uint8_t is_buffremove;
    uint8_t is_ninety;
    uint8_t is_fifty;
    uint8_t is_moving;
    uint8_t is_statechange;
    uint8_t is_flanking;
    uint8_t is_shields;
    uint8_t is_offcycle;
    uint8_t pad61;
    uint8_t pad62;
    uint8_t pad63;
    uint8_t pad64;
};
#pragma pack(pop)

struct ag {
    char* name;
    uintptr_t id;
    uint32_t prof;
    uint32_t elite;
    uint32_t self;
    uint16_t team;
};

struct EvCombatData {
    cbtevent* ev;
    ag* src;
    ag* dst;
    char* skillname;
    uint64_t id;
    uint64_t revision;
};

/* === GLOBALI === */
AddonDefinition AddonDef = {};
AddonAPI* APIDefs = nullptr;
Mumble::Data* MumbleLink = nullptr;
NexusLinkData* NexusLink = nullptr;
HMODULE hSelf = nullptr;

static bool g_IsDowned = false;
static bool g_IsDead = false;
static bool g_IsInCombat = false;

/* === CALLBACK COMBAT === */
static void OnCombatEvent(bool isLocal, void* aEventArgs) {
    if (!aEventArgs || !APIDefs) return;
    EvCombatData* cbtEv = (EvCombatData*)aEventArgs;
    if (!cbtEv->ev) return;

    cbtevent* ev = cbtEv->ev;

    if (cbtEv->src && cbtEv->src->self == 1) {
        switch (ev->is_statechange) {
        case 1: // enter combat
            g_IsInCombat = true;
            break;
        case 2: // exit combat
            g_IsInCombat = false;
            break;
        case 5: // downed
            g_IsDowned = true;
            g_IsDead = false;
            break;
        case 4: // dead
            g_IsDowned = false;
            g_IsDead = true;
            break;
        case 3: // revived
        case 6: // respawn
            g_IsDowned = false;
            g_IsDead = false;
            break;
        default:
            break;
        }
    }
}

static void OnCombatLocal(void* aEventArgs) { OnCombatEvent(true, aEventArgs); }
static void OnCombatSquad(void* aEventArgs) { OnCombatEvent(false, aEventArgs); }

/* === DLLMAIN === */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) hSelf = hModule;
    return TRUE;
}

/* === GET ADDON DEF === */
extern "C" __declspec(dllexport) AddonDefinition* GetAddonDef() {
    AddonDef.Signature = -987654321;
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "HeroesAscentGw2Nexus";
    AddonDef.Version.Major = 1;
    AddonDef.Version.Minor = 10;
    AddonDef.Version.Build = 0;
    AddonDef.Version.Revision = 0;
    AddonDef.Author = "NikeGipple";
    AddonDef.Description = "ArcDPS combat status overlay (downed detection)";
    AddonDef.Flags = EAddonFlags_None;

    AddonDef.Load = [](AddonAPI* aApi) {
        APIDefs = aApi;
        ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
        ImGui::SetAllocatorFunctions(
            (void* (*)(size_t, void*))APIDefs->ImguiMalloc,
            (void(*)(void*, void*))APIDefs->ImguiFree);

        MumbleLink = (Mumble::Data*)APIDefs->DataLink.Get("DL_MUMBLE_LINK");
        NexusLink = (NexusLinkData*)APIDefs->DataLink.Get("DL_NEXUS_LINK");

        aApi->Events.Subscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", OnCombatLocal);
        aApi->Events.Subscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW", OnCombatSquad);

        aApi->Renderer.Register(ERenderType_Render, []() {
            if (!APIDefs) return;

            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);

            ImGui::Begin("HeroesAscent Status Overlay", nullptr,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings);

            std::string playerName = "Nessun nome rilevato";
            if (MumbleLink && MumbleLink->Identity && wcslen(MumbleLink->Identity) > 0) {
                const wchar_t* p = wcsstr(MumbleLink->Identity, L"\"name\":\"");
                if (p) {
                    p += 8;
                    const wchar_t* end = wcschr(p, L'"');
                    if (end) {
                        std::wstring wName(p, end - p);
                        char nameUtf8[256];
                        WideCharToMultiByte(CP_UTF8, 0, wName.c_str(), -1, nameUtf8, sizeof(nameUtf8), nullptr, nullptr);
                        playerName = nameUtf8;
                    }
                }
            }

            ImGui::Text("Nome: %s", playerName.c_str());
            ImGui::Separator();

            // Stato visivo colorato
            ImVec4 color;
            const char* stato;
            if (g_IsDead) {
                color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                stato = "MORTO";
            }
            else if (g_IsDowned) {
                color = ImVec4(1.0f, 0.6f, 0.1f, 1.0f);
                stato = "A TERRA (DOWNED)";
            }
            else if (g_IsInCombat) {
                color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                stato = "IN COMBATTIMENTO";
            }
            else {
                color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                stato = "FUORI COMBATTIMENTO";
            }

            ImGui::TextColored(color, "Stato: %s", stato);

            ImGui::End();
            });
        };

    AddonDef.Unload = []() {
        if (APIDefs) {
            APIDefs->Renderer.Deregister(nullptr);
            APIDefs->Events.Unsubscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", OnCombatLocal);
            APIDefs->Events.Unsubscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW", OnCombatSquad);
        }
        };

    return &AddonDef;
}
