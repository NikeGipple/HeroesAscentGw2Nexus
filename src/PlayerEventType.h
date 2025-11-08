// PlayerEventType.h
#pragma once
enum class PlayerEventType {
    LOGIN,
    DOWNED,
    DEAD,
    RESPAWN,
    MAP_CHANGED,
    MOUNT_CHANGED 
};

inline const char* ToString(PlayerEventType type) {
    switch (type) {
    case PlayerEventType::LOGIN:         return "LOGIN";
    case PlayerEventType::DOWNED:        return "DOWNED";
    case PlayerEventType::DEAD:          return "DEAD";
    case PlayerEventType::MAP_CHANGED:   return "MAP_CHANGED";
    case PlayerEventType::MOUNT_CHANGED: return "MOUNT_CHANGED";
    default:                             return "UNKNOWN";
    }
}
