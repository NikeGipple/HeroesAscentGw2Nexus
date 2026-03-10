// Logger.cpp

#include "Logger.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <chrono>
#include <deque>
#include <mutex>
#include <atomic>
#include <Windows.h>

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
    if (!APIDefs) return;
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
    if (!APIDefs) return;
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
    if (!APIDefs) return;
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

// ============================================================
// PROBE (WIDE DEBUG)
// ============================================================

static std::atomic<bool> gProbeEnabled{ false };
static std::atomic<uint64_t> gProbeSeen{ 0 };
static std::atomic<uint64_t> gProbeStored{ 0 };
static std::atomic<uint64_t> gProbeFiltered{ 0 };

struct ArcProbeEvt {
    uint64_t t_ms;          // GetTickCount64
    uint64_t arc_time;      // e->ev->time
    char area[8];

    uint8_t statechange;
    uint8_t activation;
    uint8_t buff;
    uint8_t buffremove;
    uint8_t result;

    uint32_t skillid;
    int32_t value;
    int32_t buff_dmg;
    uint32_t overstack;

    uint64_t src_agent;
    uint64_t dst_agent;

    uint8_t src_self;
    uint8_t dst_self;

    char src_name[64];
    char dst_name[64];
    char skillname[96];
};

static std::deque<ArcProbeEvt> gProbe;
static std::mutex gProbeMx;

static uint32_t gProbeWindowMs = 20000; // 20s (wide)
static size_t   gProbeMaxEvents = 5000;  // max righe (wide)

void ProbeSetEnabled(bool enabled) {
    gProbeEnabled.store(enabled, std::memory_order_relaxed);
}

bool ProbeIsEnabled() {
    return gProbeEnabled.load(std::memory_order_relaxed);
}

void ProbeSetWindowMs(uint32_t ms) { gProbeWindowMs = (ms < 1000 ? 1000 : ms); }
void ProbeSetMaxEvents(size_t n) { gProbeMaxEvents = (n < 200 ? 200 : n); }

void ProbeClear()
{
    std::scoped_lock lk(gProbeMx);
    gProbe.clear();
    gProbeSeen.store(0, std::memory_order_relaxed);
    gProbeStored.store(0, std::memory_order_relaxed);
    gProbeFiltered.store(0, std::memory_order_relaxed);
}

static inline void SafeCopy(char* dst, size_t dstSz, const char* src)
{
    if (!dst || dstSz == 0) return;
    dst[0] = '\0';
    if (!src) return;
    strncpy_s(dst, dstSz, src, _TRUNCATE);
}

