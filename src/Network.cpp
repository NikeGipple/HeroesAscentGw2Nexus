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

#pragma comment(lib, "winhttp.lib")

// Definizioni delle globali dichiarate in Network.h
std::string ApiKey;
std::string AccountToken;
std::string RegistrationStatus = "Not registered";
ImVec4      RegistrationColor = ImVec4(1, 1, 0, 1);
std::string ServerStatus;
ImVec4      ServerColor = ImVec4(1, 1, 0, 1);

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

static void HttpPostJSON(const wchar_t* host, const wchar_t* path, const std::string& body, std::string& outResp) {
    HINTERNET s = WinHttpOpen(L"HeroesAscent/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return;

    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c) { WinHttpCloseHandle(s); return; }

    HINTERNET r = WinHttpOpenRequest(c, L"POST", path, NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(s); return; }

    std::wstring headers = L"Content-Type: application/json\r\n";
    BOOL ok = WinHttpSendRequest(r, headers.c_str(), -1L,
        (LPVOID)body.c_str(), (DWORD)body.size(),
        (DWORD)body.size(), 0);

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
        APIDefs->Log(ELogLevel_INFO, "Network", ("Response: " + outResp).c_str());

    WinHttpCloseHandle(r);
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
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

void SendRegistration() {
    if (ApiKey.empty()) {
        RegistrationStatus = T("ui.registration_missing_key");
        RegistrationColor = ColorWarning;
        return;
    }

    std::ostringstream p;
    p << "{\"api_key\":\"" << ApiKey << "\"}";
    std::string resp;
    HttpPostJSON(L"heroesascent.org", L"/api/register", p.str(), resp);

    if (resp.find("\"status\":\"ok\"") != std::string::npos) {
        RegistrationStatus = T("ui.registration_success");
        RegistrationColor = ColorSuccess;

        size_t s = resp.find("\"account_token\":\"");
        if (s != std::string::npos) {
            s += 17;
            size_t e = resp.find('"', s);
            AccountToken = resp.substr(s, e - s);
            SaveAccountToken(AccountToken);
        }
    }
    else if (resp.find("already_registered") != std::string::npos) {
        RegistrationStatus = T("ui.registration_already");
        RegistrationColor = ColorWarning;
    }
    else if (resp.empty()) {
        RegistrationStatus = T("ui.registration_no_response");
        RegistrationColor = ColorError;
    }
    else {
        RegistrationStatus = T("ui.registration_failed");
        RegistrationColor = ColorError;
    }

    if (APIDefs)
        APIDefs->Log(ELogLevel_INFO, "Network", ("Registration result: " + resp).c_str());
}




void SendPlayerUpdate(bool isLogin) {
    if (!RTAPIData || AccountToken.empty()) return;

    // === Costruzione JSON con RTAPI + Mumble ===
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
        { "is_login", isLogin }
    };

    // === Aggiunta dati extra da Mumble (se disponibile) ===
    if (Mumble::Data* m = Mumble::GetData()) {
        // ATTENZIONE: campo si chiama "Identity" (maiuscola) e contiene JSON widechar
        const std::string identityUtf8 = WideToUtf8(m->Identity);

        if (!identityUtf8.empty()) {
            nlohmann::json id = nlohmann::json::parse(identityUtf8, nullptr, false);
            if (!id.is_discarded()) {
                payload["race"] = id.value("race", -1);
                payload["commander"] = id.value("commander", false);
                payload["team_color_id"] = id.value("team_color_id", -1);

                // volendo puoi prendere anche la mount dal JSON standard di Mumble:
                // payload["mount"] = id.value("mount", payload["mount"]);
            }
        }
    }



    // === Invio ===
    std::string resp;
    HttpPostJSON(L"heroesascent.org", L"/api/character/update", payload.dump(), resp);
    LastServerResponse = resp;
    
    if (APIDefs) {
    if (resp.empty()) {
        APIDefs->Log(ELogLevel_WARNING, "Network", "SendPlayerUpdate(): empty response from /api/character/update");
    } else {
        APIDefs->Log(ELogLevel_INFO, "Network", ("SendPlayerUpdate() response: " + resp).c_str());
    }
}

    if (resp.find("\"rules_valid\":false") != std::string::npos) {
        ServerStatus = T("ui.violation_detected");
        ServerColor = ColorError;

        size_t codeStart = resp.find("\"violation_code\":\"");
        if (codeStart != std::string::npos) {
            codeStart += 18;
            size_t codeEnd = resp.find('"', codeStart);
            std::string code = resp.substr(codeStart, codeEnd - codeStart);
            LastViolationCode = code;

            if (APIDefs)
                APIDefs->Log(ELogLevel_INFO, "Network", ("Violation code: " + code).c_str());

            if (Violations.find(code) != Violations.end()) {
                LastViolationTitle = Violations[code].first;
                LastViolationDesc = Violations[code].second;
            }
            else {
                LastViolationTitle = code;
                LastViolationDesc = T("ui.unknown_violation");
            }
        }
        else {
            LastViolationTitle = "Unknown violation";
            LastViolationDesc = T("ui.unknown_violation");
        }
    }
    else {
        ServerStatus = T("ui.rules_respected");
        ServerColor = ColorSuccess;
        LastViolationTitle.clear();
        LastViolationDesc.clear();
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
