// Network.cpp
#include "Network.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <winhttp.h>
#include <map>
#include "RTAPI/RTAPI.h"
#include "MumbleLink.h"
#include "UIColors.h"
#include "Localization.h"
#include <fstream>
#include "Globals.h"
#include <iomanip>
#include "json/json.hpp"
#include "PlayerEventType.h"
#include <cstdio>
#include <mutex>

#pragma comment(lib, "winhttp.lib")

// Definizioni delle globali dichiarate in Network.h
std::string ApiKey;
std::string AccountToken;
std::string RegistrationStatus = "Not registered";
ImVec4      RegistrationColor = ImVec4(1, 1, 0, 1);
std::string ServerStatus;
ImVec4      ServerColor = ImVec4(1, 1, 0, 1);
extern std::string CharacterStatus;
extern ImVec4 CharacterColor;

using json = nlohmann::json;

// helper: wide -> utf8
static std::string WideToUtf8(const wchar_t* ws) {
    if (!ws) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len - 1, '\0'); // -1 per escludere il terminatore
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), len, nullptr, nullptr);
    return out;
}

static void HttpPostJSON(const wchar_t* host,
    const wchar_t* path,
    const std::string& body,
    std::string& outResp,
    DWORD timeoutMs = 0) // opzionale (0 = usa default di sistema)
{
    HINTERNET s = WinHttpOpen(L"HeroesAscent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!s) return;

    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c) { WinHttpCloseHandle(s); return; }

    HINTERNET r = WinHttpOpenRequest(c, L"POST", path, NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(s); return; }

    // Se specificato, imposta timeout personalizzato
    if (timeoutMs > 0) {
        WinHttpSetTimeouts(r,
            10000,        // DNS resolve timeout
            10000,        // connect timeout
            timeoutMs,    // send timeout
            timeoutMs);   // receive timeout
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    BOOL ok = WinHttpSendRequest(r,
        headers.c_str(),
        -1L,
        (LPVOID)body.c_str(),
        (DWORD)body.size(),
        (DWORD)body.size(),
        0);

    if (ok && WinHttpReceiveResponse(r, NULL)) {
        DWORD dwSize = 0;
        std::string response;
        do {
            if (!WinHttpQueryDataAvailable(r, &dwSize)) break;
            if (dwSize == 0) break;
            std::vector<char> buffer(dwSize + 1);
            DWORD dwDownloaded = 0;
            if (!WinHttpReadData(r, buffer.data(), dwSize, &dwDownloaded)) break;
            buffer[dwDownloaded] = '\0';
            response += buffer.data();
        } while (dwSize > 0);
        outResp = response;
    }

    if (APIDefs)
        APIDefs->Log(ELogLevel_INFO, "Network",
            ("Response: " + outResp + " (timeout=" +
                std::to_string(timeoutMs) + "ms)").c_str());

    WinHttpCloseHandle(r);
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
}

void SendJsonToServer(const wchar_t* path, const std::string& body, std::string& outResp) {
    outResp.clear();
    HttpPostJSON(L"heroesascent.org", path, body, outResp);

    {   
        std::scoped_lock lk(gStateMx);
        LastServerResponse = outResp;
    }

    if (APIDefs) {
        if (outResp.empty())
            APIDefs->Log(ELogLevel_WARNING, "Network", "SendJsonToServer(): empty response");
        else
            APIDefs->Log(ELogLevel_INFO, "Network", (std::string("POST ") + WideToUtf8(path) + " => " + outResp).c_str());
    }
}

void SaveAccountToken(const std::string& token) {
    std::string path = GetAddonBasePath() + "\\accounttoken";
    std::ofstream out(path, std::ios::trunc);
    if (out.is_open()) {
        out << token;
        out.close();
    }
    else {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Network", ("Failed to save accounttoken at: " + path).c_str());
    }
}

std::string LoadAccountToken() {
    std::string path = GetAddonBasePath() + "\\accounttoken";
    std::ifstream in(path);
    if (!in.is_open()) {
        return "";
    }
    std::string token;
    std::getline(in, token);
    in.close();
    return token;
}

void DeleteAccountToken() {
    std::string path = GetAddonBasePath() + "\\accounttoken";

    // Prova a cancellare il file
    if (std::remove(path.c_str()) == 0) {
        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "Network", ("Deleted account token file at: " + path).c_str());
    }
    else {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Network", ("Failed to delete account token file at: " + path).c_str());
    }
}