void ProbePush(EvCombatData* e, const char* area)
{
    if (!ProbeIsEnabled()) return;
    if (!e || !e->ev) return;

    gProbeSeen++;

    const bool srcSelf = (e->src && e->src->self == 1);
    const bool dstSelf = (e->dst && e->dst->self == 1);

    const uint8_t sc = e->ev->is_statechange;
    const uint8_t act = e->ev->is_activation;

    const bool isActivation = (sc == 0 && act != 0);
    const bool isState = (sc != 0);
    const bool isBuffEvt = (e->ev->buff == 1) || (e->ev->is_buffremove != 0);

    // WIDE DEBUG: per ora tieni tutto
    const bool interesting = true;

    // (quando vorrai tornare a filtrare, rimetti questo)
    /*
    const bool interesting =
        isState ||
        isActivation ||
        (isBuffEvt && (srcSelf || dstSelf)) ||
        (e->ev->result != 0) ||
        (srcSelf || dstSelf);
    */

    if (!interesting) {
        gProbeFiltered++;
        return;
    }

    gProbeStored++;

    ArcProbeEvt pe{};
    pe.t_ms = GetTickCount64();
    pe.arc_time = e->ev->time;
    SafeCopy(pe.area, sizeof(pe.area), area ? area : "");

    pe.statechange = sc;
    pe.activation = act;
    pe.buff = e->ev->buff;
    pe.buffremove = e->ev->is_buffremove;
    pe.result = e->ev->result;

    pe.skillid = e->ev->skillid;
    pe.value = e->ev->value;
    pe.buff_dmg = e->ev->buff_dmg;
    pe.overstack = e->ev->overstack_value;

    pe.src_agent = e->ev->src_agent;
    pe.dst_agent = e->ev->dst_agent;

    pe.src_self = (uint8_t)(srcSelf ? 1 : 0);
    pe.dst_self = (uint8_t)(dstSelf ? 1 : 0);

    SafeCopy(pe.src_name, sizeof(pe.src_name), (e->src && e->src->name) ? e->src->name : "");
    SafeCopy(pe.dst_name, sizeof(pe.dst_name), (e->dst && e->dst->name) ? e->dst->name : "");
    SafeCopy(pe.skillname, sizeof(pe.skillname), (e->skillname && e->skillname[0]) ? e->skillname : "");

    std::scoped_lock lk(gProbeMx);
    gProbe.push_back(pe);

    while (gProbe.size() > gProbeMaxEvents)
        gProbe.pop_front();

    while (!gProbe.empty() && (pe.t_ms - gProbe.front().t_ms) > gProbeWindowMs)
        gProbe.pop_front();
}
void ProbeDump(const char* reason)
{
    if (!APIDefs) return;

    std::scoped_lock lk(gProbeMx);

    APIDefs->Log(ELogLevel_WARNING, "TOME-PROBE", "================ DUMP START ================");
    APIDefs->Log(ELogLevel_WARNING, "TOME-PROBE", (reason ? reason : "(no reason)"));

    char stats[256];
    sprintf_s(stats, sizeof(stats),
        "stats: seen=%llu stored=%llu filtered=%llu buf=%zu window=%ums max=%zu",
        (unsigned long long)gProbeSeen.load(),
        (unsigned long long)gProbeStored.load(),
        (unsigned long long)gProbeFiltered.load(),
        gProbe.size(),
        gProbeWindowMs,
        gProbeMaxEvents
    );
    APIDefs->Log(ELogLevel_WARNING, "TOME-PROBE", stats);

    for (auto& x : gProbe)
    {
        char line[900];
        sprintf_s(line, sizeof(line),
            "[%s] t=%llu arc=%llu sc=%u(%s) act=%u(%s) buff=%u br=%u(%s) res=%u "
            "skill=%u \"%s\" val=%d bd=%d ov=%u "
            "src=%llu self=%u \"%s\" dst=%llu self=%u \"%s\"",
            x.area,
            (unsigned long long)x.t_ms,
            (unsigned long long)x.arc_time,
            x.statechange, DecodeStateChange(x.statechange),
            x.activation, DecodeActivation(x.activation),
            x.buff,
            x.buffremove, DecodeBuffRemove(x.buffremove),
            x.result,
            x.skillid, x.skillname,
            (int)x.value, (int)x.buff_dmg, x.overstack,
            (unsigned long long)x.src_agent, x.src_self, x.src_name,
            (unsigned long long)x.dst_agent, x.dst_self, x.dst_name
        );

        APIDefs->Log(ELogLevel_WARNING, "TOME-PROBE", line);
    }

    APIDefs->Log(ELogLevel_WARNING, "TOME-PROBE", "================= DUMP END =================");
}
void ProbeAutoDumpIfInteresting(EvCombatData* e, const char* area)
{
    if (!ProbeIsEnabled()) return;
    if (!APIDefs) return;
    if (!e || !e->ev) return;

    static uint64_t lastDumpMs = 0;
    const uint64_t nowMs = GetTickCount64();
    if (nowMs - lastDumpMs < 2000) return;

    const bool srcSelf = (e->src && e->src->self == 1);

    const uint8_t sc = e->ev->is_statechange;
    const uint8_t act = e->ev->is_activation;

    // Trigger “sensati” per non spam:
    // - REWARD (17)
    // - activation START (1) dal player
    if (sc == 17 /*REWARD*/)
    {
        char reason[256];
        sprintf_s(reason, sizeof(reason),
            "[AUTO][REWARD] area=%s skillid=%u name=%s val=%d bd=%d",
            area ? area : "",
            e->ev->skillid,
            (e->skillname ? e->skillname : ""),
            (int)e->ev->value,
            (int)e->ev->buff_dmg
        );
        lastDumpMs = nowMs;
        ProbeDump(reason);
        return;
    }

    if (sc == 0 && act == 1 /*START*/ && srcSelf)
    {
        char reason[256];
        sprintf_s(reason, sizeof(reason),
            "[AUTO][ACT_START] area=%s skillid=%u name=%s",
            area ? area : "",
            e->ev->skillid,
            (e->skillname ? e->skillname : "")
        );
        lastDumpMs = nowMs;
        ProbeDump(reason);
        return;
    }
}



