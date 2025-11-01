#include <Windows.h>
#include <string>
#include "nexus/Nexus.h"
#include "mumble/Mumble.h"
#include "imgui/imgui.h"

/* Prototipi */
void AddonLoad(AddonAPI* aApi);
void AddonUnload();
void AddonRender();

/* Globali */
AddonDefinition AddonDef = {};
AddonAPI* APIDefs = nullptr;
Mumble::Data* MumbleLink = nullptr;
NexusLinkData* NexusLink = nullptr;
HMODULE hSelf = nullptr;

/* Entry Point DLL */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH)
        hSelf = hModule;
    return TRUE;
}

/* Definizione Addon */
extern "C" __declspec(dllexport) AddonDefinition* GetAddonDef() {
    AddonDef.Signature = -987654321;
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "HeroesAscentGw2Nexus";
    AddonDef.Version.Major = 1;
    AddonDef.Version.Minor = 3;
    AddonDef.Version.Build = 0;
    AddonDef.Version.Revision = 0;
    AddonDef.Author = "NikeGipple";
    AddonDef.Description = "Heroes Ascent Overlay Test";
    AddonDef.Load = AddonLoad;
    AddonDef.Unload = AddonUnload;
    AddonDef.Flags = EAddonFlags_None;
    return &AddonDef;
}

/* Addon Load */
void AddonLoad(AddonAPI* aApi) {
    APIDefs = aApi;

    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions(
        (void* (*)(size_t, void*))APIDefs->ImguiMalloc,
        (void(*)(void*, void*))APIDefs->ImguiFree
    );

    MumbleLink = (Mumble::Data*)APIDefs->DataLink.Get("DL_MUMBLE_LINK");
    NexusLink = (NexusLinkData*)APIDefs->DataLink.Get("DL_NEXUS_LINK");

    // Registriamo il renderer subito
    APIDefs->Renderer.Register(ERenderType_Render, AddonRender);
}

/* Addon Unload */
void AddonUnload() {
    if (APIDefs)
        APIDefs->Renderer.Deregister(AddonRender);
}

/* Render principale */
void AddonRender() {
    if (!APIDefs)
        return;

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, 260), ImGuiCond_FirstUseEver);

    ImGui::Begin("HeroesAscent Overlay", nullptr,
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings);

    std::string playerName = "Nessun nome rilevato";
    std::string rawJson = "(vuoto)";

    if (MumbleLink && MumbleLink->Identity && wcslen(MumbleLink->Identity) > 0) {
        // Convertiamo il JSON grezzo
        char jsonUtf8[2048];
        WideCharToMultiByte(CP_UTF8, 0, MumbleLink->Identity, -1, jsonUtf8, sizeof(jsonUtf8), nullptr, nullptr);
        rawJson = jsonUtf8;

        // Estrazione semplice del nome
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

    ImGui::Text("HeroesAscent Overlay attivo");
    ImGui::Separator();
    ImGui::Text("Nome del personaggio: %s", playerName.c_str());
    ImGui::Text("Movimento: %s", (NexusLink && NexusLink->IsMoving) ? "In movimento" : "Fermo");


    if (MumbleLink) {
        const float* pos = reinterpret_cast<const float*>(&MumbleLink->AvatarPosition);
        ImGui::Text("Posizione: X %.2f | Y %.2f | Z %.2f", pos[0], pos[1], pos[2]);
    }

    ImGui::Separator();
    ImGui::Text("Raw Mumble JSON:");
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 460);
    ImGui::TextWrapped("%s", rawJson.c_str());
    ImGui::PopTextWrapPos();

    ImGui::End();
}
