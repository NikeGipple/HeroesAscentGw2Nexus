#include "logger.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <chrono>


// ============================================================
// ENUM DECODERS
// ============================================================

const char* DecodeStateChange(uint8_t sc) {
    switch (sc) {
    case 0: return "NONE";
    case 1: return "ENTERCOMBAT";
    case 2: return "EXITCOMBAT";
    case 3: return "CHANGEUP";
    case 4: return "CHANGEDEAD";
    case 5: return "CHANGEDOWN";
    case 6: return "SPAWN";
    case 7: return "DESPAWN";
    case 8: return "HEALTHPCTUPDATE";
    case 9: return "SQCOMBATSTART";
    case 10: return "SQCOMBATEND";
    case 11: return "WEAPSWAP";
    case 12: return "MAXHEALTHUPDATE";
    case 13: return "POINTOFVIEW";
    case 14: return "LANGUAGE";
    case 15: return "GWBUILD";
    case 16: return "SHARDID";
    case 17: return "REWARD";
    case 18: return "BUFFINITIAL";
    case 19: return "POSITION";
    case 20: return "VELOCITY";
    case 21: return "FACING";
    case 22: return "TEAMCHANGE";
    case 23: return "ATTACKTARGET";
    case 24: return "TARGETABLE";
    case 25: return "MAPID";
    case 26: return "REPLINFO";
    case 27: return "STACKACTIVE";
    case 28: return "STACKRESET";
    case 29: return "GUILD";
    case 30: return "BUFFINFO";
    case 31: return "BUFFFORMULA";
    case 32: return "SKILLINFO";
    case 33: return "SKILLTIMING";
    case 34: return "BREAKBARSTATE";
    case 35: return "BREAKBARPERCENT";
    case 36: return "INTEGRITY";
    case 37: return "MARKER";
    case 38: return "BARRIERPCTUPDATE";
    case 39: return "STATRESET";
    case 40: return "EXTENSION";
    case 41: return "APIDELAYED";
    case 42: return "INSTANCESTART";
    case 43: return "RATEHEALTH";
    case 44: return "LAST90BEFOREDOWN";
    case 45: return "EFFECT (retired)";
    case 46: return "IDTOGUID";
    case 47: return "LOGNPCUPDATE";
    case 48: return "IDLEEVENT";
    case 49: return "EXTENSIONCOMBAT";
    case 50: return "FRACTALSCALE";
    case 51: return "EFFECT2_DEFUNC (retired)";
    case 52: return "RULESET";
    case 53: return "SQUADMARKER";
    case 54: return "ARCBUILD";
    case 55: return "GLIDER";
    case 56: return "STUNBREAK";
    case 57: return "MISSILECREATE";
    case 58: return "MISSILELAUNCH";
    case 59: return "MISSILEREMOVE";
    case 60: return "EFFECTGROUNDCREATE";
    case 61: return "EFFECTGROUNDREMOVE";
    case 62: return "EFFECTAGENTCREATE";
    case 63: return "EFFECTAGENTREMOVE";
    case 64: return "IIDCHANGE";
    case 65: return "MAPCHANGE";
    default: return "UNKNOWN";
    }
}

const char* DecodeActivation(uint8_t act) {
    switch (act) {
    case 0: return "NONE";
    case 1: return "START";
    case 2: return "QUICKNESS_UNUSED";
    case 3: return "CANCEL_FIRE";
    case 4: return "CANCEL_CANCEL";
    case 5: return "RESET";
    default: return "UNKNOWN";
    }
}

const char* DecodeBuffRemove(uint8_t br) {
    switch (br) {
    case 0: return "NONE";
    case 1: return "ALL";
    case 2: return "SINGLE";
    case 3: return "MANUAL";
    default: return "UNKNOWN";
    }
}

const char* DecodeIFF(uint8_t iff) {
    switch (iff) {
    case 0: return "FRIEND";
    case 1: return "FOE";
    case 2: return "UNKNOWN";
    }
    return "UNKNOWN";
}

struct KillTrack {
    std::string targetName;

    std::string lastHitSrcName;
    uint64_t lastHitSrcAgent = 0;
    uint32_t lastHitSkillId = 0;
    std::string lastHitSkillName;
    int lastHitDamage = 0;

    std::chrono::steady_clock::time_point lastHitTime;
    std::chrono::steady_clock::time_point lastSeenTime;