void CheckAccountToken() {
    std::string token = LoadAccountToken();
    if (token.empty()) {
        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "Network", "No account token found — registration required.");
        return;
    }

    CheckServerStatus();

    std::string serverStatusCopy;
    {   
        std::scoped_lock lk(gStateMx);
        serverStatusCopy = ServerStatus;
    }

    if (serverStatusCopy == "Server offline" || serverStatusCopy == T("ui.server_offline")) {

        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Network", "Server offline — skipping token validation.");
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.server_unreachable");
        RegistrationColor = ColorWarning;
        return; 
    }

    std::string accountName;
    if (RTAPIData && RTAPIData->AccountName[0] != '\0') {
        accountName = RTAPIData->AccountName;
    }

    std::ostringstream payload;
    payload << "{"
        << "\"account_token\":\"" << token << "\","
        << "\"account_name\":\"" << accountName << "\""
        << "}";


    std::string resp;
    HttpPostJSON(L"heroesascent.org", L"/api/account/check", payload.str(), resp);

    if (APIDefs)
        APIDefs->Log(ELogLevel_INFO, "Network", ("Token check response: " + resp).c_str());

    // Analizziamo la risposta grezza
    if (resp.find("\"result\":true") != std::string::npos) {

        {
            std::scoped_lock lk(gStateMx);
            AccountToken = token; 
        }

        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "Network", "Account token validated successfully.");
    }
    else {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Network", "Invalid account token — deleting local token...");
        DeleteAccountToken();
        std::scoped_lock lk(gStateMx);
        AccountToken.clear();
        RegistrationStatus = T("ui.registration_required");
        RegistrationColor = ColorWarning;
    }
}

void SendRegistration() {

    std::string apiKeyLocal;
    {  
        std::scoped_lock lk(gStateMx);
        apiKeyLocal = ApiKey;
    }

    if (apiKeyLocal.empty()) {
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_missing_key");
        RegistrationColor = ColorWarning;
        return;
    }

    // Recupera il nome account da RTAPI se disponibile
    std::string accountName;
    if (RTAPIData && RTAPIData->AccountName[0] != '\0') {
        accountName = RTAPIData->AccountName;
    }

    // Costruisce il payload JSON
    std::ostringstream p;
    p << "{"
        << "\"api_key\":\"" << apiKeyLocal << "\"";

    if (!accountName.empty()) {
        p << ",\"account_name\":\"" << accountName << "\"";
    }

    p << "}";

    std::string resp;
    HttpPostJSON(L"heroesascent.org", L"/api/account/register", p.str(), resp, 70000);

    if (APIDefs)
        APIDefs->Log(ELogLevel_INFO, "Network", ("Registration raw response: " + resp).c_str());

    if (resp.empty()) {
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_no_response");
        RegistrationColor = ColorError;
        return;
    }

    // === Estrae il campo "message" dal JSON ===
    std::string message;
    size_t pos = resp.find("\"message\":\"");
    if (pos != std::string::npos) {
        pos += 11; // salta "message":" 
        size_t end = resp.find('"', pos);
        if (end != std::string::npos) {
            message = resp.substr(pos, end - pos);
        }
    }

    // === Interpreta il messaggio ===
    if (message == "registered") {
        {
            std::scoped_lock lk(gStateMx);
            RegistrationStatus = T("ui.registration_registered");
            RegistrationColor = ColorSuccess;
        }

        // Salva il token solo in caso di registrazione riuscita
        size_t s = resp.find("\"account_token\":\"");
        if (s != std::string::npos) {
            s += 17;
            size_t e = resp.find('"', s);
            if (e != std::string::npos) {
                std::string token = resp.substr(s, e - s);

                {  
                    std::scoped_lock lk(gStateMx);
                    AccountToken = token;
                }

                SaveAccountToken(token);
            }
        }
    }
    else if (message == "already_registered") {
        {   
            std::scoped_lock lk(gStateMx);
            RegistrationStatus = T("ui.registration_already_registered");
            RegistrationColor = ColorWarning;
        }

        // Aggiorniamo/riconfermiamo il token
        size_t s = resp.find("\"account_token\":\"");
        if (s != std::string::npos) {
            s += 17;
            size_t e = resp.find('"', s);
            if (e != std::string::npos) {
                std::string token = resp.substr(s, e - s);

                {
                    std::scoped_lock lk(gStateMx);
                    AccountToken = token;
                }

                SaveAccountToken(token);
            }
        }
    }
    else if (message == "missing_key") {
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_missing_key");
        RegistrationColor = ColorWarning;
    }
    else if (message == "missing_account_name") {
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_missing_account_name");
        RegistrationColor = ColorWarning;
    }
    else if (message == "gw2_invalid_api_key") {
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_gw2_invalid_api_key");
        RegistrationColor = ColorError;
    }
    else if (message == "invalid_permissions") {
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_invalid_permissions");
        RegistrationColor = ColorError;
    }
    else if (message == "account_mismatch") {
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_account_mismatch");
        RegistrationColor = ColorError;
    }
    else if (message == "too_many_ap") {
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_too_many_ap");
        RegistrationColor = ColorError;
    }
    else if (message == "gw2_api_error" || message == "gw2_api_unavailable" || message == "gw2_api_down") {
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_gw2_api_error");
        RegistrationColor = ColorError;
    }
    else if (message == "guild_membership_not_allowed") {
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_guild_membership_not_allowed");
        RegistrationColor = ColorError;
    }
    else {
        // Fallback
        std::scoped_lock lk(gStateMx);
        RegistrationStatus = T("ui.registration_failed");
        RegistrationColor = ColorError;
    }
}

