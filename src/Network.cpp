#include "Network.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <winhttp.h>
#include <map>
#include "RTAPI/RTAPI.h"
#include "UIColors.h"

#pragma comment(lib, "winhttp.lib")

// Definizioni delle globali dichiarate in Network.h
std::string ApiKey;
std::string AccountToken;
std::string RegistrationStatus = "Not registered";
ImVec4      RegistrationColor = ImVec4(1, 1, 0, 1);
std::string ServerStatus;
ImVec4      ServerColor = ImVec4(1, 1, 0, 1);
std::string LastServerResponse;


std::string LastViolationTitle;
std::string LastViolationDesc;

extern RealTimeData* RTAPIData;
extern AddonAPI* APIDefs;

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
        (LPVOID)body.data(), (DWORD)body.size(),
        (DWORD)body.size(), 0);
    if (ok && WinHttpReceiveResponse(r, NULL)) {
        DWORD sz = 0; outResp.clear();
        do {
            if (!WinHttpQueryDataAvailable(r, &sz) || !sz) break;
            std::vector<char> buf(sz + 1);
            DWORD got = 0; WinHttpReadData(r, buf.data(), sz, &got);
            buf[got] = 0; outResp += buf.data();
        } while (sz);
        outResp.erase(std::remove_if(outResp.begin(), outResp.end(),
            [](unsigned char ch) { return ch == '\n' || ch == '\r' || ch == ' '; }), outResp.end());
    }
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
}

void SendRegistration() {
    if (ApiKey.empty()) return;
    std::ostringstream p; p << "{\"api_key\":\"" << ApiKey << "\"}";
    std::string resp; HttpPostJSON(L"heroesascent.org", L"/api/register", p.str(), resp);

    if (resp.find("\"status\":\"ok\"") != std::string::npos) {
        RegistrationStatus = "Registered";
        RegistrationColor = ImVec4(0.3f, 1, 0.3f, 1);
        size_t s = resp.find("\"account_token\":\"");
        if (s != std::string::npos) {
            s += 17; size_t e = resp.find('"', s);
            AccountToken = resp.substr(s, e - s);
        }
    }
    else {
        RegistrationStatus = "Registration failed";
        RegistrationColor = ImVec4(1, 0.4f, 0.4f, 1);
    }
}

void SendPlayerUpdate() {
    if (!RTAPIData || AccountToken.empty()) return;
    std::ostringstream p;
    p << "{\"token\":\"" << AccountToken << "\","
        << "\"name\":\"" << RTAPIData->CharacterName << "\","
        << "\"map\":" << RTAPIData->MapID << ","
        << "\"state\":" << RTAPIData->CharacterState << "}";

    std::string resp; HttpPostJSON(L"heroesascent.org", L"/api/character/update", p.str(), resp);
    LastServerResponse = resp;

    if (resp.find("\"rules_valid\":false") != std::string::npos) {
        ServerStatus = "Violation detected";
        ServerColor = ImVec4(1, 0.4f, 0.4f, 1);
    }
    else {
        ServerStatus = "Rules respected";
        ServerColor = ImVec4(0.3f, 1, 0.3f, 1);
    }
}

void CheckServerStatus() {
    if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Network", "=== CheckServerStatus() called ===");

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

    if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Network", "Sending GET /api/status...");
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
        if (APIDefs) APIDefs->Log(ELogLevel_INFO, "Network", ("Raw response: " + response).c_str());
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