    uint32_t hitCount = 0;
    bool killLogged = false;
};

static std::unordered_map<uint64_t, KillTrack> killTargets;

static const int DEAD_TIMEOUT_MS = 800;
static const int MAX_KILL_WINDOW_MS = 6000;
static const int MIN_DAMAGE_FOR_KILL = 300;


static bool IsRealDamageHit(const EvCombatData* e)
{
    if (!e || !e->ev) return false;

    bool direct = (e->ev->value < 0);
    bool cond = (e->ev->buff_dmg > 0);

    if (!direct && !cond) return false;
    if (e->ev->skillid == 0) return false;

    if (!e->dst || !e->dst->name) return false;
    if (strcmp(e->dst->name, "0") == 0) return false;

    return true;
}

void LogKillCompact(const KillTrack& info)
{
    if (!APIDefs) return;

    char out[256];
    std::snprintf(out, sizeof(out),
        "[KILL] \"%s\" killed by \"%s\" using \"%s\" (%u)",
        info.targetName.c_str(),
        info.lastHitSrcName.c_str(),
        info.lastHitSkillName.c_str(),
        info.lastHitSkillId
    );

    APIDefs->Log(ELogLevel_INFO, "ArcIntegration", out);
}


// ============================================================
// LOGGER PRINCIPALE
// ============================================================

void LogArcEvent(EvCombatData* e, const char* sourceArea)
{
    if (!e || !e->ev) return;

    char out[32768];

    snprintf(out, sizeof(out),
        "\n================= ARC DPS EVENT =================\n"
        "AREA: %s\n"
        "TIME: %llu\n"
        "-----------------------------------------------\n"
        "SKILL: %u (%s)\n"
        "BUFF FLAGS: buff=%u | buffremove=%u (%s)\n"
        "ACTIVATION: %u (%s)\n"
        "STATECHANGE: %u (%s)\n"
        "IFF: %u (%s)\n"
        "-----------------------------------------------\n"
        "VALUE: %d | buff_dmg: %d | overstack: %u\n"
        "RESULT: %u\n"
        "-----------------------------------------------\n"
        "SRC_AGENT: %llu | instid=%u | master=%u\n"
        "SRC_SELF: %u | SRC_NAME=\"%s\"\n"
        "DST_AGENT: %llu | instid=%u | master=%u\n"
        "DST_SELF: %u | DST_NAME=\"%s\"\n"
        "-----------------------------------------------\n"
        "FLAGS: 90%%=%u | 50%%=%u | moving=%u | flanking=%u | shields=%u | offcycle=%u\n"
        "PADDING: pad61=%u pad62=%u pad63=%u pad64=%u\n"
        "=================================================\n",

        sourceArea,
        e->ev->time,

        e->ev->skillid,
        (e->skillname ? e->skillname : "(null)"),

        e->ev->buff,
        e->ev->is_buffremove,
        DecodeBuffRemove(e->ev->is_buffremove),

        e->ev->is_activation,
        DecodeActivation(e->ev->is_activation),

        e->ev->is_statechange,
        DecodeStateChange(e->ev->is_statechange),

        e->ev->iff,
        DecodeIFF(e->ev->iff),

        e->ev->value,
        e->ev->buff_dmg,
        e->ev->overstack_value,
        e->ev->result,

        e->ev->src_agent,
        e->ev->src_instid,
        e->ev->src_master_instid,
        (e->src ? e->src->self : 0),
        (e->src && e->src->name ? e->src->name : "(null)"),

        e->ev->dst_agent,
        e->ev->dst_instid,
        e->ev->dst_master_instid,
        (e->dst ? e->dst->self : 0),
        (e->dst && e->dst->name ? e->dst->name : "(null)"),

        e->ev->is_ninety,
        e->ev->is_fifty,
        e->ev->is_moving,
        e->ev->is_flanking,
        e->ev->is_shields,
        e->ev->is_offcycle,

        e->ev->pad61,
        e->ev->pad62,
        e->ev->pad63,
        e->ev->pad64
    );

    APIDefs->Log(ELogLevel_DEBUG, "ArcIntegration", out);
}