void SendPlayerUpdate(
    PlayerEventType eventType,
    uint32_t buffId,
    const char* buffName)
{   
    if (!RTAPIData) return;

    std::string tokenLocal;
    {  
        std::scoped_lock lk(gStateMx);
        tokenLocal = AccountToken;
    }
    if (tokenLocal.empty()) return;

    // === Costruzione JSON completo ===
    json payload = {
        {"token", tokenLocal},
        {"name", RTAPIData->CharacterName},
        {"map_id", RTAPIData->MapID},
        {"map_type", RTAPIData->MapType},
        {"profession", RTAPIData->Profession},
        {"elite_spec", RTAPIData->EliteSpecialization},
        {"mount", RTAPIData->MountIndex},
        {"state", RTAPIData->CharacterState},
        {"group_type", RTAPIData->GroupType},
        {"group_count", RTAPIData->GroupMemberCount},
        {"position", {
            {"x", RTAPIData->CharacterPosition[0]},
            {"y", RTAPIData->CharacterPosition[2]},
            {"z", RTAPIData->CharacterPosition[1]}
        }},
        {"game_state", RTAPIData->GameState},
        {"language", RTAPIData->Language},
        {"event", ToString(eventType)},
        {"level", RTAPIData->CharacterLevel},
        {"effective_level", RTAPIData->CharacterEffectiveLevel}
    };

    if (const Mumble::Data* m = MumbleLink::GetData())
    {
        std::string idStr = WideToUtf8(m->Identity);

        if (!idStr.empty())
        {
            json id = json::parse(idStr, nullptr, false);

            if (!id.is_discarded())
            {
                payload["race"] = id.value("race", -1);
            }
        }
    }


    if (eventType == PlayerEventType::BUFF_APPLIED && buffId != 0) {
        payload["buff_id"] = buffId;
        if (buffName && buffName[0] != '\0') {
            payload["buff_name"] = buffName;
        }
    }

    // === Invio ===
    std::string resp;
    HttpPostJSON(L"heroesascent.org", L"/api/character/update", payload.dump(), resp);

    // === Risposta ===
    { 
        std::scoped_lock lk(gStateMx);
        LastServerResponse = resp;
    }

    if (APIDefs) {
        if (resp.empty())
            APIDefs->Log(ELogLevel_WARNING, "Network", "SendPlayerUpdate(): empty response");
        else
            APIDefs->Log(ELogLevel_INFO, "Network", ("SendPlayerUpdate() => " + resp).c_str());
    }

    // === parse risposta JSON ===
    try {
        json j = json::parse(resp);

        std::string status = j.value("status", "");
        std::string message = j.value("message", "");


        // --- Caso: personaggio squalificato ---

        const std::vector<std::string> criticalErrors = {
            "Character is disqualified",
            "Character not found"
        };


        if (status == "error" &&
            std::find(criticalErrors.begin(), criticalErrors.end(), message) != criticalErrors.end())
        {
            // 1) Prepara tutto in locale 
            std::string newTitle;
            std::string newDesc;
            std::string newCode;

            // Se il personaggio è disqualificato, di default è una violazione "generica"
            ViolationType newType = ViolationType::GenericViolation;

            if (message == "Character not found") {
                newType = ViolationType::CharacterNotFound;
                newTitle = T("ui.character_not_found_title");
                newDesc = T("ui.character_not_found_desc");
                newCode.clear();
            }
            else if (j.contains("last_violation") && j["last_violation"].contains("code")) {
                newCode = j["last_violation"]["code"].get<std::string>();

                if (!TryGetViolation(newCode, newTitle, newDesc)) {
                    newTitle = T("ui.unknown_violation_title");
                    newDesc = T("ui.unknown_violation_desc");
                }
            }
            else {
                // "Character is disqualified" ma senza dettagli
                newCode = "DISQUALIFIED_GENERIC";

                if (!TryGetViolation(newCode, newTitle, newDesc)) {
                    newTitle = T("ui.unknown_violation_title");
                    newDesc = T("ui.unknown_violation_desc");
                }
            }

            // 2) Applica tutto in modo atomico sotto gStateMx
            {
                std::scoped_lock lk(gStateMx);

                CharacterStatus = "disqualified";
                CharacterColor = ColorError;

                LastViolationType = newType;
                LastViolationCode = newCode;
                LastViolationTitle = newTitle;
                LastViolationDesc = newDesc;
            }
        }

        // --- Caso OK ---
        else if (status == "ok") {
            std::scoped_lock lk(gStateMx);
            CharacterStatus = "valid";
            CharacterColor = ColorSuccess;

            LastViolationType = ViolationType::None;
            LastViolationTitle.clear();
            LastViolationDesc.clear();
            LastViolationCode.clear();
        }
    }
    catch (...) {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Network", "JSON parse error in SendPlayerUpdate()");
    }

}

