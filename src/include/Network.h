#pragma once
#include <string>
#include "nexus/Nexus.h"
#include "imgui/imgui.h"
#include "RTAPI/RTAPI.h"  
#include <map>

// Variabili globali condivise
extern std::string ApiKey;
extern std::string AccountToken;
extern std::string RegistrationStatus;
extern ImVec4      RegistrationColor;

extern std::string ServerStatus;
extern ImVec4      ServerColor;
extern std::string LastServerResponse;

extern std::string LastViolationTitle;
extern std::string LastViolationDesc;

// Violazioni caricate da Localization
extern std::map<std::string, std::pair<std::string, std::string>> Violations;

// Puntatore globale ai dati runtime del personaggio
extern RealTimeData* RTAPIData;

// Funzioni
void InitNetwork(AddonAPI* api);
void SendRegistration();
void SendPlayerUpdate(bool isLogin = false);
void CheckServerStatus();
std::string LoadAccountToken();
void SaveAccountToken(const std::string& token);