void LogArcEventCompact(EvCombatData* e, const char* area)
{
    if (!e || !e->ev) return;

    uint64_t srcAgent = e->ev->src_agent;
    uint64_t dstAgent = e->ev->dst_agent;

    const char* srcName = (e->src && e->src->name && strlen(e->src->name))
        ? e->src->name : "(unknown)";
    const char* dstName = (e->dst && e->dst->name && strlen(e->dst->name))
        ? e->dst->name : "0";

    uint32_t skill = e->ev->skillid;
    const char* skillName = (e->skillname ? e->skillname : "(null)");

    uint8_t state = e->ev->is_statechange;
    auto now = std::chrono::steady_clock::now();

    // ============================================================================
    // 0) FILTRO SPAM (buff onnipresenti)
    // ============================================================================
    bool spam =
        (skill == 53251) || // Extra Life Bonus
        (skill == 76065) ||
        (skill == 76082) ||
        (skill == 30528) ||
        (skill == 68119) ||
        (skill == 24007);

    if (!spam)
    {
        char dbg[512];
        snprintf(dbg, sizeof(dbg),
            "[DBG] area=%s | src=%llu \"%s\" | dst=%llu \"%s\" | skill=%u \"%s\" | val=%d buff=%d | state=%u",
            area,
            srcAgent, srcName,
            dstAgent, dstName,
            skill, skillName,
            e->ev->value, e->ev->buff_dmg,
            state
        );
        APIDefs->Log(ELogLevel_DEBUG, "ArcIntegration", dbg);
    }

    // ============================================================================
    // 1) TRACK TARGET
    // ============================================================================
    if (dstAgent != 0 && strcmp(dstName, "0") != 0)
    {
        auto& t = killTargets[dstAgent];
        t.targetName = dstName;
        t.lastSeenTime = now;

        if (IsRealDamageHit(e))
        {
            int dmg = (e->ev->value < 0) ? -e->ev->value : e->ev->buff_dmg;

            t.lastHitTime = now;
            t.lastHitDamage = dmg;
            t.lastHitSkillId = skill;
            t.lastHitSkillName = skillName;
            t.lastHitSrcAgent = srcAgent;
            t.lastHitSrcName = srcName;
            t.hitCount++;

            char dbg2[256];
            snprintf(dbg2, sizeof(dbg2),
                "[DBG-HIT] dmg=%d hits=%u on \"%s\"",
                dmg, t.hitCount, t.targetName.c_str());
            APIDefs->Log(ELogLevel_DEBUG, "ArcIntegration", dbg2);
        }
    }

    // ============================================================================
    // 2) CHECK KILL
    // ============================================================================
    if (dstAgent == 0 && strcmp(dstName, "0") == 0)
    {
        for (auto& kv : killTargets)
        {
            auto& t = kv.second;

            if (t.killLogged) continue;

            auto msSinceHit = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.lastHitTime).count();
            auto msSinceSeen = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.lastSeenTime).count();

            if (msSinceHit > MAX_KILL_WINDOW_MS) continue;

            bool died =
                msSinceHit > DEAD_TIMEOUT_MS &&
                msSinceSeen > DEAD_TIMEOUT_MS;

            if (!died) continue;

            // evita duplicati multi-canale
            if (msSinceSeen > DEAD_TIMEOUT_MS * 3)
                continue;

            // kill valida
            t.killLogged = true;

            char out[512];
            snprintf(out, sizeof(out),
                "[KILL] \"%s\" killed by \"%s\" using \"%s\" (%u) [hits=%u, lastHit=%lldms ago, lastSeen=%lldms ago]",
                t.targetName.c_str(),
                t.lastHitSrcName.c_str(),
                t.lastHitSkillName.c_str(),
                t.lastHitSkillId,
                t.hitCount,
                (long long)msSinceHit,
                (long long)msSinceSeen
            );

            APIDefs->Log(ELogLevel_INFO, "ArcIntegration", out);
        }
    }
}


void LogArcEventUltraCompact(EvCombatData* e, const char* area)
{
    if (!e || !e->ev) return;

    uint64_t dstAgent = e->ev->dst_agent;
    if (dstAgent != 0) return;

    auto now = std::chrono::steady_clock::now();

    for (auto& kv : killTargets)
    {
        KillTrack& t = kv.second;
        if (t.killLogged) continue;

        auto msHit = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.lastHitTime).count();
        auto msSeen = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.lastSeenTime).count();

        if (msHit > MAX_KILL_WINDOW_MS) continue;

        bool died = (msHit > DEAD_TIMEOUT_MS && msSeen > DEAD_TIMEOUT_MS);
        if (!died) continue;

        t.killLogged = true;

        LogKillCompact(t);
    }
}