void CheckServerStatus() {


    HINTERNET hSession = WinHttpOpen(L"HeroesAscent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Network", "WinHttpOpen failed");
        {
            std::scoped_lock lk(gStateMx);
            ServerStatus = "Server offline";
            ServerColor = ColorError;
        }
        return;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"heroesascent.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Network", "WinHttpConnect failed");
        WinHttpCloseHandle(hSession);
        {
            std::scoped_lock lk(gStateMx);
            ServerStatus = "Server offline";
            ServerColor = ColorError;
        }
        return;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/api/status", NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Network", "WinHttpOpenRequest failed");
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        {
            std::scoped_lock lk(gStateMx);
            ServerStatus = "Server offline";
            ServerColor = ColorError;
        }
        return;
    }

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    std::string response;
    if (ok && WinHttpReceiveResponse(hRequest, NULL)) {
        if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Network", "Response received");

        DWORD sz = 0;
        do {
            if (!WinHttpQueryDataAvailable(hRequest, &sz) || !sz) break;
            std::vector<char> buf(sz + 1);
            DWORD got = 0; WinHttpReadData(hRequest, buf.data(), sz, &got);
            buf[got] = 0; response += buf.data();
        } while (sz);
    }
    else {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Network", "No response or WinHttpReceiveResponse failed");
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (response.find("\"status\":\"ok\"") != std::string::npos) {
        {
            std::scoped_lock lk(gStateMx);
            ServerStatus = T("ui.server_online");
            ServerColor = ColorSuccess;
        }

        if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Network", "Server online");
    }
    else {
        {
            std::scoped_lock lk(gStateMx);
            ServerStatus = T("ui.server_offline");
            ServerColor = ColorError;
        }
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Network", "Server offline");
    }
}

void InitNetwork(AddonAPI* api) {
    (void)api;
    std::scoped_lock lk(gStateMx);
    ServerStatus = "Checking…";
}
