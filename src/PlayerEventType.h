    // PlayerEventType.h
    #pragma once
    enum class PlayerEventType {
        LOGIN,
        LOGOUT,
        LOGOUT_LOW_HP,
        DOWNED,
        DEAD,
        MAP_CHANGED,
        MOUNT_CHANGED,
        HEALING_USED,
        LEVEL_UP,
        BUFF_APPLIED,
        GROUP,
        GLIDING
    };

    inline const char* ToString(PlayerEventType type) {
        switch (type) {
        case PlayerEventType::LOGIN:            return "LOGIN";
        case PlayerEventType::LOGOUT:           return "LOGOUT";
		case PlayerEventType::LOGOUT_LOW_HP:    return "LOGOUT_LOW_HP";
        case PlayerEventType::DOWNED:           return "DOWNED";
        case PlayerEventType::DEAD:             return "DEAD";
        case PlayerEventType::MAP_CHANGED:      return "MAP_CHANGED";
        case PlayerEventType::MOUNT_CHANGED:    return "MOUNT_CHANGED";
        case PlayerEventType::HEALING_USED:     return "HEALING_USED"; 
        case PlayerEventType::LEVEL_UP:         return "LEVEL_UP";
        case PlayerEventType::BUFF_APPLIED:     return "BUFF_APPLIED";
        case PlayerEventType::GROUP:            return "GROUP";
        case PlayerEventType::GLIDING:          return "GLIDING";
        default:                                return "UNKNOWN";
        }
    }
