// Network.cpp
#include "Network.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <winhttp.h>
#include <map>
#include "RTAPI/RTAPI.h"
#include "mumble/Mumble.h"
#include "UIColors.h"
#include "Localization.h"
#include <fstream>
#include "Globals.h"
#include <iomanip>
#include "json/json.hpp"
#include "PlayerEventType.h"
#include <cstdio>

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
    LastServerResponse = outResp;

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
    if (ServerStatus == "Server offline") {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Network", "Server offline — skipping token validation.");
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
        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "Network", "Account token validated successfully.");
    }
    else {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, "Network", "Invalid account token — deleting local token...");
        DeleteAccountToken();
        AccountToken.clear();
        RegistrationStatus = T("ui.registration_required");
        RegistrationColor = ColorWarning;
    }
}

void SendRegistration() {
    if (ApiKey.empty()) {
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
        << "\"api_key\":\"" << ApiKey << "\"";

    if (!accountName.empty()) {
        p << ",\"account_name\":\"" << accountName << "\"";
    }

    p << "}";

    std::string resp;
    HttpPostJSON(L"heroesascent.org", L"/api/account/register", p.str(), resp, 70000);

    if (APIDefs)
        APIDefs->Log(ELogLevel_INFO, "Network", ("Registration raw response: " + resp).c_str());

    if (resp.empty()) {
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
        RegistrationStatus = T("ui.registration_registered");
        RegistrationColor = ColorSuccess;

        // Salva il token solo in caso di registrazione riuscita
        size_t s = resp.find("\"account_token\":\"");
        if (s != std::string::npos) {
            s += 17;
            size_t e = resp.find('"', s);
            if (e != std::string::npos) {
                AccountToken = resp.substr(s, e - s);
                SaveAccountToken(AccountToken);
            }
        }
    }
    else if (message == "already_registered") {
        RegistrationStatus = T("ui.registration_already_registered");
        RegistrationColor = ColorWarning;

        // Aggiorniamo/riconfermiamo il token
        size_t s = resp.find("\"account_token\":\"");
        if (s != std::string::npos) {
            s += 17;
            size_t e = resp.find('"', s);
            if (e != std::string::npos) {
                AccountToken = resp.substr(s, e - s);
                SaveAccountToken(AccountToken);
            }
        }
    }
    else if (message == "missing_key") {
        RegistrationStatus = T("ui.registration_missing_key");
        RegistrationColor = ColorWarning;
    }
    else if (message == "missing_account_name") {
        RegistrationStatus = T("ui.registration_missing_account_name");
        RegistrationColor = ColorWarning;
    }
    else if (message == "gw2_invalid_api_key") {
        RegistrationStatus = T("ui.registration_gw2_invalid_api_key");
        RegistrationColor = ColorError;
    }
    else if (message == "invalid_permissions") {
        RegistrationStatus = T("ui.registration_invalid_permissions");
        RegistrationColor = ColorError;
    }
    else if (message == "account_mismatch") {
        RegistrationStatus = T("ui.registration_account_mismatch");
        RegistrationColor = ColorError;
    }
    else if (message == "too_many_ap") {
        RegistrationStatus = T("ui.registration_too_many_ap");
        RegistrationColor = ColorError;
    }
    else if (message == "gw2_api_error" || message == "gw2_api_unavailable" || message == "gw2_api_down") {
        RegistrationStatus = T("ui.registration_gw2_api_error");
        RegistrationColor = ColorError;
    }
    else {
        // Fallback
        RegistrationStatus = T("ui.registration_failed");
        RegistrationColor = ColorError;
    }
}

void SendPlayerUpdate(PlayerEventType eventType) {
    if (!RTAPIData || AccountToken.empty()) return;

    // === Costruzione JSON completo ===
    json payload = {
        {"token", AccountToken},
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
        {"event", ToString(eventType)}
    };

    // === Aggiunta dati extra da Mumble (se disponibile) ===
    if (Mumble::Data* m = Mumble::GetData()) {
        const std::string identityUtf8 = WideToUtf8(m->Identity);
        if (!identityUtf8.empty()) {
            nlohmann::json id = nlohmann::json::parse(identityUtf8, nullptr, false);
            if (!id.is_discarded()) {
                payload["race"] = id.value("race", -1);
                payload["commander"] = id.value("commander", false);
                payload["team_color_id"] = id.value("team_color_id", -1);
            }
        }
    }

    // === Invio ===
    std::string resp;
    HttpPostJSON(L"heroesascent.org", L"/api/character/update", payload.dump(), resp);
    LastServerResponse = resp;

    if (APIDefs) {
        if (resp.empty())
            APIDefs->Log(ELogLevel_WARNING, "Network", "SendPlayerUpdate(): empty response");
        else
            APIDefs->Log(ELogLevel_INFO, "Network", ("SendPlayerUpdate() => " + resp).c_str());
    }

    // === Analisi risposta JSON ===
    try {
        json j = json::parse(resp);

        std::string status = j.value("status", "");
        std::string message = j.value("message", "");

        // --- Caso: personaggio squalificato ---
        if (status == "error" && message == "Character is disqualified") {
            CharacterStatus = "disqualified";
            CharacterColor = ColorError;

            LastViolationTitle.clear();
            LastViolationDesc.clear();
            LastViolationCode.clear();

            if (j.contains("last_violation") && j["last_violation"].contains("code")) {
                LastViolationCode = j["last_violation"]["code"].get<std::string>();

                if (APIDefs) {
                    APIDefs->Log(ELogLevel_INFO, "Network",
                        (std::string("Last violation code received: ") + LastViolationCode).c_str());
                }

                auto it = Violations.find(LastViolationCode);
                if (it != Violations.end()) {
                    LastViolationTitle = it->second.first;
                    LastViolationDesc = it->second.second;
                }
                else {
                    LastViolationTitle = T("ui.unknown_violation_title");
                    LastViolationDesc = T("ui.unknown_violation_desc");
                }
            }
            else {
                LastViolationTitle = T("ui.unknown_violation_title");
                LastViolationDesc = T("ui.unknown_violation_desc");
            }
        }

        // --- Caso OK ---
        else if (status == "ok") {
            CharacterStatus = "valid";
            CharacterColor = ColorSuccess;

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
        ServerStatus = "Server offline"; ServerColor = ColorError;
        return;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"heroesascent.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Network", "WinHttpConnect failed");
        WinHttpCloseHandle(hSession);
        ServerStatus = "Server offline"; ServerColor = ColorError;
        return;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/api/status", NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Network", "WinHttpOpenRequest failed");
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        ServerStatus = "Server offline"; ServerColor = ColorError;
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
        ServerStatus = "Server online";
        ServerColor = ColorSuccess;
        if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Network", "Server online");
    }
    else {
        ServerStatus = "Server offline";
        ServerColor = ColorError;
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, "Network", "Server offline");
    }
}

void InitNetwork(AddonAPI* api) {
    (void)api;
    ServerStatus = "Checking…";
}
