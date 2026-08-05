// Dock Distribution Office
// Copyright (C) 2026 Ultimate-Universe
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

// DockDistributionOffice.cpp
// Dock Distribution Office - v1.0.0 release.
//
// v1.0.0 is the first public release. It combines the manager-owned ship
// distribution controller, live task-policy refresh, route-pinned domestic
// source reserves, 99.9% loading, complete-unload holding, direct repeat cycles,
// save/load route reconstruction, safe depot transfers, and final Workshop asset
// identifiers DockDistributionOfficeSmall / DockDistributionOfficeMedium.
//
// v0.7.0 removes the road Distribution Office planner from dispatch decisions.
// The native Distribution Office panel and assignment records remain the player-facing
// configuration layer, but a mod-owned controller now:
//   * reads source/destination tasks and their resource percentages;
//   * treats destination percentages as dispatch triggers;
//   * selects only compatible ships physically berthed in the Distribution Dock;
//   * writes a native source -> destination -> home ship schedule directly;
//   * loads to the configured ship fraction and waits until fully unloaded;
//   * reserves destination/resource jobs in a runtime manager array;
//   * reconstructs that array from native ship routes after a save is loaded.
//
// No external sidecar is required in this first manager branch. Standing tasks and active
// ship routes are already serialized by the game; the controller rebuilds its pointer-free
// ownership/reservation state from those native records after load.
// The road Distribution Office planner is never called for marked Distribution Docks.
// Tesmio Loader plugin API v3.
//
// This branch keeps the proven native Distribution Office UI, assignment
// vector, harbour selector and border controls, but it does not invoke the
// road Distribution Office planner for marked docks. Native ship pathfinding,
// harbour cargo transfer, and the ordinary vehicle state machine still execute
// the generated route.
//
// Target: supplied SOVIET64.exe, PE timestamp 2026-03-25 16:13 UTC.
// Hooked entry points and cargo helpers are byte-guarded. The six unhooked
// direct-route helpers are checked for readable executable memory at startup;
// full prologue signatures remain a future hardening task.

typedef __SIZE_TYPE__ usize;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long longlong;

#define TSM_API_VERSION 3u
#define EXPORT extern "C" __declspec(dllexport)
extern "C" int _fltused = 0;

struct TsmPluginInfo { const char* name; const char* version; };

struct TsmHost {
    unsigned apiVersion;
    unsigned structSize;
    void* exeModule;
    u8* exeBase;
    usize exeSize;
    void* engineModule;
    const char* baseDir;
    const char* pluginDir;
    void (*log)(const char* fmt, ...);
    void** (*findIatSlot)(void* module, const char* dll, const char* fn);
    int (*patchIat)(void* module, const char* dll, const char* fn,
                    void* detour, void** original, const char* label);
    int (*installInlineHook)(void* target, void* detour, void** trampoline,
                             const u8* expect, usize stolen,
                             const char* label);
    u8* (*allocNear)(u8* anchor, usize size);
    int (*readablePtr)(const void* p, usize n);
    long (*faultFilter)(const char* what, void* exceptionPointers);
    int (*configInt)(const char* iniName, const char* section,
                     const char* key, int fallback);
    int (*configString)(const char* iniName, const char* section,
                        const char* key, char* out, int outSize,
                        const char* fallback);
    int (*provide)(const char* service, unsigned version, const void* iface);
    const void* (*consume)(const char* service, unsigned version);
};

static const TsmHost* H = 0;
static u8* EXE = 0;

// --------------------------------------------------------------------- target layout

#define RVA_BUILDING_DISPATCH 0x00139A80u // FUN_140139a80(game)
#define RVA_ROAD_DO_DISPATCH  0x001C5FE0u // FUN_1401c5fe0(game, office)
#define RVA_SHIP_PANEL        0x007A0B70u // FUN_1407a0b70(ui, window)
#define RVA_ROAD_DO_PANEL     0x00741050u // FUN_140741050(ui, window, descriptor, clipTop)
#define RVA_DO_ADD_SELECTOR   0x002B7840u // FUN_1402b7840(game, ..., ..., scale)
#define RVA_VEHICLE_UPDATE    0x00699310u // FUN_140699310(vehicle)
#define RVA_HIGHLIGHT_BUILDING 0x0040BCC0u // FUN_14040bcc0(...)
#define RVA_ASSIGN_PUSH       0x0008E050u // vector<assignment*>::push_back
#define RVA_ASSIGN_INIT       0x00151230u // initialise 0x198 assignment config
#define RVA_QWORD_PUSH        0x000B0E90u // vector<void*>::push_back
#define RVA_CARGO_CAPACITY     0x006BDCA0u // FUN_1406bdca0(vehicle, resource, ...)
#define RVA_CARGO_AMOUNT       0x006BE030u // FUN_1406be030(vehicle, resource, includeChildren)
#define RVA_BUILDING_STATS_BASE  0x001E82E0u // FUN_1401e82e0(game, outPair, building, resource, mode, scratch)
#define RVA_BUILDING_HAS_EXTRA   0x001E5010u // FUN_1401e5010(game, building, resource, mode)
#define RVA_BUILDING_STATS_EXTRA 0x001E9270u // FUN_1401e9270(game, outPair, building, resource, mode, scratch)
#define RVA_ROUTE_TARGET_RESIZE 0x001FEE90u // vector<void*>::resize
#define RVA_ROUTE_CONFIG_RESIZE 0x0044A0E0u // vector<0x198 config>::resize
#define RVA_ROUTE_CONFIG_INIT   0x0067CC20u // initialise one route config for vehicle
#define RVA_CONFIG_HALF_COPY    0x001E0F50u // deep-copy one 0xC8 config half
#define RVA_ROUTE_REFRESH       0x006B8DA0u // recalculate native vehicle route target/path
#define RVA_VEHICLE_ROUTE_START 0x004299E0u // activate current native schedule

#define W_BUILDING           0x0240u
#define W_CLIP_TOP           0x0504u
#define B_TYPEDESC           0x0318u
#define B_ASSIGN_BEGIN       0x0D38u
#define B_ASSIGN_END         0x0D40u
#define B_ASSIGN_DIRTY       0x0D50u
#define B_ASSIGN_TIMER       0x0FC0u
#define B_FINISHED           0x0604u
#define B_DEMOLISHED         0x0EA8u
#define B_VEH_BEGIN          0x0C70u
#define B_VEH_END            0x0C78u
#define B_ASSIGNED_BEGIN     0x0C88u
#define B_ASSIGNED_END       0x0C90u
#define B_ASSIGNED_CAP       0x0C98u
#define B_CONN_BEGIN         0x0A10u
#define B_CONN_END           0x0A18u
#define B_CONN_CAP           0x0A20u
#define B_BACKREF_BEGIN      0x0D90u
#define B_BACKREF_END        0x0D98u
#define B_BACKREF_CAP        0x0DA0u
#define T_BUILDING_TYPE      0x0360u
#define T_BUILDING_SUBTYPE   0x0364u

#define G_ASSIGN_HOME        0x0D298u
#define G_BUILDINGS          0x11B08u
#define G_BUILDINGS_END      0x11B10u
#define G_VEHICLES           0x12810u
#define G_VEHICLES_END       0x12818u
#define G_CACHED_PATH_OK     0x0F5B0u
#define G_CACHED_CANDIDATE   0x0F5B8u
#define G_WESTERN_NODE       0x11AF8u
#define G_SOVIET_NODE        0x11B00u

#define TYPE_EXTERNAL        0x14u
#define TYPE_CARGO_STATION   0x00u
#define TYPE_SHIP_DOCK       0x2Au
#define TYPE_ROAD_DO         0x2Bu
#define SUBTYPE_SHIP         0x13u

// Shared UI globals and the exact two icon slots used by the native ship
// schedule editor for Soviet/Western overseas destinations.
#define G_CLICK_FLAG_RVA      0x00A54E91u
#define G_MOUSE_OBJECT_RVA    0x00A54B90u
#define G_PANEL_RVA           0x009BE060u
#define G_PANEL_POS_RVA       0x009BE2F0u
#define G_PANEL_SIZE_RVA      0x009BE2E8u
#define G_PANEL_PAD_RVA       0x009BE2F8u
#define G_PANEL_COLOR_RVA     0x009BE30Cu
#define G_TECHNIQUE_RVA       0x009EAD08u
#define G_PANEL_FULLSIZE_RVA  0x00909F70u
#define G_DPI_RVA             0x00992088u
#define G_LAYOUT_ROW_RVA      0x0090A9E4u // 35
#define G_LAYOUT_XBACK_RVA    0x0090A8E4u // 15
#define G_LAYOUT_XINDENT_RVA  0x0090ABB0u // 110
#define G_PANEL_FONTMGR_RVA   0x00996FB0u
#define G_PANEL_FONT_RVA      0x00995220u
#define GAME_SOVIET_ICON      0x0B2C0u
#define GAME_WESTERN_ICON     0x0B2C8u
#define GAME_ASSIGN_MESSAGE   0x0D5A0u
#define GAME_ASSIGN_TIP_TIME  0x0F5A0u
#define W_POS_X               0x0004u
#define W_POS_Y               0x0008u
#define W_OFF_X               0x0028u
#define W_OFF_Y               0x002Cu
#define W_BOTTOM              0x0250u
#define V_HOME                0x04F8u
#define V_CURRENT_TARGET      0x0528u
#define V_ROUTE_CONFIG_BEGIN  0x0638u
#define V_ROUTE_CONFIG_END    0x0640u
#define V_ROUTE_BEGIN         0x0680u
#define V_ROUTE_END           0x0688u
#define V_ROUTE_INDEX         0x0698u
#define V_ROUTE_DIRTY         0x0D3Au

// Verified from FUN_140151230/FUN_140151350 and the native assignment-row UI
// FUN_140741050 -> FUN_1407ebe30 -> FUN_1407ec1d0.  Each 0x198-byte
// assignment config contains two 0xC8-byte halves: load at +0x00 and unload
// at +0xC8.  Within either half the enable byte is +0x00, threshold is +0x04,
// and the vector<resource_entry[0x10]> is +0x08/+0x10/+0x18.
#define CFG_HALF_SIZE       0x0C8u
#define CFG_ENABLED         0x000u
#define CFG_THRESHOLD       0x004u
#define CFG_RES_BEGIN       0x008u
#define CFG_RES_END         0x010u
#define CFG_RES_CAP         0x018u
#define CFG_LOAD_HALF       0x000u
#define CFG_UNLOAD_HALF     0x0C8u

// --------------------------------------------------------------------- configuration/state

static int g_enabled = 1;
static int g_probe = 1;
static int g_logAssignments = 1;
static int g_scanBytes = 8192;
static int g_distributionMode = 1;
static int g_hotkeyEnabled = 1;
static int g_toggleVk = 0x77; // F8
static int g_lastKeyDown = 0;
static int g_loggedHotkeyFailure = 0;
static int g_loggedPanelOpen = 0;
static int g_selectorEnabled = 1;
static int g_overseasEnabled = 1;
static int g_overseasHotkeys = 0;
static int g_overseasButtons = 1;
static int g_autoSeedOverseas = 0;
static int g_selectorDiagnostics = 1;
static int g_dispatchEnabled = 1;
static int g_vehicleLifecycleEnabled = 0;
static int g_dispatchDiagnostics = 1;
static int g_dispatchStride = 1;
static int g_fleetStableTicks = 300;
static int g_plannerCooldownTicks = 300;
static volatile int g_plannerBusy = 0;
static int g_customPolicyEnabled = 1;
static int g_customPolicyDiagnostics = 1;
static int g_minDepartureLoadPermille = 999;
static int g_waitUntilEmpty = 1;
static int g_blockPlannerWhileActive = 0;
static int g_taskReservations = 1;
static int g_reconstructAssignedVoyages = 1;
static int g_ownershipGraceTicks = 1800;
static int g_managerEnabled = 1;
static int g_idleControllerTicks = 300;
static int g_activeControllerTicks = 15;
static int g_uiEditQuietTicks = 300;
static void* g_uiEditingOffice = 0;
static u32 g_uiEditingHeartbeatTick = 0;
static int g_nativeCargoProbeFaults = 0;
static int g_worldScanTicks = 600;
static int g_loadGraceTicks = 1200;
static int g_assignmentStableTicks = 300;
static float g_triggerEpsilon = 0.002f;
static int g_maxDispatchPerPass = 3;
static int g_reconstructWorldVehicles = 1;
static int g_savedTaskHalfFallback = 1;
static int g_taskParseDiagnostics = 1;
static int g_directRepeatEnabled = 1;
static int g_sourceReserveControlTicks = 1;
static int g_activePolicyRefreshTicks = 300;
// Legacy planner-gate fields retained only so the old, unreachable helper code
// remains buildable in this first manager branch. RunPlannerForOffice never
// calls those helpers.
static int g_demandGateEnabled = 0;
static int g_plannerBurstCalls = 0;
static int g_idleDemandRecheckTicks = 0;
static int g_activeDemandRecheckTicks = 0;
static float g_demandEpsilon = 0.0f;
static int g_demandLoadGraceTicks = 0;
static int g_demandAssignmentStableTicks = 0;
static u32 g_lastWorldScanTick = 0;
static u32 g_tick = 0;
static int g_buttonXOffset = 394;
static int g_buttonSourceYOffset = 290;
static int g_buttonDestinationYOffset = 336;
static int g_buttonSize = 42;
static int g_buttonGap = 4;
static void* g_buttonHome = 0;
static int g_buttonCapture = 0; // 0=none, 1=Soviet, 2=Western
static int g_lastLeftButtonDown = 0;
static int g_buttonSuppressFrames = 0;
static int g_sovietVk = 0x75;  // F6
static int g_westernVk = 0x76; // F7
static int g_lastSovietDown = 0;
static int g_lastWesternDown = 0;
static void* g_lastSelectorCandidate = 0;
static void* g_activeGame = 0;
static char g_marker[96] = "DockDistributionOffice";

#define MAX_CUSTOM_DESCRIPTORS 8
#define MAX_NEGATIVE_DESCRIPTORS 512
#define MAX_TRACKED_BUILDINGS 32
#define MAX_ACTIVE_JOBS 32
#define MAX_MANAGER_TASKS 64

static void* g_customDescriptors[MAX_CUSTOM_DESCRIPTORS];
static int g_customDescriptorCount = 0;
static void* g_negativeDescriptors[MAX_NEGATIVE_DESCRIPTORS];
static int g_negativeDescriptorCount = 0;

struct ActiveShipJob {
    void* ship;
    void* resource;
    void* sourceTarget;
    void* destinationTarget;
    void* homeTarget;
    int sourceIndex;
    int destinationIndex;
    int homeIndex;
    int unloadArmed;
    int emptyConfirmed;
    int lastRouteIndex;
    float cargoCapacity;
    float targetLoadFraction;
    float targetLoadAmount;
    float sourceReserveFraction;
    float destinationTriggerFraction;
    int sourceExternal;
    int sourceReservePaused;
    int sourceRoutePinned;
    int directRepeatPending;
    int directRepeatCount;
    u32 startedTick;
    u32 missingSinceTick;
    int lastOwnershipState; // 1=in port, 2=assigned/away, 0=grace
};

struct TrackedBuilding {
    void* building;
    int assignmentCount;
    int overseasSeedAttempted;
    u32 dispatchRuns;
    u32 lastNoJobLogTick;
    int lastVehicleCount;
    void* lastVehicleBegin;
    void* lastVehicleEnd;
    int lastOwnedVehicleCount;
    int lastInPortVehicleCount;
    int lastAssignedVehicleCount;
    u32 lastFleetChangeTick;
    u32 lastPlannerTick;
    u32 lastJobTick;
    u32 lastPolicyCompletionTick;
    u32 lastAssignedScanTick;
    u32 nextDemandCheckTick;
    u32 demandScans;
    u32 plannerSkips;
    u32 discoveredTick;
    u32 lastAssignmentChangeTick;
    int lastDemandState; // -1=load grace, 0=sleeping, 1=burst active
    int plannerBurstRemaining;
    u32 nextControllerTick;
    u32 lastControllerLogTick;
    u32 lastWorldVehicleScanTick;
    u32 lastTaskPolicyRefreshTick;
    u32 lastPolicyRefreshHeartbeatTick;
    ActiveShipJob activeJobs[MAX_ACTIVE_JOBS];
    int activeJobCount;
};
static TrackedBuilding g_tracked[MAX_TRACKED_BUILDINGS];
static int g_trackedCount = 0;

// --------------------------------------------------------------------- native call types

typedef void (*FnOne)(void* value);
typedef void (*FnTwo)(void* first, void* second);
typedef void (*FnValidHighlight)(void* game, void* building, float scale);
typedef void (*FnShipPanel)(void* ui, void* window);
typedef void (*FnRoadDoPanel)(void* ui, void* window, void* descriptor, float clipTop);
typedef void (*FnDoAddSelector)(void* game, u64 arg2, u64 arg3, float scale);
typedef void (*FnAssignPush)(u64* vector, void** value);
typedef void* (*FnAssignInit)(void* config);
typedef void (*FnQwordPush)(u64* vector, void** value);
typedef void* (*FnMalloc)(usize size);
typedef void* (*FnGetModuleHandleA)(const char* moduleName);
typedef void* (*FnGetProcAddress)(void* module, const char* procName);
typedef short (*FnGetAsyncKeyState)(int virtualKey);
typedef int (*FnVirtualProtect)(void* address, usize size, u32 newProtect, u32* oldProtect);
typedef int (*FnFlushInstructionCache)(void* process, const void* address, usize size);
typedef void* (*FnGetCurrentProcess)(void);
typedef void (*FnPanelDraw)(void* panel, float u0, float v0, float u1, float v1, float angle, int alpha);
typedef void* (*FnGetMouseSolid)(void* input, void* returnBuffer);
typedef int (*FnPanelCollision)(void* panel, void* mouseVector, float halfX, float halfY);
typedef void (*FnPrintLeftUnicode)(void* manager, void* font, float x, float y, u32 colour, const wchar_t* fmt, ...);
typedef float (*FnCargoCapacity)(void* vehicle, void* resource, char strict, char includeLinked);
typedef float (*FnCargoAmount)(void* vehicle, void* resource, char includeLinked);
typedef u64* (*FnBuildingStatsBase)(longlong game, u64* outputPair, u64 building, u64 resource, char mode, u64* scratch);
typedef char (*FnBuildingHasExtra)(longlong game, longlong building, longlong resource, char mode);
typedef longlong* (*FnBuildingStatsExtra)(longlong game, longlong* outputPair, longlong building, longlong resource, char mode, longlong* scratch);
typedef void (*FnVectorResizeQword)(longlong* vector, u64 count);
typedef void (*FnVectorResizeConfig)(longlong* vector, u64 count);
typedef void (*FnRouteConfigInit)(longlong vehicle, u8* config);
typedef u8* (*FnConfigHalfCopy)(u8* destination, u8* source);
typedef u64 (*FnRouteRefresh)(void** vehicle, char arg2, char arg3, void** arg4);
typedef void (*FnVehicleRouteStart)(longlong game, longlong vehicle, char immediate);

static FnOne o_BuildingDispatch = 0;
static FnTwo f_RoadDoDispatch = 0;
static FnShipPanel o_ShipPanel = 0;
static FnRoadDoPanel f_RoadDoPanel = 0;
static FnDoAddSelector o_DoAddSelector = 0;
static FnOne o_VehicleUpdate = 0;
static FnValidHighlight f_ValidHighlight = 0;
static u8* g_highlightAddress = 0;
static FnAssignPush f_AssignPush = 0;
static FnAssignInit f_AssignInit = 0;
static FnQwordPush f_QwordPush = 0;
static FnMalloc f_Malloc = 0;
static FnGetAsyncKeyState f_GetAsyncKeyState = 0;
static FnVirtualProtect f_VirtualProtect = 0;
static FnFlushInstructionCache f_FlushInstructionCache = 0;
static FnGetCurrentProcess f_GetCurrentProcess = 0;
static FnPanelDraw f_PanelDraw = 0;
static FnGetMouseSolid f_GetMouseSolid = 0;
static FnPanelCollision f_PanelCollision = 0;
typedef int (*FnPanelCollisionRect)(void* panel, void* mouseVector, float left, float right, float top, float bottom);
static FnPanelCollisionRect f_PanelCollisionRect = 0;
static FnPrintLeftUnicode f_PrintLeftUnicode = 0;
static FnCargoCapacity f_CargoCapacity = 0;
static FnCargoAmount f_CargoAmount = 0;
static FnBuildingStatsBase f_BuildingStatsBase = 0;
static FnBuildingHasExtra f_BuildingHasExtra = 0;
static FnBuildingStatsExtra f_BuildingStatsExtra = 0;
static FnVectorResizeQword f_RouteTargetResize = 0;
static FnVectorResizeConfig f_RouteConfigResize = 0;
static FnRouteConfigInit f_RouteConfigInit = 0;
static FnConfigHalfCopy f_ConfigHalfCopy = 0;
static FnRouteRefresh f_RouteRefresh = 0;
static FnVehicleRouteStart f_VehicleRouteStart = 0;

// --------------------------------------------------------------------- minimal helpers

static usize StrLen(const char* s) {
    usize n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static int ToLowerAscii(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int EqualsNoCase(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (ToLowerAscii((unsigned char)*a) != ToLowerAscii((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int BytesEqual(const u8* a, const u8* b, usize n) {
    if (!a || !b) return 0;
    for (usize i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static int IsReadable(const void* p, usize n);
static void ZeroBytes(void* memory, usize count);


struct ResolvedHookSite {
    u8* target;
    const u8* expected;
    int chainPointerPatch;
    void* previousDetour;
};

static u8* FindUniqueExeSignature(const u8* signature, usize length, int* matchCount) {
    if (matchCount) *matchCount = 0;
    if (!EXE || !H || !signature || length < 8 || H->exeSize < length) return 0;
    u8* found = 0;
    const usize page = 0x1000u;
    for (usize pageOff = 0; pageOff < H->exeSize; pageOff += page) {
        usize bytes = H->exeSize - pageOff;
        if (bytes > page) bytes = page;
        // Scan a small overlap so a signature crossing a page boundary is not missed.
        usize readable = bytes;
        if (pageOff + readable + length <= H->exeSize) readable += length - 1;
        if (!IsReadable(EXE + pageOff, readable)) continue;
        usize limit = readable >= length ? readable - length + 1 : 0;
        for (usize i = 0; i < limit; ++i) {
            u8* candidate = EXE + pageOff + i;
            if (!BytesEqual(candidate, signature, length)) continue;
            if (!found) found = candidate;
            if (matchCount) ++*matchCount;
            if (matchCount && *matchCount > 1) return found;
        }
    }
    return found;
}

static int IsTesmioAbsoluteJump(const u8* p) {
    return p && IsReadable(p, 14) && p[0] == 0xFF && p[1] == 0x25 &&
           p[2] == 0 && p[3] == 0 && p[4] == 0 && p[5] == 0;
}

static int ResolveHookSite(u32 preferredRva, const u8* expected, usize stolen,
                           const char* label, ResolvedHookSite* site) {
    if (!site || !EXE || !expected || stolen < 14) return 0;
    ZeroBytes(site, sizeof(ResolvedHookSite));
    u8* preferred = EXE + preferredRva;
    if (IsReadable(preferred, stolen) && BytesEqual(preferred, expected, stolen)) {
        site->target = preferred;
        site->expected = expected;
        if (H && H->log) H->log("DockDistributionOffice  signature ok      %s exe+0x%X", label, preferredRva);
        return 1;
    }

    // Tesmio Loader writes exactly `FF 25 00 00 00 00 <qword target>`.
    // Patch only that qword and call the previous detour from our hook. This
    // avoids copying a RIP-relative jump into a trampoline, which the loader's
    // simple prologue copier cannot relocate safely.
    if (IsTesmioAbsoluteJump(preferred) && IsReadable(preferred + 6, sizeof(void*))) {
        void* previous = *(void**)(preferred + 6);
        if (previous && IsReadable(previous, 16)) {
            site->target = preferred;
            site->chainPointerPatch = 1;
            site->previousDetour = previous;
            if (H && H->log) H->log("DockDistributionOffice  %s already detoured; chain pointer=%p", label, previous);
            return 1;
        }
    }

    int matches = 0;
    u8* scanned = FindUniqueExeSignature(expected, stolen, &matches);
    if (scanned && matches == 1) {
        site->target = scanned;
        site->expected = expected;
        if (H && H->log) H->log("DockDistributionOffice  signature relocated %s exe+0x%llX",
                                 label, (u64)(scanned - EXE));
        return 1;
    }
    if (H && H->log) {
        H->log("DockDistributionOffice  hook resolve FAILED %s preferred=exe+0x%X signature_matches=%d",
               label, preferredRva, matches);
    }
    return 0;
}

static int InstallResolvedHook(const ResolvedHookSite* site, void* detour,
                               void** original, usize stolen, const char* label) {
    if (!site || !site->target || !detour || !original) return 0;
    if (!site->chainPointerPatch) {
        return H->installInlineHook(site->target, detour, original, site->expected,
                                    stolen, label);
    }
    if (!f_VirtualProtect || !f_FlushInstructionCache || !f_GetCurrentProcess) {
        if (H && H->log) H->log("DockDistributionOffice  hook chain FAILED %s: kernel patch imports unavailable", label);
        return 0;
    }
    u32 oldProtect = 0;
    if (!f_VirtualProtect(site->target + 6, sizeof(void*), 0x40u, &oldProtect)) {
        if (H && H->log) H->log("DockDistributionOffice  hook chain FAILED %s: VirtualProtect", label);
        return 0;
    }
    *original = site->previousDetour;
    *(void**)(site->target + 6) = detour;
    u32 ignored = 0;
    f_VirtualProtect(site->target + 6, sizeof(void*), oldProtect, &ignored);
    f_FlushInstructionCache(f_GetCurrentProcess(), site->target, 14);
    if (H && H->log) H->log("hook ok      %-22s target=%p chained=%p", label, site->target, *original);
    return 1;
}


static u8* ResolveCallable(u32 preferredRva, const u8* expected, usize length,
                           const char* label) {
    if (!EXE || !expected || length < 8) return 0;
    u8* preferred = EXE + preferredRva;
    if (IsReadable(preferred, length) && BytesEqual(preferred, expected, length)) {
        if (H && H->log) H->log("DockDistributionOffice  signature ok      %s exe+0x%X", label, preferredRva);
        return preferred;
    }
    if (IsTesmioAbsoluteJump(preferred)) {
        if (H && H->log) H->log("DockDistributionOffice  callable %s already detoured; using chained entry", label);
        return preferred;
    }
    int matches = 0;
    u8* scanned = FindUniqueExeSignature(expected, length, &matches);
    if (scanned && matches == 1) {
        if (H && H->log) H->log("DockDistributionOffice  signature relocated %s exe+0x%llX",
                                 label, (u64)(scanned - EXE));
        return scanned;
    }
    if (H && H->log) H->log("DockDistributionOffice  callable resolve FAILED %s matches=%d", label, matches);
    return 0;
}

static int IsCanonicalUserRange(const void* p, usize n) {
    const u64 first = (u64)p;
    const u64 userMax = 0x00007FFFFFFFFFFFull;
    if (first < 0x10000ull || first > userMax) return 0;
    if (n == 0) return 1;
    const u64 span = (u64)n - 1ull;
    if (span > userMax - first) return 0;
    return 1;
}

static int IsReadable(const void* p, usize n) {
    // readablePtr is a second-stage page check, not an address-sanity check.
    // Reject native sentinels such as -1, kernel/non-canonical addresses, and
    // ranges that wrap before asking the host to inspect the memory mapping.
    return H && H->readablePtr && IsCanonicalUserRange(p, n) && H->readablePtr(p, n);
}

static int ContainsAscii(const u8* p, usize n, const char* needle) {
    usize m = StrLen(needle);
    if (!p || !m || n < m) return 0;
    for (usize i = 0; i + m <= n; ++i) {
        if (BytesEqual(p + i, (const u8*)needle, m)) return 1;
    }
    return 0;
}

static int ContainsUtf16Ascii(const u8* p, usize n, const char* needle) {
    usize m = StrLen(needle);
    if (!p || !m || n < m * 2) return 0;
    for (usize i = 0; i + m * 2 <= n; ++i) {
        usize j = 0;
        for (; j < m; ++j) {
            if (p[i + j * 2] != (u8)needle[j] || p[i + j * 2 + 1] != 0) break;
        }
        if (j == m) return 1;
    }
    return 0;
}

static void* ReadPointer(void* base, usize offset) {
    u8* p = (u8*)base;
    if (!p || !IsReadable(p + offset, sizeof(void*))) return 0;
    return *(void**)(p + offset);
}

static int ReadInt(void* base, usize offset, int* value) {
    u8* p = (u8*)base;
    if (!p || !value || !IsReadable(p + offset, sizeof(int))) return 0;
    *value = *(int*)(p + offset);
    return 1;
}

static void ZeroBytes(void* memory, usize count) {
    u8* p = (u8*)memory;
    if (!p) return;
    for (usize i = 0; i < count; ++i) p[i] = 0;
}

static int ReadByte(void* base, usize offset, u8* value) {
    u8* p = (u8*)base;
    if (!p || !value || !IsReadable(p + offset, 1)) return 0;
    *value = p[offset];
    return 1;
}

static float ReadFloatOr(void* base, usize offset, float fallback) {
    u8* p = (u8*)base;
    if (!p || !IsReadable(p + offset, sizeof(float))) return fallback;
    return *(float*)(p + offset);
}

static int WriteByte(void* base, usize offset, u8 value) {
    u8* p = (u8*)base;
    if (!p || !IsReadable(p + offset, 1)) return 0;
    p[offset] = value;
    return 1;
}

static int WriteFloat(void* base, usize offset, float value) {
    u8* p = (u8*)base;
    if (!p || !IsReadable(p + offset, sizeof(float))) return 0;
    *(float*)(p + offset) = value;
    return 1;
}

static int ReadRouteIndex(void* vehicle) {
    int value = -1;
    ReadInt(vehicle, V_ROUTE_INDEX, &value);
    return value;
}

static int IsKnownMarkerIn(const u8* p, usize n) {
    static const char* builtins[] = {
        "DockDistributionOfficeSmall",
        "DockDistributionOfficeMedium",
        "Dock Distribution Office (Small)",
        "Dock Distribution Office (Medium)"
    };

    if (g_marker[0] &&
        (ContainsAscii(p, n, g_marker) || ContainsUtf16Ascii(p, n, g_marker))) return 1;

    for (usize i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
        if (ContainsAscii(p, n, builtins[i]) || ContainsUtf16Ascii(p, n, builtins[i])) return 1;
    }
    return 0;
}

static int DescriptorIsCachedCustom(void* descriptor) {
    for (int i = 0; i < g_customDescriptorCount; ++i) {
        if (g_customDescriptors[i] == descriptor) return 1;
    }
    return 0;
}

static int DescriptorIsCachedNegative(void* descriptor) {
    for (int i = 0; i < g_negativeDescriptorCount; ++i) {
        if (g_negativeDescriptors[i] == descriptor) return 1;
    }
    return 0;
}

static void CacheCustomDescriptor(void* descriptor) {
    if (!descriptor || DescriptorIsCachedCustom(descriptor)) return;
    if (g_customDescriptorCount < MAX_CUSTOM_DESCRIPTORS) {
        g_customDescriptors[g_customDescriptorCount++] = descriptor;
        if (H && H->log) {
            int type = -1;
            ReadInt(descriptor, T_BUILDING_TYPE, &type);
            H->log("DockDistributionOffice  matched Distribution Dock descriptor %p type=%d (%d/2)",
                   descriptor, type, g_customDescriptorCount);
        }
    }
}

static void CacheNegativeDescriptor(void* descriptor) {
    if (!descriptor || DescriptorIsCachedNegative(descriptor)) return;
    if (g_negativeDescriptorCount < MAX_NEGATIVE_DESCRIPTORS) {
        g_negativeDescriptors[g_negativeDescriptorCount++] = descriptor;
    }
}

static int DescriptorHasDistributionDockMarker(void* descriptor) {
    if (!descriptor) return 0;
    if (DescriptorIsCachedCustom(descriptor)) return 1;

    // Fast path for the thousands of non-ship vehicle homes/buildings. Do not
    // put ordinary road/rail/factory descriptors in the negative marker cache;
    // reading their native type is cheaper than a growing linear cache scan.
    int type = -1;
    if (!ReadInt(descriptor, T_BUILDING_TYPE, &type)) return 0;
    if (type != TYPE_SHIP_DOCK && type != TYPE_ROAD_DO) return 0;
    if (DescriptorIsCachedNegative(descriptor)) return 0;

    usize requested = (usize)g_scanBytes;
    if (requested < 512) requested = 512;
    if (requested > 32768) requested = 32768;

    // Try the largest fully readable direct range, then progressively smaller.
    for (usize direct = requested; direct >= 512; direct >>= 1) {
        if (IsReadable(descriptor, direct) && IsKnownMarkerIn((const u8*)descriptor, direct)) {
            CacheCustomDescriptor(descriptor);
            return 1;
        }
        if (direct == 512) break;
    }

    // Workshop object names/paths can also be reached through descriptor pointers.
    usize pointerScan = requested;
    if (pointerScan > 8192) pointerScan = 8192;
    for (usize off = 0; off + sizeof(void*) <= pointerScan; off += sizeof(void*)) {
        u8* at = (u8*)descriptor + off;
        if (!IsReadable(at, sizeof(void*))) break;
        u8* target = *(u8**)at;
        // Native descriptors contain integer/sentinel fields mixed with real
        // pointers. In particular, an ordinary ship dock can expose -1 while
        // the "move to depot" selector is active. Never pass such values to
        // readablePtr or the marker scanner.
        if (!IsCanonicalUserRange(target, 1)) continue;
        usize probe = 0;
        if (IsReadable(target, 1024)) probe = 1024;
        else if (IsReadable(target, 256)) probe = 256;
        else if (IsReadable(target, 64)) probe = 64;
        if (probe && IsKnownMarkerIn(target, probe)) {
            CacheCustomDescriptor(descriptor);
            return 1;
        }
    }

    if (g_probe && H && H->log) {
        H->log("DockDistributionOffice  ship-dock descriptor %p did not match Distribution Dock markers", descriptor);
    }
    CacheNegativeDescriptor(descriptor);
    return 0;
}

static int IsDistributionDock(void* building) {
    void* descriptor = ReadPointer(building, B_TYPEDESC);
    return descriptor && DescriptorHasDistributionDockMarker(descriptor);
}

static int AssignmentCount(void* building) {
    u8* b = (u8*)building;
    if (!b || !IsReadable(b + B_ASSIGN_BEGIN, 16)) return -1;
    u8* begin = *(u8**)(b + B_ASSIGN_BEGIN);
    u8* end = *(u8**)(b + B_ASSIGN_END);
    if (!begin && !end) return 0;
    if (!begin || !end || end < begin) return -1;
    usize bytes = (usize)(end - begin);
    if ((bytes % sizeof(void*)) != 0) return -1;
    usize count = bytes / sizeof(void*);
    if (count > 49) return -1;
    return (int)count;
}

static void LogAssignmentChange(void* building) {
    if (!g_logAssignments || !building) return;
    int count = AssignmentCount(building);
    if (count < 0) return;

    for (int i = 0; i < g_trackedCount; ++i) {
        if (g_tracked[i].building == building) {
            if (g_tracked[i].assignmentCount != count) {
                g_tracked[i].assignmentCount = count;
                g_tracked[i].nextDemandCheckTick = 0;
                g_tracked[i].lastDemandState = -1;
                g_tracked[i].plannerBurstRemaining = 0;
                g_tracked[i].nextControllerTick = 0;
                g_tracked[i].lastAssignmentChangeTick = g_tick;
                g_tracked[i].lastTaskPolicyRefreshTick = 0;
                g_tracked[i].lastPolicyRefreshHeartbeatTick = 0;
                if (H && H->log) H->log("DockDistributionOffice  native assignment count changed: building=%p count=%d", building, count);
            }
            return;
        }
    }

    if (g_trackedCount < MAX_TRACKED_BUILDINGS) {
        g_tracked[g_trackedCount].building = building;
        g_tracked[g_trackedCount].assignmentCount = count;
        g_tracked[g_trackedCount].overseasSeedAttempted = 0;
        g_tracked[g_trackedCount].dispatchRuns = 0;
        g_tracked[g_trackedCount].lastNoJobLogTick = 0;
        g_tracked[g_trackedCount].lastVehicleCount = -1;
        g_tracked[g_trackedCount].lastVehicleBegin = 0;
        g_tracked[g_trackedCount].lastVehicleEnd = 0;
        g_tracked[g_trackedCount].lastOwnedVehicleCount = -1;
        g_tracked[g_trackedCount].lastInPortVehicleCount = -1;
        g_tracked[g_trackedCount].lastAssignedVehicleCount = -1;
        g_tracked[g_trackedCount].lastFleetChangeTick = g_tick;
        g_tracked[g_trackedCount].lastPlannerTick = 0;
        g_tracked[g_trackedCount].lastJobTick = 0;
        g_tracked[g_trackedCount].lastPolicyCompletionTick = 0;
        g_tracked[g_trackedCount].lastAssignedScanTick = 0;
        g_tracked[g_trackedCount].nextDemandCheckTick = 0;
        g_tracked[g_trackedCount].demandScans = 0;
        g_tracked[g_trackedCount].plannerSkips = 0;
        g_tracked[g_trackedCount].discoveredTick = g_tick;
        g_tracked[g_trackedCount].lastAssignmentChangeTick = g_tick;
        g_tracked[g_trackedCount].lastDemandState = -1;
        g_tracked[g_trackedCount].plannerBurstRemaining = 0;
        g_tracked[g_trackedCount].nextControllerTick = 0;
        g_tracked[g_trackedCount].lastControllerLogTick = 0;
        g_tracked[g_trackedCount].lastWorldVehicleScanTick = 0;
        g_tracked[g_trackedCount].lastTaskPolicyRefreshTick = 0;
        g_tracked[g_trackedCount].lastPolicyRefreshHeartbeatTick = 0;
        g_tracked[g_trackedCount].activeJobCount = 0;
        for (int job = 0; job < MAX_ACTIVE_JOBS; ++job) {
            ZeroBytes(&g_tracked[g_trackedCount].activeJobs[job], sizeof(ActiveShipJob));
        }
        ++g_trackedCount;
        if (H && H->log) H->log("DockDistributionOffice  tracking Distribution Dock building=%p assignments=%d", building, count);
    }
}

// --------------------------------------------------------------------- native assignment helpers and ship-harbour classification

static int IsShipCargoHarbour(void* building) {
    void* descriptor = ReadPointer(building, B_TYPEDESC);
    if (!descriptor) return 0;
    int type = -1;
    int subtype = -1;
    if (!ReadInt(descriptor, T_BUILDING_TYPE, &type) ||
        !ReadInt(descriptor, T_BUILDING_SUBTYPE, &subtype)) return 0;
    return type == TYPE_CARGO_STATION && subtype == SUBTYPE_SHIP;
}

static int AssignmentExists(void* home, void* target) {
    u8* b = (u8*)home;
    if (!b || !target || !IsReadable(b + B_ASSIGN_BEGIN, 16)) return 0;
    void** begin = *(void***)(b + B_ASSIGN_BEGIN);
    void** end = *(void***)(b + B_ASSIGN_END);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > 49) count = 49;
    for (usize i = 0; i < count; ++i) {
        void* record = begin[i];
        if (record && IsReadable((u8*)record + 8, sizeof(void*)) &&
            *(void**)((u8*)record + 8) == target) return 1;
    }
    return 0;
}

static int PointerVectorContains(void* owner, usize beginOffset, usize endOffset, void* value) {
    u8* p = (u8*)owner;
    if (!p || !value || !IsReadable(p + beginOffset, 16)) return 0;
    void** begin = *(void***)(p + beginOffset);
    void** end = *(void***)(p + endOffset);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > 4096) return 0;
    for (usize i = 0; i < count; ++i) if (begin[i] == value) return 1;
    return 0;
}

static int PointerVectorCount(void* owner, usize beginOffset, usize endOffset, int maximum) {
    u8* p = (u8*)owner;
    if (!p || !IsReadable(p + beginOffset, 16)) return -1;
    u8* begin = *(u8**)(p + beginOffset);
    u8* end = *(u8**)(p + endOffset);
    if (!begin && !end) return 0;
    if (!begin || !end || end < begin) return -1;
    usize bytes = (usize)(end - begin);
    if ((bytes % sizeof(void*)) != 0) return -1;
    usize count = bytes / sizeof(void*);
    if (count > (usize)maximum) return -1;
    return (int)count;
}

static int AppendUniquePointerVector(void* owner, usize beginOffset, usize endOffset,
                                     void** output, int count, int capacity) {
    if (!owner || !output || capacity <= 0 || count < 0 || count > capacity) return count;
    u8* p = (u8*)owner;
    if (!IsReadable(p + beginOffset, 16)) return count;
    void** begin = *(void***)(p + beginOffset);
    void** end = *(void***)(p + endOffset);
    if (!begin || !end || end < begin) return count;
    usize vectorCount = (usize)(end - begin);
    if (vectorCount > 4096 || (vectorCount && !IsReadable(begin, vectorCount * sizeof(void*)))) return count;
    for (usize i = 0; i < vectorCount && count < capacity; ++i) {
        void* value = begin[i];
        if (!value) continue;
        int duplicate = 0;
        for (int j = 0; j < count; ++j) {
            if (output[j] == value) { duplicate = 1; break; }
        }
        if (!duplicate) output[count++] = value;
    }
    return count;
}

static int SnapshotOwnedVehicles(void* office, void** output, int capacity) {
    if (!office || !output || capacity <= 0) return 0;
    int count = 0;
    count = AppendUniquePointerVector(office, B_VEH_BEGIN, B_VEH_END, output, count, capacity);
    count = AppendUniquePointerVector(office, B_ASSIGNED_BEGIN, B_ASSIGNED_END, output, count, capacity);
    return count;
}

static int ShipOwnershipState(void* office, void* ship) {
    if (!office || !ship) return 0;
    if (PointerVectorContains(office, B_VEH_BEGIN, B_VEH_END, ship)) return 1;
    if (PointerVectorContains(office, B_ASSIGNED_BEGIN, B_ASSIGNED_END, ship)) return 2;
    return 0;
}

static int AddNativeAssignment(void* home, void* target, const char* label) {
    if (!home || !target || !f_Malloc || !f_AssignPush || !f_AssignInit) return 0;
    if (AssignmentExists(home, target)) return 1;
    int countBefore = AssignmentCount(home);
    if (countBefore < 0 || countBefore >= 49) return 0;

    u8* record = (u8*)f_Malloc(0x18);
    u8* config = (u8*)f_Malloc(0x198);
    if (!record || !config) {
        if (H && H->log) H->log("DockDistributionOffice  could not allocate overseas assignment record");
        return 0;
    }
    ZeroBytes(record, 0x18);
    ZeroBytes(config, 0x198);
    f_AssignInit(config);

    record[0] = 1;
    *(void**)(record + 8) = target;
    *(void**)(record + 0x10) = config;

    config[0] = 1;
    config[200] = 1;
    *(float*)(config + 4) = 1.0f;
    *(u64*)(config + 0x10) = *(u64*)(config + 8);
    *(float*)(config + 0xCC) = 1.0f;
    *(u64*)(config + 0xD8) = *(u64*)(config + 0xD0);
    *(u64*)(config + 0x190) = 0;
    *(u64*)(config + 0x20) = 0;
    config[0x28] = 1;
    *(u64*)(config + 0xE8) = 0;
    config[0xF0] = 1;

    void* targetDescriptor = ReadPointer(target, B_TYPEDESC);
    int targetType = -1;
    if (targetDescriptor) ReadInt(targetDescriptor, T_BUILDING_TYPE, &targetType);
    if (targetType == TYPE_EXTERNAL) {
        *(u32*)(config + 4) = 0;
        *(u32*)(config + 0xCC) = 0x3F7D70A4u;
    }
    if (targetType < 0 || targetType >= 9 ||
        ((0x121u >> ((u32)targetType & 31u)) & 1u) == 0u) {
        config[0] = 0;
    }
    else {
        config[200] = 0;
    }

    void* recordValue = record;
    f_AssignPush((u64*)((u8*)home + B_ASSIGN_BEGIN), &recordValue);
    if (AssignmentCount(home) != countBefore + 1) {
        if (H && H->log) H->log("DockDistributionOffice  native assignment push failed for %s", label);
        return 0;
    }

    // The two overseas targets are permanent pseudo-buildings.  Their native
    // ship schedules do not require a building back-reference, and avoiding a
    // write into their private bookkeeping is safer for this first test.
    if (targetType != TYPE_EXTERNAL && f_QwordPush &&
        IsReadable((u8*)target + B_BACKREF_BEGIN, 24) &&
        !PointerVectorContains(target, B_BACKREF_BEGIN, B_BACKREF_END, home)) {
        void* homeValue = home;
        f_QwordPush((u64*)((u8*)target + B_BACKREF_BEGIN), &homeValue);
    }

    if (IsReadable((u8*)home + B_ASSIGN_DIRTY, 1)) *((u8*)home + B_ASSIGN_DIRTY) = 1;
    if (IsReadable((u8*)home + B_ASSIGN_TIMER, sizeof(float))) *(float*)((u8*)home + B_ASSIGN_TIMER) = 3.0f;
    if (H && H->log) H->log("DockDistributionOffice  added %s as native Distribution Dock target", label);
    return 1;
}

static TrackedBuilding* GetTrackedBuilding(void* building) {
    for (int i = 0; i < g_trackedCount; ++i) {
        if (g_tracked[i].building == building) return &g_tracked[i];
    }
    return 0;
}

static void EnsureOverseasTargets(void* game, void* home, int forceSoviet, int forceWestern) {
    if (!g_overseasEnabled || !game || !home) return;
    TrackedBuilding* tracked = GetTrackedBuilding(home);
    if (!tracked) {
        LogAssignmentChange(home);
        tracked = GetTrackedBuilding(home);
    }
    if (!tracked) return;

    if (forceSoviet || forceWestern) {
        void* soviet = ReadPointer(game, G_SOVIET_NODE);
        void* western = ReadPointer(game, G_WESTERN_NODE);
        if (forceSoviet && soviet) {
            AddNativeAssignment(home, soviet, "Soviet overseas connection");
        }
        if (forceWestern && western) {
            AddNativeAssignment(home, western, "Western/NATO overseas connection");
        }
        tracked->overseasSeedAttempted = 1;
        LogAssignmentChange(home);
    }
}


// --------------------------------------------------------------------- native overseas buttons

// Defined in the resolver section below; declared here for the button setup.
static void* ReadIatFunction(void* module, const char* dll, const char* name);

static void* ReadEngineImport(const char* name) {
    static const char* dlls[] = { "C3DDLL64.dll", "c3ddll64.dll" };
    for (usize i = 0; i < sizeof(dlls) / sizeof(dlls[0]); ++i) {
        void* p = ReadIatFunction(H->exeModule, dlls[i], name);
        if (p) return p;
    }
    return 0;
}

static int ResolvePanelImports(void) {
    f_PanelDraw = (FnPanelDraw)ReadEngineImport("?Draw@C3D_PANEL2D@@QEAAXMMMMM_N@Z");
    f_GetMouseSolid = (FnGetMouseSolid)ReadEngineImport("?GetMouseSolid@C3D_INPUT@@QEAA?AVC3DVECTOR3@@XZ");
    f_PanelCollision = (FnPanelCollision)ReadEngineImport("?Collision@C3D_PANEL2D@@QEAA_NVC3DVECTOR3@@MM@Z");
    f_PanelCollisionRect = (FnPanelCollisionRect)ReadEngineImport("?CollisionRect@C3D_PANEL2D@@QEAA_NVC3DVECTOR3@@MMMM@Z");
    f_PrintLeftUnicode = (FnPrintLeftUnicode)ReadEngineImport("?PrintLeftUnicode@C3D_FONTMANAGER@@QEAAXPEAVC3D_FONT@@MMKPEB_WZZ");
    if (!f_PanelDraw || !f_GetMouseSolid) {
        if (H && H->log) H->log("DockDistributionOffice  overseas buttons disabled: C3D draw/mouse imports were not found");
        return 0;
    }
    if (H && H->log) H->log("DockDistributionOffice  native Soviet/Western ship-route button imports resolved");
    return 1;
}

static float GlobalFloat(usize rva, float fallback) {
    if (!EXE || !IsReadable(EXE + rva, sizeof(float))) return fallback;
    return *(float*)(EXE + rva);
}

static void SetPanelRect(float x, float y, float w, float h) {
    if (!EXE || !IsReadable(EXE + G_PANEL_POS_RVA, 16)) return;
    float* pos = (float*)(EXE + G_PANEL_POS_RVA);
    float* size = (float*)(EXE + G_PANEL_SIZE_RVA);
    pos[0] = x; pos[1] = y;
    size[0] = w; size[1] = h;
    *(u32*)(EXE + G_PANEL_PAD_RVA) = 0;
}

static void SetPanelAlpha(float alpha) {
    if (!EXE || !IsReadable(EXE + G_PANEL_COLOR_RVA, 16)) return;
    float* c = (float*)(EXE + G_PANEL_COLOR_RVA);
    c[0] = 1.0f; c[1] = 1.0f; c[2] = 1.0f; c[3] = alpha;
}

static int BindPanelTexture(void* texture, void* technique) {
    if (!texture || !technique || !IsReadable(texture, sizeof(void*))) return 0;
    void** vtable = *(void***)texture;
    if (!vtable || !IsReadable(vtable, 0x78)) return 0;
    typedef void (*FnBind)(void* self, int stage, void* technique);
    FnBind bind = (FnBind)vtable[0x70 / sizeof(void*)];
    if (!bind) return 0;
    bind(texture, 0, technique);
    return 1;
}

static int DrawCompactOverseasButton(void* texture, float x, float y, float icon,
                                      const float* mouse, int assigned) {
    if (!texture || !mouse || !f_PanelDraw) return 0;
    void* technique = ReadPointer(EXE, G_TECHNIQUE_RVA);
    if (!technique || !BindPanelTexture(texture, technique)) return 0;

    // Draw first, then ask the panel object itself for collision.  The previous
    // manual screen-coordinate rectangle did not include the panel engine's
    // internal transform, leaving the invisible hover/click area offset from
    // the rendered icon.  C3D_PANEL2D::Collision uses the exact transformed
    // rectangle that Draw() just placed, so the hitbox and icon now coincide.
    SetPanelRect(x, y, icon, icon);
    SetPanelAlpha(assigned ? 0.32f : 0.72f);
    float full = GlobalFloat(G_PANEL_FULLSIZE_RVA, 1.0f);
    f_PanelDraw(EXE + G_PANEL_RVA, 0.0f, 0.0f, full, full, 0.0f, 1);

    int hovered = 0;
    if (f_PanelCollisionRect) {
        // Use the engine's explicit rectangle collision routine rather than
        // C3D_PANEL2D::Collision().  Collision() applies the panel's full 3-D
        // transform and, for this reused global panel, produced a hitbox far
        // to the right of the rendered icon (over the task-row remove button).
        // CollisionRect() uses the exact screen rectangle while still applying
        // the panel scroll/clip offset, so the visible icon and input rectangle
        // share the same coordinates.
        alignas(16) float collisionMouse[4] = { mouse[0], mouse[1], mouse[2], 0.0f };
        // Runtime testing shows this reused panel reports pointer coordinates
        // approximately 0.7 button-width down and right of the icon render.
        // Keep the icon fixed and translate ONLY its input rectangle up/left.
        const float hitShift = icon * 0.70f;
        const float hitX = x - hitShift;
        const float hitY = y - hitShift;
        hovered = f_PanelCollisionRect(EXE + G_PANEL_RVA, collisionMouse,
                                        hitX, hitX + icon, hitY, hitY + icon);
    }
    else {
        // Fallback mirrors CollisionRect's vertical-scroll correction.
        float scrollY = 0.0f;
        u8* panel = EXE + G_PANEL_RVA;
        if (IsReadable(panel + 0x674c, sizeof(float))) scrollY = *(float*)(panel + 0x674c);
        float adjustedY = mouse[1] - scrollY;
        const float hitShift = icon * 0.70f;
        const float hitX = x - hitShift;
        const float hitY = y - hitShift;
        hovered = mouse[0] >= hitX && mouse[0] <= hitX + icon &&
                  adjustedY >= hitY && adjustedY <= hitY + icon;
    }

    if (hovered && !assigned) {
        SetPanelRect(x, y, icon, icon);
        SetPanelAlpha(1.0f);
        f_PanelDraw(EXE + G_PANEL_RVA, 0.0f, 0.0f, full, full, 0.0f, 1);
    }
    return hovered;
}

static void DrawOverseasButtons(void* game, void* window, void* home, int suppressInput) {
    if (!g_overseasButtons || !game || !window || !home ||
        !f_PanelDraw || !f_GetMouseSolid) return;

    void* soviet = ReadPointer(game, G_SOVIET_NODE);
    void* western = ReadPointer(game, G_WESTERN_NODE);
    void* sovietIcon = ReadPointer(game, GAME_SOVIET_ICON);
    void* westernIcon = ReadPointer(game, GAME_WESTERN_ICON);
    if (!soviet || !western || !sovietIcon || !westernIcon) return;

    alignas(16) u8 mouseBuffer[16];
    ZeroBytes(mouseBuffer, sizeof(mouseBuffer));
    f_GetMouseSolid(EXE + G_MOUSE_OBJECT_RVA, mouseBuffer);
    const float* mouse = (const float*)mouseBuffer;
    if (!IsReadable(mouse, sizeof(float) * 3)) return;

    float dpi = GlobalFloat(G_DPI_RVA, 1.0f);
    float baseX = ReadFloatOr(window, W_POS_X, 0.0f) + ReadFloatOr(window, W_OFF_X, 0.0f);
    float baseY = ReadFloatOr(window, W_POS_Y, 0.0f) + ReadFloatOr(window, W_OFF_Y, 0.0f);
    float icon = dpi * (float)g_buttonSize;
    float x = baseX + dpi * (float)g_buttonXOffset;
    float rowY = baseY + dpi * (float)g_buttonSourceYOffset;
    float westernX = x + icon + dpi * (float)g_buttonGap;

    int sovietAssigned = AssignmentExists(home, soviet);
    int westernAssigned = AssignmentExists(home, western);
    int hoverSoviet = DrawCompactOverseasButton(sovietIcon, x, rowY, icon, mouse, sovietAssigned);
    int hoverWestern = DrawCompactOverseasButton(westernIcon, westernX, rowY, icon, mouse, westernAssigned);

    // Use the physical left-mouse state, not the game's shared one-frame UI
    // click flag.  The shared flag is raised by Load, Unload, Add Stop and
    // other panel controls; consuming it here was what recreated removed
    // border nodes.  A border target is now created only after BOTH edges of
    // one deliberate click occur on the same overseas icon.
    if (!f_GetAsyncKeyState) return;
    int leftDown = (((unsigned short)f_GetAsyncKeyState(0x01) & 0x8000u) != 0u); // VK_LBUTTON
    if (g_buttonHome != home) {
        g_buttonHome = home;
        g_buttonCapture = 0;
        g_lastLeftButtonDown = leftDown;
    }

    // Any native task-list mutation (especially clicking a row's red remove
    // button) owns the current mouse gesture.  Synchronise to the physical
    // state and discard our capture so the release cannot be reinterpreted as
    // an overseas-button click after the native panel has shifted.
    if (suppressInput) {
        g_buttonCapture = 0;
        g_buttonSuppressFrames = 2;
        g_lastLeftButtonDown = leftDown;
        return;
    }
    if (g_buttonSuppressFrames > 0) {
        --g_buttonSuppressFrames;
        g_buttonCapture = 0;
        g_lastLeftButtonDown = leftDown;
        return;
    }

    int pressEdge = leftDown && !g_lastLeftButtonDown;
    int releaseEdge = !leftDown && g_lastLeftButtonDown;

    if (pressEdge) {
        // Exactly one unassigned icon must own the initial mouse-down.
        if (hoverSoviet && !hoverWestern && !sovietAssigned) g_buttonCapture = 1;
        else if (hoverWestern && !hoverSoviet && !westernAssigned) g_buttonCapture = 2;
        else g_buttonCapture = 0;
    }

    // Dragging away cancels the gesture.  A later layout change cannot move a
    // different icon under the cursor and inherit the old press.
    if (leftDown) {
        if (g_buttonCapture == 1 && !hoverSoviet) g_buttonCapture = 0;
        if (g_buttonCapture == 2 && !hoverWestern) g_buttonCapture = 0;
    }

    if (releaseEdge) {
        int captured = g_buttonCapture;
        g_buttonCapture = 0;
        if (captured == 1 && hoverSoviet && !hoverWestern && !sovietAssigned) {
            EnsureOverseasTargets(game, home, 1, 0);
            g_buttonSuppressFrames = 2;
        }
        else if (captured == 2 && hoverWestern && !hoverSoviet && !westernAssigned) {
            EnsureOverseasTargets(game, home, 0, 1);
            g_buttonSuppressFrames = 2;
        }
    }

    g_lastLeftButtonDown = leftDown;
}

// --------------------------------------------------------------------- optional F8 resolver with no DLL imports

static void* ReadIatFunction(void* module, const char* dll, const char* name) {
    if (!H || !H->findIatSlot || !module) return 0;
    void** slot = H->findIatSlot(module, dll, name);
    if (!slot || !IsReadable(slot, sizeof(void*))) return 0;
    return *slot;
}

static void ResolveKernelPatchFunctions(void) {
    f_VirtualProtect = (FnVirtualProtect)ReadIatFunction(H->exeModule, "KERNEL32.dll", "VirtualProtect");
    f_FlushInstructionCache = (FnFlushInstructionCache)ReadIatFunction(H->exeModule, "KERNEL32.dll", "FlushInstructionCache");
    f_GetCurrentProcess = (FnGetCurrentProcess)ReadIatFunction(H->exeModule, "KERNEL32.dll", "GetCurrentProcess");
    if (f_VirtualProtect && f_FlushInstructionCache && f_GetCurrentProcess) return;

    FnGetModuleHandleA getModuleHandleA = (FnGetModuleHandleA)
        ReadIatFunction(H->exeModule, "KERNEL32.dll", "GetModuleHandleA");
    FnGetProcAddress getProcAddress = (FnGetProcAddress)
        ReadIatFunction(H->exeModule, "KERNEL32.dll", "GetProcAddress");
    if (!getModuleHandleA || !getProcAddress) return;
    void* kernel = getModuleHandleA("KERNEL32.dll");
    if (!kernel) return;
    if (!f_VirtualProtect) f_VirtualProtect = (FnVirtualProtect)getProcAddress(kernel, "VirtualProtect");
    if (!f_FlushInstructionCache) f_FlushInstructionCache = (FnFlushInstructionCache)getProcAddress(kernel, "FlushInstructionCache");
    if (!f_GetCurrentProcess) f_GetCurrentProcess = (FnGetCurrentProcess)getProcAddress(kernel, "GetCurrentProcess");
}

static void ResolveHotkeyFunction(void) {
    if (!g_hotkeyEnabled && !g_overseasButtons) return;

    FnGetModuleHandleA getModuleHandleA = (FnGetModuleHandleA)
        ReadIatFunction(H->exeModule, "KERNEL32.dll", "GetModuleHandleA");
    FnGetProcAddress getProcAddress = (FnGetProcAddress)
        ReadIatFunction(H->exeModule, "KERNEL32.dll", "GetProcAddress");

    if (!getModuleHandleA || !getProcAddress) {
        if (H && H->log) H->log("DockDistributionOffice  F8 toggle unavailable: Kernel32 resolver imports not found");
        return;
    }

    void* user32 = getModuleHandleA("USER32.dll");
    if (!user32) user32 = getModuleHandleA("user32.dll");
    if (user32) f_GetAsyncKeyState = (FnGetAsyncKeyState)getProcAddress(user32, "GetAsyncKeyState");

    if (f_GetAsyncKeyState) {
        if (H && H->log) H->log("DockDistributionOffice  panel hotkey active: VK=%d (F8 default)", g_toggleVk);
    }
    else if (H && H->log) {
        H->log("DockDistributionOffice  F8 toggle unavailable: GetAsyncKeyState not resolved; use default_panel in INI");
    }
}

static void UpdatePanelHotkeys(void* game, void* building) {
    if (!f_GetAsyncKeyState) return;
    if (g_hotkeyEnabled) {
        short state = f_GetAsyncKeyState(g_toggleVk);
        int down = (((unsigned short)state & 0x8000u) != 0u);
        if (down && !g_lastKeyDown) {
            g_distributionMode = !g_distributionMode;
            if (H && H->log) {
                H->log("DockDistributionOffice  panel switched to %s",
                       g_distributionMode ? "DISTRIBUTION" : "FLEET");
            }
        }
        g_lastKeyDown = down;
    }
    // Stable fallback deliberately has no keyboard path for overseas targets.
    // EnsureOverseasTargets is reachable only from a completed click captured
    // by DrawOverseasButtons.
    (void)game;
    (void)building;
}

static int BuildValidHighlightThunk(void) {
    if (!H || !H->allocNear) return 0;
    u8* anchor = g_highlightAddress ? g_highlightAddress : EXE + RVA_HIGHLIGHT_BUILDING;
    u8* code = H->allocNear(anchor, 64);
    if (!code || !IsReadable(code, 64)) return 0;

    // Windows x64 quirk: FUN_14040bcc0 consumes its scale from XMM3 and its
    // overlay colour from R9D at the same ordinal argument position.  A normal
    // C++ prototype cannot set both, so this tiny bridge receives scale in
    // XMM2, moves it to XMM3, forces R9D=0 (green), supplies drawConnections=1,
    // and calls the guarded native function.
    const u8 prefix[] = {
        0x48,0x83,0xEC,0x28,             // sub rsp,28h
        0x45,0x33,0xC0,                  // xor r8d,r8d (mode 0)
        0x45,0x33,0xC9,                  // xor r9d,r9d (green)
        0xF3,0x0F,0x10,0xDA,             // movss xmm3,xmm2
        0xC6,0x44,0x24,0x20,0x01,        // fifth arg = 1
        0x48,0xB8                         // mov rax, imm64
    };
    usize at = 0;
    for (usize i = 0; i < sizeof(prefix); ++i) code[at++] = prefix[i];
    *(u64*)(code + at) = (u64)anchor; at += 8;
    const u8 suffix[] = {
        0xFF,0xD0,                        // call rax
        0x48,0x83,0xC4,0x28,             // add rsp,28h
        0xC3                              // ret
    };
    for (usize i = 0; i < sizeof(suffix); ++i) code[at++] = suffix[i];
    f_ValidHighlight = (FnValidHighlight)code;
    return 1;
}

// --------------------------------------------------------------------- ship-cargo selector adapter

static void ClearRoadSelectorMessage(void* game) {
    if (!game) return;
    if (IsReadable((u8*)game + GAME_ASSIGN_MESSAGE, 2)) {
        *((u16*)((u8*)game + GAME_ASSIGN_MESSAGE)) = 0;
    }
    if (IsReadable((u8*)game + GAME_ASSIGN_TIP_TIME, sizeof(float))) {
        *(float*)((u8*)game + GAME_ASSIGN_TIP_TIME) = 0.0f;
    }
}

static void LogSelectorCandidate(void* candidate) {
    if (!g_selectorDiagnostics || !candidate || candidate == g_lastSelectorCandidate) return;
    g_lastSelectorCandidate = candidate;
    void* descriptor = ReadPointer(candidate, B_TYPEDESC);
    int type = -1, subtype = -1;
    if (descriptor) {
        ReadInt(descriptor, T_BUILDING_TYPE, &type);
        ReadInt(descriptor, T_BUILDING_SUBTYPE, &subtype);
    }
    if (H && H->log) {
        H->log("DockDistributionOffice  selector candidate=%p type=%d subtype=%d shipCargo=%d",
               candidate, type, subtype,
               (type == TYPE_CARGO_STATION && subtype == SUBTYPE_SHIP) ? 1 : 0);
    }
}

static void HookDoAddSelector(void* game, u64 arg2, u64 arg3, float scale) {
    if (!g_selectorEnabled || !game) {
        o_DoAddSelector(game, arg2, arg3, scale);
        return;
    }

    void* home = ReadPointer(game, G_ASSIGN_HOME);
    if (!home || !IsDistributionDock(home)) {
        o_DoAddSelector(game, arg2, arg3, scale);
        return;
    }

    int before = AssignmentCount(home);
    int clicked = IsReadable(EXE + G_CLICK_FLAG_RVA, 1) && EXE[G_CLICK_FLAG_RVA];
    void* cachedBefore = ReadPointer(game, G_CACHED_CANDIDATE);
    int suppressStockFailureClick = clicked && cachedBefore && IsShipCargoHarbour(cachedBefore);
    if (suppressStockFailureClick) EXE[G_CLICK_FLAG_RVA] = 0;

    // Stock code supplies collision and the exact hovered building.  When a
    // valid harbour was already cached we hide the click from the road-only
    // branch, avoiding its failure sound/message, then perform the ship-aware
    // assignment below.
    o_DoAddSelector(game, arg2, arg3, scale);
    if (suppressStockFailureClick) EXE[G_CLICK_FLAG_RVA] = 1;

    void* candidate = ReadPointer(game, G_CACHED_CANDIDATE);
    LogSelectorCandidate(candidate);
    if (!candidate || candidate == home || !IsShipCargoHarbour(candidate)) return;

    ClearRoadSelectorMessage(game);
    if (IsReadable((u8*)game + G_CACHED_PATH_OK, 1)) *((u8*)game + G_CACHED_PATH_OK) = 1;
    if (f_ValidHighlight) f_ValidHighlight(game, candidate, scale);

    if (clicked && !AssignmentExists(home, candidate)) {
        if (AddNativeAssignment(home, candidate, "ship cargo harbour")) {
            if (H && H->log) {
                H->log("DockDistributionOffice  ship cargo harbour assigned directly; road/garage path not consulted");
            }
        }
    }

    int after = AssignmentCount(home);
    if (after != before && H && H->log) {
        H->log("DockDistributionOffice  ship cargo harbour assignment count=%d", after);
    }
}


// --------------------------------------------------------------------- native trigger planner + custom ship cargo policy

static int SpoofDescriptorToRoadDo(void* descriptor, int* oldType);
static void RestoreDescriptorType(void* descriptor, int oldType);

struct VehicleSnapshot {
    void* vehicle;
    int routeCount;
    void* target;
};

struct PolicyRouteInfo {
    int routeCount;
    int sourceIndex;
    int destinationIndex;
    int homeIndex;
    void* resource;
    void* sourceTarget;
    void* destinationTarget;
    void* homeTarget;
};

static int SnapshotVehicles(void* office, VehicleSnapshot* snapshots, int capacity) {
    if (!office || !snapshots || capacity <= 0 || !IsReadable((u8*)office + B_VEH_BEGIN, 16)) return 0;
    void** begin = *(void***)((u8*)office + B_VEH_BEGIN);
    void** end = *(void***)((u8*)office + B_VEH_END);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > (usize)capacity) count = (usize)capacity;
    if (count && !IsReadable(begin, count * sizeof(void*))) return 0;
    for (usize i = 0; i < count; ++i) {
        snapshots[i].vehicle = begin[i];
        snapshots[i].routeCount = begin[i] ? PointerVectorCount(begin[i], V_ROUTE_BEGIN, V_ROUTE_END, 1024) : -1;
        snapshots[i].target = begin[i] ? ReadPointer(begin[i], V_CURRENT_TARGET) : 0;
    }
    return (int)count;
}

static int RouteConfigCount(void* vehicle) {
    u8* v = (u8*)vehicle;
    if (!v || !IsReadable(v + V_ROUTE_CONFIG_BEGIN, 16)) return -1;
    u8* begin = *(u8**)(v + V_ROUTE_CONFIG_BEGIN);
    u8* end = *(u8**)(v + V_ROUTE_CONFIG_END);
    if (!begin && !end) return 0;
    if (!begin || !end || end < begin) return -1;
    usize bytes = (usize)(end - begin);
    if ((bytes % 0x198u) != 0) return -1;
    usize count = bytes / 0x198u;
    if (count > 1024u) return -1;
    return (int)count;
}

static u8* RouteConfigAt(void* vehicle, int index) {
    if (!vehicle || index < 0) return 0;
    int count = RouteConfigCount(vehicle);
    if (count <= index) return 0;
    u8* begin = (u8*)ReadPointer(vehicle, V_ROUTE_CONFIG_BEGIN);
    u8* config = begin ? begin + (usize)index * 0x198u : 0;
    return config && IsReadable(config, 0x198u) ? config : 0;
}

static void* FirstResourceInConfig(u8* config, int unload) {
    if (!config || !IsReadable(config, 0x198u)) return 0;
    usize halfOffset = unload ? CFG_UNLOAD_HALF : CFG_LOAD_HALF;
    usize beginOffset = halfOffset + CFG_RES_BEGIN;
    usize endOffset = halfOffset + CFG_RES_END;
    u8* begin = *(u8**)(config + beginOffset);
    u8* end = *(u8**)(config + endOffset);
    if (!begin || !end || end < begin) return 0;
    usize bytes = (usize)(end - begin);
    if (bytes < 0x10u || (bytes % 0x10u) != 0 || bytes > 0x1000u) return 0;
    if (!IsReadable(begin, 0x10u)) return 0;
    return *(void**)begin;
}

static int DestinationReserved(const TrackedBuilding* tracked, void* destination) {
    if (!tracked || !destination) return 0;
    for (int i = 0; i < tracked->activeJobCount; ++i) {
        if (tracked->activeJobs[i].destinationTarget == destination) return 1;
    }
    return 0;
}

static int TaskReserved(const TrackedBuilding* tracked, void* destination, void* resource) {
    if (!tracked || !destination) return 0;
    for (int i = 0; i < tracked->activeJobCount; ++i) {
        const ActiveShipJob* job = &tracked->activeJobs[i];
        if (job->destinationTarget == destination && (!resource || job->resource == resource)) return 1;
    }
    return 0;
}

static int HasUnreservedUnloadTask(TrackedBuilding* tracked, void* office) {
    if (!tracked || !office || !IsReadable((u8*)office + B_ASSIGN_BEGIN, 16)) return 0;
    void** begin = *(void***)((u8*)office + B_ASSIGN_BEGIN);
    void** end = *(void***)((u8*)office + B_ASSIGN_END);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > 49 || (count && !IsReadable(begin, count * sizeof(void*)))) return 0;
    for (usize i = 0; i < count; ++i) {
        u8* record = (u8*)begin[i];
        if (!record || !IsReadable(record, 0x18u)) continue;
        void* target = *(void**)(record + 8);
        u8* config = *(u8**)(record + 0x10);
        if (!target || !config || !IsReadable(config, 0x198u) || config[0xC8u] == 0) continue;
        void* resource = FirstResourceInConfig(config, 1);
        if (!resource) continue;
        if (!TaskReserved(tracked, target, resource) && !DestinationReserved(tracked, target)) return 1;
    }
    return 0;
}

struct SuppressedAssignment {
    u8* config;
    u8 oldUnloadEnabled;
};

static int SuppressReservedDestinations(TrackedBuilding* tracked, void* office,
                                        SuppressedAssignment* suppressed, int capacity) {
    if (!g_taskReservations || !tracked || !office || !suppressed || capacity <= 0 ||
        !IsReadable((u8*)office + B_ASSIGN_BEGIN, 16)) return 0;
    void** begin = *(void***)((u8*)office + B_ASSIGN_BEGIN);
    void** end = *(void***)((u8*)office + B_ASSIGN_END);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > 49 || (count && !IsReadable(begin, count * sizeof(void*)))) return 0;
    int write = 0;
    for (usize i = 0; i < count && write < capacity; ++i) {
        u8* record = (u8*)begin[i];
        if (!record || !IsReadable(record, 0x18u)) continue;
        void* target = *(void**)(record + 8);
        u8* config = *(u8**)(record + 0x10);
        if (!target || !config || !IsReadable(config, 0x198u) || config[0xC8u] == 0) continue;
        if (!DestinationReserved(tracked, target)) continue;
        suppressed[write].config = config;
        suppressed[write].oldUnloadEnabled = config[0xC8u];
        config[0xC8u] = 0;
        ++write;
    }
    return write;
}

static void RestoreSuppressedAssignments(SuppressedAssignment* suppressed, int count) {
    if (!suppressed || count <= 0) return;
    for (int i = 0; i < count; ++i) {
        if (suppressed[i].config && IsReadable(suppressed[i].config + 0xC8u, 1)) {
            suppressed[i].config[0xC8u] = suppressed[i].oldUnloadEnabled;
        }
    }
}

static int InspectPolicyRoute(void* ship, void* office, PolicyRouteInfo* info) {
    if (!ship || !info) return 0;
    ZeroBytes(info, sizeof(PolicyRouteInfo));
    info->sourceIndex = -1;
    info->destinationIndex = -1;
    info->homeIndex = -1;

    int routeCount = PointerVectorCount(ship, V_ROUTE_BEGIN, V_ROUTE_END, 64);
    int configCount = RouteConfigCount(ship);
    if (routeCount < 3 || configCount != routeCount) return 0;

    void** targets = *(void***)((u8*)ship + V_ROUTE_BEGIN);
    if (!targets || !IsReadable(targets, (usize)routeCount * sizeof(void*))) return 0;
    void* home = office ? office : ReadPointer(ship, V_HOME);
    void* loadResource = 0;
    void* unloadResource = 0;

    for (int i = 0; i < routeCount; ++i) {
        u8* config = RouteConfigAt(ship, i);
        if (!config) return 0;
        if (targets[i] == home) info->homeIndex = i;

        if (info->sourceIndex < 0 && config[0] != 0) {
            void* resource = FirstResourceInConfig(config, 0);
            if (resource) {
                info->sourceIndex = i;
                loadResource = resource;
            }
        }
        if (info->destinationIndex < 0 && config[200] != 0) {
            void* resource = FirstResourceInConfig(config, 1);
            if (resource) {
                info->destinationIndex = i;
                unloadResource = resource;
            }
        }
    }

    if (info->homeIndex < 0) info->homeIndex = routeCount - 1;
    if (info->sourceIndex < 0 || info->destinationIndex < 0 ||
        info->sourceIndex == info->destinationIndex) return 0;
    if (loadResource && unloadResource && loadResource != unloadResource) return 0;

    info->routeCount = routeCount;
    info->resource = loadResource ? loadResource : unloadResource;
    info->sourceTarget = targets[info->sourceIndex];
    info->destinationTarget = targets[info->destinationIndex];
    info->homeTarget = targets[info->homeIndex];
    return info->resource && info->sourceTarget && info->destinationTarget && info->homeTarget;
}

static float CargoCapacity(void* ship, void* resource) {
    if (!ship || !resource || !f_CargoCapacity) return 0.0f;
    float value = f_CargoCapacity(ship, resource, 0, 0);
    if (!(value > 0.0f) || value > 1000000000.0f) return 0.0f;
    return value;
}

static float CargoAmount(void* ship, void* resource) {
    if (!ship || !resource || !f_CargoAmount) return 0.0f;
    float value = f_CargoAmount(ship, resource, 1);
    if (!(value >= 0.0f) || value > 1000000000.0f) return 0.0f;
    return value;
}


// v0.6.5 deliberately does not inspect building cargo amounts directly.
// The native cargo helpers below are used only with actual ships.  Idle demand
// detection is performed by bounded bursts of the proven native planner path.
// This preserves v0.6.2 route generation and custom-policy interception while
// avoiding continuous planner pumping when no order is due.

// --------------------------------------------------------------------- manager-owned task controller (v0.7.3)

static int ActiveJobExists(TrackedBuilding* tracked, void* ship);
static int RegisterCustomJob(TrackedBuilding* tracked, void* office, void* ship);
static int ApplyCustomPolicy(void* office, void* ship, const PolicyRouteInfo* route, ActiveShipJob* job);


struct AssignmentTask {
    void* target;
    u8* config;
    void* resource;
    float threshold;
    int externalTarget;
    int wildcardResources;
};

static int IsSaneObjectPointer(void* value) {
    return IsCanonicalUserRange(value, 16) && IsReadable(value, 16);
}

static int TargetBuildingType(void* target) {
    void* descriptor = ReadPointer(target, B_TYPEDESC);
    int type = -1;
    if (descriptor) ReadInt(descriptor, T_BUILDING_TYPE, &type);
    return type;
}

static int IsExternalAssignmentTarget(void* target) {
    if (!target || !g_activeGame) return 0;
    // Authoritative detection only.  FUN_140741050/FUN_1407ec1d0 receive the
    // exact assignment target pointer, and the schedule editor stores the two
    // overseas pseudo-buildings at these game-object fields.  Do not infer an
    // overseas target from a building type: assignment targets restored from a
    // save can expose transient/adapter descriptors during world initialisation.
    void* soviet = ReadPointer(g_activeGame, G_SOVIET_NODE);
    void* western = ReadPointer(g_activeGame, G_WESTERN_NODE);
    return target == soviet || target == western;
}

struct ConfigHalfView {
    u8* half;
    int enabled;
    float threshold;
    u8* begin;
    u8* end;
    int resourceCount;
};

static int ReadConfigHalf(u8* config, usize halfOffset, ConfigHalfView* view) {
    if (!view) return 0;
    ZeroBytes(view, sizeof(ConfigHalfView));
    if (!config || halfOffset + CFG_HALF_SIZE > 0x198u ||
        !IsReadable(config + halfOffset, CFG_HALF_SIZE)) return 0;
    u8* half = config + halfOffset;
    u8* begin = *(u8**)(half + CFG_RES_BEGIN);
    u8* end = *(u8**)(half + CFG_RES_END);
    int resourceCount = 0;
    if (begin || end) {
        if (!begin || !end || end < begin) return 0;
        usize bytes = (usize)(end - begin);
        if ((bytes % 0x10u) != 0 || bytes > 0x4000u ||
            (bytes && !IsReadable(begin, bytes))) return 0;
        resourceCount = (int)(bytes / 0x10u);
    }
    float threshold = *(float*)(half + CFG_THRESHOLD);
    if (!(threshold >= 0.0f) || threshold > 1.0f) threshold = halfOffset ? 1.0f : 0.0f;
    view->half = half;
    view->enabled = half[CFG_ENABLED] != 0;
    view->threshold = threshold;
    view->begin = begin;
    view->end = end;
    view->resourceCount = resourceCount;
    return 1;
}

static void DecodeCargoPair(u64 packed, float* amount, float* capacity) {
    union PairBits { u64 q; float f[2]; } value;
    value.q = packed;
    if (amount) *amount = value.f[0];
    if (capacity) *capacity = value.f[1];
}

// Verified building-storage path from the decompiled game:
//   FUN_1401e7c20 calls FUN_1401e82e0 to obtain the cargo-station's physical
//   current/capacity pair, and conditionally adds FUN_1401e9270 for linked
//   factory-storage capacity. FUN_1406bdca0/FUN_1406be030 are VEHICLE cargo
//   helpers and must never receive a building pointer. Earlier manager builds
//   did exactly that, which explains the +0x6BDCE5/+0x6BDEA0 faults.
static int SafeCargoStats(void* target, void* resource, float* amount, float* capacity) {
    if (amount) *amount = 0.0f;
    if (capacity) *capacity = 0.0f;
    if (!target || !resource || !amount || !capacity || !g_activeGame ||
        !f_BuildingStatsBase || !f_BuildingHasExtra || !f_BuildingStatsExtra) return 0;
    if (!IsSaneObjectPointer(target) || !IsSaneObjectPointer(resource)) return 0;
    if (TargetBuildingType(target) != TYPE_CARGO_STATION) return 0;

    u64 basePair[2] = {0, 0};
    float physicalAmount = 0.0f;
    float physicalCapacity = 0.0f;
    __try {
        u64* baseResult = f_BuildingStatsBase((longlong)g_activeGame, basePair,
                                              (u64)target, (u64)resource, 0, 0);
        if (!baseResult || !IsReadable(baseResult, 16)) return 0;
        DecodeCargoPair(baseResult[1], &physicalAmount, &physicalCapacity);

        if (f_BuildingHasExtra((longlong)g_activeGame, (longlong)target,
                               (longlong)resource, 0) != 0) {
            // Native FUN_1401e7c20 preserves the base pair, asks FUN_1401e9270
            // for the additional connected-storage pair, then adds the two.
            u64 extraPair[2] = {baseResult[0], baseResult[1]};
            longlong* extraResult = f_BuildingStatsExtra((longlong)g_activeGame,
                                                          (longlong*)extraPair,
                                                          (longlong)target,
                                                          (longlong)resource, 0, 0);
            if (extraResult && IsReadable(extraResult, 16)) {
                float extraAmount = 0.0f;
                float extraCapacity = 0.0f;
                DecodeCargoPair((u64)extraResult[1], &extraAmount, &extraCapacity);
                if (extraCapacity > 0.0f && extraCapacity < 1000000000.0f &&
                    extraAmount >= 0.0f && extraAmount < 1000000000.0f) {
                    physicalAmount += extraAmount;
                    physicalCapacity += extraCapacity;
                }
            }
        }
    } __except (1) {
        ++g_nativeCargoProbeFaults;
        if (H && H->log && (g_nativeCargoProbeFaults <= 8 || (g_nativeCargoProbeFaults % 100) == 0)) {
            H->log("DockDistributionOffice  guarded building-storage stats fault: target=%p resource=%p faults=%d; task deferred",
                   target, resource, g_nativeCargoProbeFaults);
        }
        return 0;
    }

    if (!(physicalCapacity > 0.01f) || physicalCapacity > 1000000000.0f) return 0;
    if (!(physicalAmount >= 0.0f) || physicalAmount > 1000000000.0f) return 0;
    if (physicalAmount > physicalCapacity) physicalAmount = physicalCapacity;
    *amount = physicalAmount;
    *capacity = physicalCapacity;
    return 1;
}

static int TaskAlreadyPresent(const AssignmentTask* tasks, int count,
                              void* target, void* resource) {
    if (!tasks || count <= 0) return 0;
    for (int i = 0; i < count; ++i) {
        if (tasks[i].target == target && tasks[i].resource == resource) return 1;
    }
    return 0;
}

static int AppendHalfResources(AssignmentTask* tasks, int count, int capacity,
                               void* target, u8* config, const ConfigHalfView* view,
                               float threshold, int externalTarget) {
    if (!tasks || count < 0 || count >= capacity || !target || !config || !view ||
        view->resourceCount <= 0 || !view->begin || !view->end) return count;
    for (int i = 0; i < view->resourceCount && count < capacity; ++i) {
        u8* entry = view->begin + (usize)i * 0x10u;
        void* resource = *(void**)entry;
        if (!IsSaneObjectPointer(resource) || TaskAlreadyPresent(tasks, count, target, resource)) continue;
        tasks[count].target = target;
        tasks[count].config = config;
        tasks[count].resource = resource;
        tasks[count].threshold = threshold;
        tasks[count].externalTarget = externalTarget;
        tasks[count].wildcardResources = 0;
        ++count;
    }
    return count;
}

static int AppendConfigResources(AssignmentTask* tasks, int count, int capacity,
                                 void* target, u8* config, int unload,
                                 float threshold, int externalTarget) {
    ConfigHalfView view;
    if (!ReadConfigHalf(config, unload ? CFG_UNLOAD_HALF : CFG_LOAD_HALF, &view)) return count;
    return AppendHalfResources(tasks, count, capacity, target, config, &view,
                               threshold, externalTarget);
}

static int FilterConfigToResource(u8* config, int unload, void* resource) {
    if (!config || !resource || !IsReadable(config, 0x198u)) return 0;
    usize halfOffset = unload ? CFG_UNLOAD_HALF : CFG_LOAD_HALF;
    usize beginOffset = halfOffset + CFG_RES_BEGIN;
    usize endOffset = halfOffset + CFG_RES_END;
    u8* begin = *(u8**)(config + beginOffset);
    u8* end = *(u8**)(config + endOffset);
    if (!begin || !end || end < begin) return 0;
    usize bytes = (usize)(end - begin);
    if ((bytes % 0x10u) != 0 || bytes < 0x10u || bytes > 0x4000u || !IsReadable(begin, bytes)) return 0;
    usize entries = bytes / 0x10u;
    for (usize i = 0; i < entries; ++i) {
        u8* entry = begin + i * 0x10u;
        if (*(void**)entry != resource) continue;
        if (entry != begin) {
            for (usize j = 0; j < 0x10u; ++j) begin[j] = entry[j];
        }
        *(u8**)(config + endOffset) = begin + 0x10u;
        return 1;
    }
    return 0;
}

struct WildcardDestination {
    void* target;
    u8* config;
    float threshold;
};

static int ReadAssignmentTasks(void* office, AssignmentTask* sources, int sourceCapacity,
                               AssignmentTask* destinations, int destinationCapacity,
                               int* sourceCountOut, int* destinationCountOut) {
    if (sourceCountOut) *sourceCountOut = 0;
    if (destinationCountOut) *destinationCountOut = 0;
    if (!office || !sources || !destinations || sourceCapacity <= 0 || destinationCapacity <= 0 ||
        !IsReadable((u8*)office + B_ASSIGN_BEGIN, 16)) return 0;
    void** begin = *(void***)((u8*)office + B_ASSIGN_BEGIN);
    void** end = *(void***)((u8*)office + B_ASSIGN_END);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > 49 || (count && !IsReadable(begin, count * sizeof(void*)))) return 0;

    WildcardDestination wildcards[49];
    ZeroBytes(wildcards, sizeof(wildcards));
    int wildcardCount = 0;
    int sourceCount = 0;
    int destinationCount = 0;

    // First pass: decode explicit source/destination resource vectors.  Native
    // Distribution Office saves are also allowed to represent an enabled unload
    // row with an empty per-destination vector.  In that case the row means
    // "accept matching configured source resources" and is expanded below.
    for (usize i = 0; i < count; ++i) {
        u8* record = (u8*)begin[i];
        if (!record || !IsReadable(record, 0x18u)) continue;
        void* target = *(void**)(record + 8);
        u8* config = *(u8**)(record + 0x10);
        if (!target || !config || !IsReadable(config, 0x198u)) continue;

        ConfigHalfView loadHalf;
        ConfigHalfView unloadHalf;
        if (!ReadConfigHalf(config, CFG_LOAD_HALF, &loadHalf) ||
            !ReadConfigHalf(config, CFG_UNLOAD_HALF, &unloadHalf)) continue;

        int external = IsExternalAssignmentTarget(target);
        int useLoad = loadHalf.enabled;
        int useUnload = unloadHalf.enabled;

        if (g_savedTaskHalfFallback) {
            if (!useLoad && loadHalf.resourceCount > 0 && unloadHalf.resourceCount == 0)
                useLoad = 1;
            if (!useUnload && unloadHalf.resourceCount > 0 && loadHalf.resourceCount == 0)
                useUnload = 1;
        }

        if (useLoad && loadHalf.resourceCount > 0) {
            sourceCount = AppendHalfResources(sources, sourceCount, sourceCapacity,
                                              target, config, &loadHalf,
                                              loadHalf.threshold, external);
        }
        if (useUnload && !external) {
            if (unloadHalf.resourceCount > 0) {
                destinationCount = AppendHalfResources(destinations, destinationCount,
                                                       destinationCapacity, target, config,
                                                       &unloadHalf, unloadHalf.threshold, 0);
            } else if (wildcardCount < 49) {
                wildcards[wildcardCount].target = target;
                wildcards[wildcardCount].config = config;
                wildcards[wildcardCount].threshold = unloadHalf.threshold;
                ++wildcardCount;
            }
        }
    }

    // Second pass: expand empty unload vectors from the authoritative set of
    // resources configured on source rows.  Compatibility is resolved later by
    // the guarded native cargo-capacity probe, so steel/aluminium/bricks can
    // share one overseas source without inventing destination resource state.
    for (int w = 0; w < wildcardCount && destinationCount < destinationCapacity; ++w) {
        for (int sidx = 0; sidx < sourceCount && destinationCount < destinationCapacity; ++sidx) {
            void* resource = sources[sidx].resource;
            if (!resource || TaskAlreadyPresent(destinations, destinationCount,
                                                wildcards[w].target, resource)) continue;
            destinations[destinationCount].target = wildcards[w].target;
            destinations[destinationCount].config = wildcards[w].config;
            destinations[destinationCount].resource = resource;
            destinations[destinationCount].threshold = wildcards[w].threshold;
            destinations[destinationCount].externalTarget = 0;
            destinations[destinationCount].wildcardResources = 1;
            ++destinationCount;
        }
    }

    if (sourceCountOut) *sourceCountOut = sourceCount;
    if (destinationCountOut) *destinationCountOut = destinationCount;
    return sourceCount > 0 && destinationCount > 0;
}


// Recover the source-side reserve policy for a reconstructed native route.
// The Distribution Office load percentage is a source-storage reserve: a
// domestic source may contribute only the cargo physically above this level.
// Overseas pseudo-buildings are purchasable sources and therefore ignore the
// reserve calculation even though their UI row normally displays 0%.
static int BindTaskPolicyFromAssignments(void* office, ActiveShipJob* job) {
    if (!office || !job || !job->sourceTarget || !job->destinationTarget ||
        !job->resource) return 0;

    static AssignmentTask sources[MAX_MANAGER_TASKS];
    static AssignmentTask destinations[MAX_MANAGER_TASKS];
    ZeroBytes(sources, sizeof(sources));
    ZeroBytes(destinations, sizeof(destinations));
    int sourceCount = 0;
    int destinationCount = 0;
    if (!ReadAssignmentTasks(office, sources, MAX_MANAGER_TASKS,
                             destinations, MAX_MANAGER_TASKS,
                             &sourceCount, &destinationCount)) return 0;

    int sourceFound = 0;
    int destinationFound = 0;
    job->sourceExternal = IsExternalAssignmentTarget(job->sourceTarget);
    job->sourceReserveFraction = 0.0f;
    job->destinationTriggerFraction = 0.0f;

    for (int i = 0; i < sourceCount; ++i) {
        if (sources[i].target != job->sourceTarget ||
            sources[i].resource != job->resource) continue;
        job->sourceExternal = sources[i].externalTarget;
        job->sourceReserveFraction = sources[i].threshold;
        sourceFound = 1;
        break;
    }
    for (int i = 0; i < destinationCount; ++i) {
        if (destinations[i].target != job->destinationTarget ||
            destinations[i].resource != job->resource) continue;
        job->destinationTriggerFraction = destinations[i].threshold;
        destinationFound = 1;
        break;
    }
    return sourceFound && destinationFound;
}

// Refresh the policy cached by every active voyage from one stable snapshot of
// the office task table. Assignment target/resource identity stays bound to the
// voyage; only the player-editable source reserve, destination trigger, and
// external-source flag are refreshed. This is deliberately deferred while the
// native panel is editing the same office.
static int RefreshActiveTaskPolicies(TrackedBuilding* tracked, void* office,
                                     int afterUiEdit) {
    if (!tracked || !office || tracked->activeJobCount <= 0) return 0;

    static AssignmentTask sources[MAX_MANAGER_TASKS];
    static AssignmentTask destinations[MAX_MANAGER_TASKS];
    ZeroBytes(sources, sizeof(sources));
    ZeroBytes(destinations, sizeof(destinations));
    int sourceCount = 0;
    int destinationCount = 0;
    if (!ReadAssignmentTasks(office, sources, MAX_MANAGER_TASKS,
                             destinations, MAX_MANAGER_TASKS,
                             &sourceCount, &destinationCount)) return 0;

    for (int j = 0; j < tracked->activeJobCount; ++j) {
        ActiveShipJob* job = &tracked->activeJobs[j];
        if (!job->ship || !job->sourceTarget || !job->destinationTarget ||
            !job->resource) continue;

        int sourceFound = 0;
        int destinationFound = 0;
        int newExternal = job->sourceExternal;
        float newReserve = job->sourceReserveFraction;
        float newTrigger = job->destinationTriggerFraction;

        for (int i = 0; i < sourceCount; ++i) {
            if (sources[i].target != job->sourceTarget ||
                sources[i].resource != job->resource) continue;
            newExternal = sources[i].externalTarget;
            newReserve = sources[i].threshold;
            sourceFound = 1;
            break;
        }
        for (int i = 0; i < destinationCount; ++i) {
            if (destinations[i].target != job->destinationTarget ||
                destinations[i].resource != job->resource) continue;
            newTrigger = destinations[i].threshold;
            destinationFound = 1;
            break;
        }
        if (!sourceFound || !destinationFound) continue;

        float reserveDelta = newReserve - job->sourceReserveFraction;
        if (reserveDelta < 0.0f) reserveDelta = -reserveDelta;
        float triggerDelta = newTrigger - job->destinationTriggerFraction;
        if (triggerDelta < 0.0f) triggerDelta = -triggerDelta;
        int changed = reserveDelta > 0.0001f || triggerDelta > 0.0001f ||
                      newExternal != job->sourceExternal;

        float oldReserve = job->sourceReserveFraction;
        float oldTrigger = job->destinationTriggerFraction;
        job->sourceExternal = newExternal;
        job->sourceReserveFraction = newReserve;
        job->destinationTriggerFraction = newTrigger;

        if (changed && g_customPolicyDiagnostics && H && H->log) {
            H->log("DockDistributionOffice  manager live task policy refreshed: office=%p ship=%p source_reserve=%.1f%%->%.1f%% destination_trigger=%.1f%%->%.1f%% reason=%s",
                   office, job->ship, oldReserve * 100.0f, newReserve * 100.0f,
                   oldTrigger * 100.0f, newTrigger * 100.0f,
                   afterUiEdit ? "panel-close" : "periodic");
        }
    }
    return 1;
}

// Convert a standing source reserve into the current safe transfer ceiling.
// This is deliberately not the voyage departure target. onboard +
// (source amount - reserve amount) remains stable while cargo is transferred,
// so the ceiling can pause loading at the reserve and rise again when the
// source is replenished.
static int ComputeEffectiveLoadTarget(ActiveShipJob* job, float onBoard,
                                      float* targetAmount, float* targetFraction) {
    if (!job || !targetAmount || !targetFraction || job->cargoCapacity <= 0.01f) return 0;
    float amount = job->targetLoadAmount;
    if (amount < onBoard) amount = onBoard;
    if (!job->sourceExternal) {
        float sourceAmount = 0.0f, sourceCapacity = 0.0f;
        if (!SafeCargoStats(job->sourceTarget, job->resource, &sourceAmount, &sourceCapacity)) return 0;
        float reserve = sourceCapacity * job->sourceReserveFraction;
        float surplus = sourceAmount - reserve;
        if (surplus < 0.0f) surplus = 0.0f;
        float sourceLimited = onBoard + surplus;
        if (sourceLimited < amount) amount = sourceLimited;
        if (amount < onBoard) amount = onBoard;
    }
    if (amount > job->cargoCapacity) amount = job->cargoCapacity;
    float fraction = amount / job->cargoCapacity;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    *targetAmount = amount;
    *targetFraction = fraction;
    return 1;
}

static void LogAssignmentHalfDiagnostics(void* office) {
    if (!g_taskParseDiagnostics || !H || !H->log || !office ||
        !IsReadable((u8*)office + B_ASSIGN_BEGIN, 16)) return;
    void** begin = *(void***)((u8*)office + B_ASSIGN_BEGIN);
    void** end = *(void***)((u8*)office + B_ASSIGN_END);
    if (!begin || !end || end < begin) return;
    usize count = (usize)(end - begin);
    if (count > 49 || (count && !IsReadable(begin, count * sizeof(void*)))) return;
    for (usize i = 0; i < count; ++i) {
        u8* record = (u8*)begin[i];
        if (!record || !IsReadable(record, 0x18u)) continue;
        void* target = *(void**)(record + 8);
        u8* config = *(u8**)(record + 0x10);
        ConfigHalfView loadHalf, unloadHalf;
        int loadOk = config && ReadConfigHalf(config, CFG_LOAD_HALF, &loadHalf);
        int unloadOk = config && ReadConfigHalf(config, CFG_UNLOAD_HALF, &unloadHalf);
        int type = target ? TargetBuildingType(target) : -1;
        int external = target ? IsExternalAssignmentTarget(target) : 0;
        H->log("DockDistributionOffice  task half[%d]: target=%p type=%d external=%d config=%p load(ok=%d on=%d resources=%d threshold=%.1f%%) unload(ok=%d on=%d resources=%d threshold=%.1f%%)",
               (int)i, target, type, external, config,
               loadOk, loadOk ? loadHalf.enabled : 0, loadOk ? loadHalf.resourceCount : -1,
               loadOk ? loadHalf.threshold * 100.0f : -1.0f,
               unloadOk, unloadOk ? unloadHalf.enabled : 0, unloadOk ? unloadHalf.resourceCount : -1,
               unloadOk ? unloadHalf.threshold * 100.0f : -1.0f);
    }
}

static int SourceCanSupply(const AssignmentTask* source) {
    if (!source || !source->target || !source->resource) return 0;
    if (source->externalTarget) return 1;
    float amount = 0.0f, capacity = 0.0f;
    if (!SafeCargoStats(source->target, source->resource, &amount, &capacity)) return 0;
    float fraction = amount / capacity;
    return fraction > source->threshold + g_triggerEpsilon;
}

static void* FindAvailableShip(void* office, void* resource) {
    if (!office || !resource || !IsReadable((u8*)office + B_VEH_BEGIN, 16)) return 0;
    void** begin = *(void***)((u8*)office + B_VEH_BEGIN);
    void** end = *(void***)((u8*)office + B_VEH_END);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > 256 || (count && !IsReadable(begin, count * sizeof(void*)))) return 0;
    TrackedBuilding* tracked = GetTrackedBuilding(office);
    for (usize i = 0; i < count; ++i) {
        void* ship = begin[i];
        if (!ship || !IsReadable((u8*)ship + V_ROUTE_INDEX, sizeof(int))) continue;
        if (tracked && ActiveJobExists(tracked, ship)) continue;
        int routeCount = PointerVectorCount(ship, V_ROUTE_BEGIN, V_ROUTE_END, 64);
        if (routeCount > 1) continue;
        if (CargoCapacity(ship, resource) <= 0.01f) continue;
        return ship;
    }
    return 0;
}

static int ClearManagedRoute(void* ship) {
    if (!ship || !f_RouteTargetResize || !f_RouteConfigResize) return 0;
    f_RouteTargetResize((longlong*)((u8*)ship + V_ROUTE_BEGIN), 0);
    f_RouteConfigResize((longlong*)((u8*)ship + V_ROUTE_CONFIG_BEGIN), 0);
    if (IsReadable((u8*)ship + V_ROUTE_INDEX, sizeof(int))) *(int*)((u8*)ship + V_ROUTE_INDEX) = -1;
    if (IsReadable((u8*)ship + V_CURRENT_TARGET, sizeof(void*))) *(void**)((u8*)ship + V_CURRENT_TARGET) = 0;
    if (IsReadable((u8*)ship + V_ROUTE_DIRTY, sizeof(u16))) *(u16*)((u8*)ship + V_ROUTE_DIRTY) = 0x0101u;
    if (f_RouteRefresh) f_RouteRefresh((void**)ship, 0, 0, 0);
    return 1;
}

static int BuildManagedRoute(void* game, void* office, void* ship,
                             const AssignmentTask* source, const AssignmentTask* destination) {
    if (!game || !office || !ship || !source || !destination ||
        source->resource != destination->resource ||
        !f_RouteTargetResize || !f_RouteConfigResize || !f_RouteConfigInit || !f_ConfigHalfCopy) return 0;

    // Start from a clean temporary schedule. The native vector helpers run
        // the correct destructors for the nested resource lists.
        f_RouteTargetResize((longlong*)((u8*)ship + V_ROUTE_BEGIN), 0);
        f_RouteConfigResize((longlong*)((u8*)ship + V_ROUTE_CONFIG_BEGIN), 0);
        f_RouteTargetResize((longlong*)((u8*)ship + V_ROUTE_BEGIN), 3);
        f_RouteConfigResize((longlong*)((u8*)ship + V_ROUTE_CONFIG_BEGIN), 3);

        void** targets = *(void***)((u8*)ship + V_ROUTE_BEGIN);
        u8* configs = *(u8**)((u8*)ship + V_ROUTE_CONFIG_BEGIN);
        if (!targets || !configs || !IsReadable(targets, 3 * sizeof(void*)) || !IsReadable(configs, 3 * 0x198u)) return 0;

        for (int i = 0; i < 3; ++i) f_RouteConfigInit((longlong)ship, configs + (usize)i * 0x198u);
        u8* sourceConfig = configs;
        u8* destinationConfig = configs + 0x198u;
        u8* homeConfig = configs + 0x330u;

        // Copy only the relevant half of each native assignment record. This
        // preserves resource lists and special border/customs metadata while
        // keeping the opposite operation disabled.
        f_ConfigHalfCopy(sourceConfig, source->config);
        // A native unload row with an empty resource vector means that the
        // destination accepts matching resources configured on source rows.
        // For the temporary ship schedule, seed the unload half from the
        // selected source half so the deep-copied native vector contains the
        // exact one resource chosen by the manager.  Explicit destination
        // vectors continue to copy from their own unload half.
        if (destination->wildcardResources)
            f_ConfigHalfCopy(destinationConfig + 200, source->config);
        else
            f_ConfigHalfCopy(destinationConfig + 200, destination->config + 200);
        // One assignment can expose many resources (especially an overseas
        // node).  A managed voyage must carry only the resource selected for
        // this destination, otherwise the ship can mix unrelated cargo and the
        // policy tracker will bind to whichever entry happens to be first.
        if (!FilterConfigToResource(sourceConfig, 0, source->resource) ||
            !FilterConfigToResource(destinationConfig, 1, destination->resource)) {
            ClearManagedRoute(ship);
            return 0;
        }
        *(u64*)(sourceConfig + 400) = *(u64*)(source->config + 400);
        *(u64*)(destinationConfig + 400) = *(u64*)(destination->config + 400);
        *(u64*)(homeConfig + 400) = 0;

        sourceConfig[0] = 1;
        *(float*)(sourceConfig + 4) = (float)g_minDepartureLoadPermille / 1000.0f;
        sourceConfig[0x28u] = 0;
        sourceConfig[0xC8u] = 0;
        destinationConfig[0] = 0;
        destinationConfig[0xC8u] = 1;
        *(float*)(destinationConfig + 0xCCu) = 1.0f;
        destinationConfig[0xF0u] = 0;
        homeConfig[0] = 0;
        homeConfig[0xC8u] = 0;

        targets[0] = source->target;
        targets[1] = destination->target;
        targets[2] = office;

        if (IsReadable((u8*)ship + V_ROUTE_INDEX, sizeof(int))) *(int*)((u8*)ship + V_ROUTE_INDEX) = 2;
        if (IsReadable((u8*)ship + V_CURRENT_TARGET, sizeof(void*))) *(void**)((u8*)ship + V_CURRENT_TARGET) = office;
        if (IsReadable((u8*)ship + V_ROUTE_DIRTY, sizeof(u16))) *(u16*)((u8*)ship + V_ROUTE_DIRTY) = 0x0101u;

    if (f_RouteRefresh) f_RouteRefresh((void**)ship, 0, 0, 0);
    if (f_VehicleRouteStart) f_VehicleRouteStart((longlong)game, (longlong)ship, 1);
    return PointerVectorCount(ship, V_ROUTE_BEGIN, V_ROUTE_END, 64) == 3;
}

static int GlobalVehicleContains(void* game, void* vehicle) {
    if (!game || !vehicle || !IsReadable((u8*)game + G_VEHICLES, 16)) return 0;
    void** begin = *(void***)((u8*)game + G_VEHICLES);
    void** end = *(void***)((u8*)game + G_VEHICLES_END);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > 200000 || (count && !IsReadable(begin, count * sizeof(void*)))) return 0;
    for (usize i = 0; i < count; ++i) if (begin[i] == vehicle) return 1;
    return 0;
}

static int ReconstructWorldVoyages(TrackedBuilding* tracked, void* game, void* office) {
    if (!g_reconstructWorldVehicles || !tracked || !game || !office ||
        tracked->activeJobCount >= MAX_ACTIVE_JOBS) return 0;
    if ((u32)(g_tick - tracked->lastWorldVehicleScanTick) < (u32)g_worldScanTicks) return 0;
    tracked->lastWorldVehicleScanTick = g_tick;
    if (!IsReadable((u8*)game + G_VEHICLES, 16)) return 0;
    void** begin = *(void***)((u8*)game + G_VEHICLES);
    void** end = *(void***)((u8*)game + G_VEHICLES_END);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > 200000 || (count && !IsReadable(begin, count * sizeof(void*)))) return 0;
    int rebuilt = 0;
    for (usize i = 0; i < count && tracked->activeJobCount < MAX_ACTIVE_JOBS; ++i) {
        void* ship = begin[i];
        if (!ship || ActiveJobExists(tracked, ship)) continue;
        PolicyRouteInfo route;
        if (!InspectPolicyRoute(ship, office, &route)) continue;
        if (route.homeTarget != office) continue;
        if (!AssignmentExists(office, route.sourceTarget) || !AssignmentExists(office, route.destinationTarget)) continue;
        ActiveShipJob* slot = &tracked->activeJobs[tracked->activeJobCount];
        if (!ApplyCustomPolicy(office, ship, &route, slot)) continue;
        BindTaskPolicyFromAssignments(office, slot);
        slot->lastOwnershipState = PointerVectorContains(office, B_VEH_BEGIN, B_VEH_END, ship) ? 1 : 2;
        ++tracked->activeJobCount;
        ++rebuilt;
        if (H && H->log) H->log("DockDistributionOffice  manager reconstructed saved voyage: office=%p ship=%p destination=%p resource=%p",
                                office, ship, route.destinationTarget, route.resource);
    }
    return rebuilt;
}

static int DispatchManagedJob(void* game, TrackedBuilding* tracked, void* office,
                              const AssignmentTask* source, const AssignmentTask* destination,
                              void* ship) {
    if (!game || !tracked || !office || !source || !destination || !ship) return 0;
    if (!BuildManagedRoute(game, office, ship, source, destination)) return 0;
    if (!RegisterCustomJob(tracked, office, ship)) {
        ClearManagedRoute(ship);
        return 0;
    }
    // Bind the standing source reserve to the newly created runtime job.
    // The voyage departure target remains near-full.  A domestic source may
    // transfer only cargo above the reserve; when that temporary ceiling is
    // reached the ship waits instead of departing partially loaded.
    for (int i = 0; i < tracked->activeJobCount; ++i) {
        ActiveShipJob* job = &tracked->activeJobs[i];
        if (job->ship != ship) continue;
        job->sourceReserveFraction = source->threshold;
        job->destinationTriggerFraction = destination->threshold;
        job->sourceExternal = source->externalTarget;
        float onBoard = CargoAmount(ship, source->resource);
        if (onBoard < 0.0f) onBoard = 0.0f;
        float safeAmount = job->targetLoadAmount;
        float safeFraction = job->targetLoadFraction;
        if (ComputeEffectiveLoadTarget(job, onBoard, &safeAmount, &safeFraction)) {
            u8* sourceConfig = RouteConfigAt(ship, job->sourceIndex);
            if (sourceConfig) WriteFloat(sourceConfig, 0x04u, safeFraction);
        }
        break;
    }
    if (H && H->log) {
        H->log("DockDistributionOffice  manager dispatched: office=%p ship=%p source=%p destination=%p resource=%p trigger=%.1f%% departure=%.1f%% source_reserve=%.1f%% external=%d active=%d",
               office, ship, source->target, destination->target, destination->resource,
               destination->threshold * 100.0f, (float)g_minDepartureLoadPermille / 10.0f,
               source->threshold * 100.0f, source->externalTarget, tracked->activeJobCount);
    }
    return 1;
}

static int DemandGateAllowsPlanner(TrackedBuilding* tracked, void* office) {
    if (!g_demandGateEnabled || !tracked || !office) return 1;

    u32 officeAge = (u32)(g_tick - tracked->discoveredTick);
    u32 assignmentAge = (u32)(g_tick - tracked->lastAssignmentChangeTick);
    if (officeAge < (u32)g_demandLoadGraceTicks ||
        assignmentAge < (u32)g_demandAssignmentStableTicks) {
        tracked->lastDemandState = -1;
        ++tracked->plannerSkips;
        if (g_dispatchDiagnostics && H && H->log && tracked->demandScans == 0 &&
            (officeAge == 0 || (officeAge % 300u) == 0u)) {
            H->log("DockDistributionOffice  planner burst load grace: office=%p office_age=%u/%d assignment_age=%u/%d; planner asleep",
                   office, officeAge, g_demandLoadGraceTicks,
                   assignmentAge, g_demandAssignmentStableTicks);
        }
        return 0;
    }

    if (tracked->plannerBurstRemaining <= 0) {
        if (g_tick < tracked->nextDemandCheckTick) {
            tracked->lastDemandState = 0;
            ++tracked->plannerSkips;
            return 0;
        }
        tracked->plannerBurstRemaining = g_plannerBurstCalls;
        if (tracked->plannerBurstRemaining < 1) tracked->plannerBurstRemaining = 1;
        ++tracked->demandScans;
        tracked->lastDemandState = 1;
        if (g_dispatchDiagnostics && H && H->log) {
            H->log("DockDistributionOffice  planner burst start: office=%p burst=%d active=%d starts=%u skipped=%u",
                   office, tracked->plannerBurstRemaining, tracked->activeJobCount,
                   tracked->demandScans, tracked->plannerSkips);
        }
    }

    --tracked->plannerBurstRemaining;
    if (tracked->plannerBurstRemaining <= 0) {
        int sleepTicks = tracked->activeJobCount > 0 ?
                         g_activeDemandRecheckTicks : g_idleDemandRecheckTicks;
        tracked->nextDemandCheckTick = g_tick + (u32)sleepTicks;
        tracked->lastDemandState = 0;
        if (g_dispatchDiagnostics && H && H->log) {
            H->log("DockDistributionOffice  planner burst exhausted: office=%p no route created; sleep=%d ticks runs=%u",
                   office, sleepTicks, tracked->dispatchRuns + 1u);
        }
    }
    return 1;
}

static int ApplyCustomPolicy(void* office, void* ship, const PolicyRouteInfo* route, ActiveShipJob* job) {
    if (!office || !ship || !route || !job || !route->resource) return 0;
    u8* sourceConfig = RouteConfigAt(ship, route->sourceIndex);
    u8* destinationConfig = RouteConfigAt(ship, route->destinationIndex);
    if (!sourceConfig || !destinationConfig) return 0;

    float capacity = CargoCapacity(ship, route->resource);
    float current = CargoAmount(ship, route->resource);
    if (capacity <= 0.01f) return 0;
    if (current < 0.0f) current = 0.0f;
    if (current > capacity) current = capacity;

    // The native stop value is a normalized fraction of compatible vehicle
    // capacity, not a tonnage value. Native DO planning writes the tiny
    // fraction required to cross the destination threshold. For ships we
    // deliberately replace that with the configured departure fraction.
    float targetFraction = (float)g_minDepartureLoadPermille / 1000.0f;
    float currentFraction = current / capacity;
    if (targetFraction < currentFraction) targetFraction = currentFraction;
    if (targetFraction > 1.0f) targetFraction = 1.0f;
    if (targetFraction < 0.01f) targetFraction = 0.01f;
    float targetAmount = capacity * targetFraction;

    // Load half. Keep the native dynamic-completion byte clear while loading;
    // UpdateOneActiveJob reasserts the ship-sized fraction until departure.
    WriteByte(sourceConfig, 0x00u, 1);
    WriteFloat(sourceConfig, 0x04u, targetFraction);
    WriteByte(sourceConfig, 0x28u, 0);

    // Unload half. A value of 1.0 means unload the compatible cargo rather
    // than only the native partial-refill fraction. The completion byte stays
    // clear until CargoAmount reports that the ship is empty.
    WriteByte(destinationConfig, 0xC8u, 1);
    WriteFloat(destinationConfig, 0xCCu, 1.0f);
    WriteByte(destinationConfig, 0xF0u, 0);

    // Do not mark the whole route dirty here. The route and path are already
    // valid; only the cargo policy changed. Marking V_ROUTE_DIRTY while a ship
    // is leaving home can make the native state machine bounce between its
    // in-port and return-home states. The transfer code reads these records
    // directly.

    ZeroBytes(job, sizeof(ActiveShipJob));
    job->ship = ship;
    job->resource = route->resource;
    job->sourceTarget = route->sourceTarget;
    job->destinationTarget = route->destinationTarget;
    job->homeTarget = route->homeTarget;
    job->sourceIndex = route->sourceIndex;
    job->destinationIndex = route->destinationIndex;
    job->homeIndex = route->homeIndex;
    job->lastRouteIndex = ReadRouteIndex(ship);
    job->cargoCapacity = capacity;
    job->targetLoadFraction = targetFraction;
    job->targetLoadAmount = targetAmount;
    job->startedTick = g_tick;

    if (g_customPolicyDiagnostics && H && H->log) {
        H->log("DockDistributionOffice  custom voyage armed: office=%p ship=%p source=%p destination=%p resource=%p capacity=%.2f current=%.2f departure=%.1f%%",
               office, ship, route->sourceTarget, route->destinationTarget,
               route->resource, capacity, current, targetFraction * 100.0f);
    }
    return 1;
}

static int ActiveJobExists(TrackedBuilding* tracked, void* ship) {
    if (!tracked || !ship) return 0;
    for (int i = 0; i < tracked->activeJobCount; ++i) {
        if (tracked->activeJobs[i].ship == ship) return 1;
    }
    return 0;
}

static int RegisterCustomJob(TrackedBuilding* tracked, void* office, void* ship) {
    if (!g_customPolicyEnabled || !tracked || !office || !ship) return 0;
    if (ActiveJobExists(tracked, ship)) return 1;
    if (tracked->activeJobCount >= MAX_ACTIVE_JOBS) {
        if (H && H->log) H->log("DockDistributionOffice  custom voyage table full; leaving native quantities on ship=%p", ship);
        return 0;
    }
    PolicyRouteInfo route;
    if (!InspectPolicyRoute(ship, office, &route)) {
        if (g_customPolicyDiagnostics && H && H->log) {
            H->log("DockDistributionOffice  custom policy could not identify source/destination/resource for ship=%p; native partial quantities retained", ship);
        }
        return 0;
    }
    if (DestinationReserved(tracked, route.destinationTarget) && g_customPolicyDiagnostics && H && H->log) {
        H->log("DockDistributionOffice  warning: native planner produced a duplicate reserved destination; tracking ship=%p destination=%p safely",
               ship, route.destinationTarget);
    }
    ActiveShipJob* slot = &tracked->activeJobs[tracked->activeJobCount];
    if (!ApplyCustomPolicy(office, ship, &route, slot)) {
        if (g_customPolicyDiagnostics && H && H->log) {
            H->log("DockDistributionOffice  custom policy could not read compatible cargo capacity for ship=%p", ship);
        }
        return 0;
    }
    ++tracked->activeJobCount;
    return 1;
}

static int WriteRouteIndex(void* ship, int index) {
    if (!ship || !IsReadable((u8*)ship + V_ROUTE_INDEX, sizeof(int))) return 0;
    *(int*)((u8*)ship + V_ROUTE_INDEX) = index;
    return 1;
}

// After a complete unload, keep the existing reservation and cycle the same
// ship directly back to its source when the destination is still below its
// configured dispatch trigger. V_ROUTE_INDEX is the next schedule stop
// (FUN_14067da00 increments it); V_CURRENT_TARGET remains the physical dock
// during departure. Re-selecting the source therefore avoids a redundant
// office berth while preserving the native three-stop schedule.
static int TryArmDirectRepeat(void* office, ActiveShipJob* job,
                              u8* sourceConfig, u8* destinationConfig) {
    if (!g_directRepeatEnabled || !office || !job || !sourceConfig ||
        !destinationConfig || !g_activeGame) return 0;
    if (!AssignmentExists(office, job->sourceTarget) ||
        !AssignmentExists(office, job->destinationTarget)) return 0;

    // Refresh current task policy in case it changed while the ship was away.
    if (!BindTaskPolicyFromAssignments(office, job)) return 0;
    float amount = 0.0f;
    float capacity = 0.0f;
    if (!SafeCargoStats(job->destinationTarget, job->resource, &amount, &capacity) ||
        capacity <= 0.01f) return 0;
    float fraction = amount / capacity;
    if (fraction + g_triggerEpsilon >= job->destinationTriggerFraction) return 0;

    float safeAmount = job->targetLoadAmount;
    float safeFraction = job->targetLoadFraction;
    if (!ComputeEffectiveLoadTarget(job, 0.0f, &safeAmount, &safeFraction))
        safeFraction = 0.0f; // never bypass a domestic reserve after a failed read

    WriteByte(sourceConfig, 0x00u, 1);
    WriteFloat(sourceConfig, 0x04u, safeFraction);
    WriteByte(sourceConfig, 0x28u, 0);
    WriteByte(destinationConfig, 0xC8u, 1);
    WriteFloat(destinationConfig, 0xCCu, 1.0f);
    WriteByte(destinationConfig, 0xF0u, 0);

    if (!WriteRouteIndex(job->ship, job->sourceIndex)) return 0;
    if (IsReadable((u8*)job->ship + V_ROUTE_DIRTY, sizeof(u16)))
        *(u16*)((u8*)job->ship + V_ROUTE_DIRTY) = 0x0101u;
    if (f_RouteRefresh) f_RouteRefresh((void**)job->ship, 0, 0, 0);
    if (f_VehicleRouteStart)
        f_VehicleRouteStart((longlong)g_activeGame, (longlong)job->ship, 1);

    job->unloadArmed = 0;
    job->emptyConfirmed = 0;
    job->sourceReservePaused = 0;
    job->sourceRoutePinned = 0;
    job->directRepeatPending = 1;
    ++job->directRepeatCount;
    job->lastRouteIndex = job->sourceIndex;
    if (g_customPolicyDiagnostics && H && H->log) {
        H->log("DockDistributionOffice  manager direct repeat armed: ship=%p destination=%p %.2f%% < trigger %.2f%%; next=source repeat=%d",
               job->ship, job->destinationTarget, fraction * 100.0f,
               job->destinationTriggerFraction * 100.0f, job->directRepeatCount);
    }
    return 1;
}

static int ActiveJobNeedsImmediateControl(const TrackedBuilding* tracked) {
    if (!tracked) return 0;
    for (int i = 0; i < tracked->activeJobCount; ++i) {
        const ActiveShipJob* job = &tracked->activeJobs[i];
        if (!job->ship) continue;
        if (job->directRepeatPending || job->sourceRoutePinned || job->sourceReservePaused)
            return 1;
        if (!job->sourceExternal && job->sourceReserveFraction > 0.0001f &&
            ReadPointer(job->ship, V_CURRENT_TARGET) == job->sourceTarget)
            return 1;
    }
    return 0;
}

static int UpdateOneActiveJob(void* office, ActiveShipJob* job) {
    if (!office || !job || !job->ship || !job->resource) return 0;
    void* ship = job->ship;
    if (!IsReadable((u8*)ship + V_CURRENT_TARGET, sizeof(void*)) ||
        !IsReadable((u8*)ship + V_ROUTE_INDEX, sizeof(int))) return 0;

    int inPort = PointerVectorContains(office, B_VEH_BEGIN, B_VEH_END, ship);
    int globallyAlive = IsReadable((u8*)ship + V_ROUTE_END, sizeof(void*));
    if (inPort || globallyAlive) job->missingSinceTick = 0;
    else if (job->missingSinceTick == 0) job->missingSinceTick = g_tick;

    int routeCount = PointerVectorCount(ship, V_ROUTE_BEGIN, V_ROUTE_END, 64);
    if (routeCount <= 0) {
        if (inPort) {
            if (g_customPolicyDiagnostics && H && H->log)
                H->log("DockDistributionOffice  manager voyage complete: office=%p ship=%p route cleared and berthed", office, ship);
            return 0;
        }
        if (globallyAlive) return 1;
        if (job->missingSinceTick != 0 &&
            (u32)(g_tick - job->missingSinceTick) < (u32)g_ownershipGraceTicks) return 1;
        if (H && H->log) H->log("DockDistributionOffice  manager voyage released after missing-ship grace: office=%p ship=%p", office, ship);
        return 0;
    }

    if (routeCount <= 1 && inPort) {
        ClearManagedRoute(ship);
        if (g_customPolicyDiagnostics && H && H->log)
            H->log("DockDistributionOffice  manager voyage complete: office=%p ship=%p returned home", office, ship);
        return 0;
    }

    int configCount = RouteConfigCount(ship);
    if (configCount != routeCount || job->sourceIndex < 0 || job->sourceIndex >= configCount ||
        job->destinationIndex < 0 || job->destinationIndex >= configCount) return 1;

    int currentIndex = ReadRouteIndex(ship);
    void* currentTarget = ReadPointer(ship, V_CURRENT_TARGET);
    u8* sourceConfig = RouteConfigAt(ship, job->sourceIndex);
    u8* destinationConfig = RouteConfigAt(ship, job->destinationIndex);
    if (!sourceConfig || !destinationConfig) return 1;

    float capacity = job->cargoCapacity > 0.01f ? job->cargoCapacity : CargoCapacity(ship, job->resource);
    if (capacity <= 0.01f) return 1;
    float onBoard = CargoAmount(ship, job->resource);
    if (onBoard < 0.0f) onBoard = 0.0f;
    if (onBoard > capacity) onBoard = capacity;
    float onBoardFraction = onBoard / capacity;

    int sourceActive = (currentTarget == job->sourceTarget);
    int destinationActive = (currentTarget == job->destinationTarget);
    // The native route index can advance to the home slot before CURRENT_TARGET
    // leaves the destination.  Never use the index alone to decide that the
    // ship is home; physical membership in the office's in-port vector is the
    // authoritative completion signal.
    int homeTargetActive = (currentTarget == job->homeTarget);

    int controlledIndexAdvance =
        (sourceActive && onBoard + 0.01f < job->targetLoadAmount &&
         currentIndex != job->sourceIndex) ||
        (job->directRepeatPending && currentTarget == job->destinationTarget);
    if (currentIndex != job->lastRouteIndex && !controlledIndexAdvance &&
        g_customPolicyDiagnostics && H && H->log) {
        H->log("DockDistributionOffice  manager voyage stage: ship=%p route_index=%d->%d target=%p cargo=%.2f",
               ship, job->lastRouteIndex, currentIndex, currentTarget, onBoard);
    }

    // Route activation can leave the ship physically at the old destination
    // for several frames. Keep the repeated job pointed at its source until it
    // has actually departed, and avoid rearming the same empty stop repeatedly.
    if (job->directRepeatPending) {
        if (currentTarget == job->destinationTarget) {
            if (currentIndex != job->sourceIndex) {
                WriteRouteIndex(ship, job->sourceIndex);
                currentIndex = job->sourceIndex;
            }
            job->lastRouteIndex = currentIndex;
            return 1;
        }
        job->directRepeatPending = 0;
    }

    if (sourceActive) {
        float safeTargetAmount = job->targetLoadAmount;
        float safeTargetFraction = job->targetLoadFraction;
        int statsOk = ComputeEffectiveLoadTarget(job, onBoard,
                                                 &safeTargetAmount, &safeTargetFraction);
        WriteByte(sourceConfig, 0x00u, 1);

        // The standing source percentage is a transfer reserve, never a
        // reduced departure target.  The native load fraction is temporarily
        // capped at onboard + cargo currently above the reserve. The native
        // index can still mark that temporary ceiling complete; the route-index
        // pin below is therefore the authoritative departure hold. When
        // production or deliveries raise the source above its reserve,
        // ComputeEffectiveLoadTarget raises the ceiling and loading resumes.
        int reachedDepartureTarget = onBoard + 0.01f >= job->targetLoadAmount;
        int reservePaused = 0;
        if (statsOk) {
            WriteFloat(sourceConfig, 0x04u, safeTargetFraction);
            reservePaused = !job->sourceExternal && !reachedDepartureTarget &&
                            safeTargetAmount + 0.01f < job->targetLoadAmount &&
                            onBoard + 0.01f >= safeTargetAmount;
            WriteByte(sourceConfig, 0x28u, reachedDepartureTarget ? 1 : 0);
        } else {
            // A domestic-source statistics failure must not silently bypass
            // the reserve and drain the building. Freeze the transfer ceiling
            // at the cargo already aboard and keep the source stop held.
            float holdFraction = onBoardFraction;
            if (holdFraction < 0.0f) holdFraction = 0.0f;
            if (holdFraction > 1.0f) holdFraction = 1.0f;
            WriteFloat(sourceConfig, 0x04u, holdFraction);
            WriteByte(sourceConfig, 0x28u, 0);
            reservePaused = !job->sourceExternal && !reachedDepartureTarget;
        }

        if (reservePaused != job->sourceReservePaused &&
            g_customPolicyDiagnostics && H && H->log) {
            if (reservePaused) {
                H->log("DockDistributionOffice  manager source reserve hold: ship=%p source=%p cargo=%.2f/%.2f reserve=%.1f%%; waiting for replenishment",
                       ship, job->sourceTarget, onBoard, job->targetLoadAmount,
                       job->sourceReserveFraction * 100.0f);
            } else {
                H->log("DockDistributionOffice  manager source reserve released: ship=%p source=%p cargo=%.2f/%.2f; loading resumed",
                       ship, job->sourceTarget, onBoard, job->targetLoadAmount);
            }
        }
        job->sourceReservePaused = reservePaused;

        // FUN_14067da00 advances V_ROUTE_INDEX when the temporary transfer
        // ceiling is reached. The index is the next stop; CURRENT_TARGET still
        // identifies the physical source berth. Pin the next stop to source
        // until the real 99.9% ship target has been reached.
        if (!reachedDepartureTarget && currentIndex != job->sourceIndex) {
            if (WriteRouteIndex(ship, job->sourceIndex)) {
                currentIndex = job->sourceIndex;
                if (!job->sourceRoutePinned && g_customPolicyDiagnostics && H && H->log) {
                    H->log("DockDistributionOffice  manager source departure blocked: ship=%p cargo=%.2f/%.2f reserve=%.1f%%; next stop pinned to source",
                           ship, onBoard, job->targetLoadAmount,
                           job->sourceReserveFraction * 100.0f);
                }
                job->sourceRoutePinned = 1;
            }
        } else if (reachedDepartureTarget && job->sourceRoutePinned) {
            if (g_customPolicyDiagnostics && H && H->log) {
                H->log("DockDistributionOffice  manager source departure released: ship=%p cargo=%.2f/%.2f (%.1f%%)",
                       ship, onBoard, job->targetLoadAmount, onBoardFraction * 100.0f);
            }
            job->sourceRoutePinned = 0;
        }
    }

    if (destinationActive) {
        WriteByte(destinationConfig, 0xC8u, 1);
        WriteFloat(destinationConfig, 0xCCu, 1.0f);
        if (onBoard > 0.01f && g_waitUntilEmpty) {
            WriteByte(destinationConfig, 0xF0u, 0);
            if (!job->unloadArmed && g_customPolicyDiagnostics && H && H->log) {
                H->log("DockDistributionOffice  manager full-unload hold armed: ship=%p destination=%p cargo=%.2f (%.1f%% capacity)",
                       ship, job->destinationTarget, onBoard, onBoardFraction * 100.0f);
            }
            job->unloadArmed = 1;
        }
        else {
            WriteFloat(destinationConfig, 0xCCu, 0.0f);
            WriteByte(destinationConfig, 0xF0u, 1);
            if (!job->emptyConfirmed && g_customPolicyDiagnostics && H && H->log)
                H->log("DockDistributionOffice  manager full-unload hold released: ship=%p cargo empty", ship);
            job->emptyConfirmed = 1;
        }
    }

    if (destinationActive && onBoard <= 0.01f && job->emptyConfirmed &&
        TryArmDirectRepeat(office, job, sourceConfig, destinationConfig)) {
        return 1;
    }

    // Managed routes are temporary, unlike player-authored cyclic schedules.
    // Keep the home leg intact until the ship is physically berthed in the
    // Distribution Dock.  In v0.7.4 the route index advanced to home while
    // CURRENT_TARGET still pointed at the destination, so the route was
    // cleared early and empty ships lingered at cargo harbours.
    if (inPort && onBoard <= 0.01f && (job->emptyConfirmed || job->unloadArmed) &&
        (homeTargetActive || currentIndex == job->homeIndex || routeCount <= 1)) {
        if (ClearManagedRoute(ship) && g_customPolicyDiagnostics && H && H->log)
            H->log("DockDistributionOffice  manager cleared completed temporary route after berth: office=%p ship=%p", office, ship);
        return 0;
    }

    job->lastRouteIndex = currentIndex;
    return 1;
}

static int UpdateActiveJobs(TrackedBuilding* tracked, void* office) {
    if (!tracked || !office || tracked->activeJobCount <= 0) return 0;
    int write = 0;
    int completed = 0;
    for (int read = 0; read < tracked->activeJobCount; ++read) {
        ActiveShipJob job = tracked->activeJobs[read];
        if (UpdateOneActiveJob(office, &job)) {
            tracked->activeJobs[write++] = job;
        }
        else {
            ++completed;
        }
    }
    for (int i = write; i < tracked->activeJobCount; ++i) {
        ZeroBytes(&tracked->activeJobs[i], sizeof(ActiveShipJob));
    }
    tracked->activeJobCount = write;
    if (completed > 0) {
        if (write == 0) tracked->lastPolicyCompletionTick = g_tick;
        tracked->nextDemandCheckTick = 0;
        tracked->nextControllerTick = 0;
        tracked->lastDemandState = -1;
        tracked->plannerBurstRemaining = 0;
    }
    return write;
}

static int ReconstructAssignedVoyages(TrackedBuilding* tracked, void* office) {
    if (!g_customPolicyEnabled || !g_reconstructAssignedVoyages || !tracked || !office ||
        tracked->activeJobCount >= MAX_ACTIVE_JOBS) return 0;
    if ((u32)(g_tick - tracked->lastAssignedScanTick) < 300u) return 0;
    tracked->lastAssignedScanTick = g_tick;

    void* assigned[64];
    int count = 0;
    count = AppendUniquePointerVector(office, B_ASSIGNED_BEGIN, B_ASSIGNED_END,
                                      assigned, count, 64);
    int rebuilt = 0;
    for (int i = 0; i < count && tracked->activeJobCount < MAX_ACTIVE_JOBS; ++i) {
        void* ship = assigned[i];
        if (!ship || ActiveJobExists(tracked, ship)) continue;
        PolicyRouteInfo route;
        if (!InspectPolicyRoute(ship, office, &route)) continue;
        if (route.homeTarget != office) continue;
        if (!AssignmentExists(office, route.sourceTarget) ||
            !AssignmentExists(office, route.destinationTarget)) continue;
        ActiveShipJob* slot = &tracked->activeJobs[tracked->activeJobCount];
        if (!ApplyCustomPolicy(office, ship, &route, slot)) continue;
        BindTaskPolicyFromAssignments(office, slot);
        slot->lastOwnershipState = 2;
        ++tracked->activeJobCount;
        ++rebuilt;
        if (g_customPolicyDiagnostics && H && H->log) {
            H->log("DockDistributionOffice  reconstructed custom voyage from assigned fleet: office=%p ship=%p destination=%p resource=%p",
                   office, ship, route.destinationTarget, route.resource);
        }
    }
    return rebuilt;
}

static void RunPlannerForOffice(void* game, void* office) {
    if (!g_managerEnabled || !game || !office) return;
    g_activeGame = game;

    TrackedBuilding* tracked = GetTrackedBuilding(office);
    if (!tracked) {
        LogAssignmentChange(office);
        tracked = GetTrackedBuilding(office);
    }
    if (!tracked) return;

    if ((u32)(g_tick - tracked->discoveredTick) >= (u32)g_loadGraceTicks)
        ReconstructWorldVoyages(tracked, game, office);

    int cadence = tracked->activeJobCount > 0 ? g_activeControllerTicks : g_idleControllerTicks;
    if (ActiveJobNeedsImmediateControl(tracked)) cadence = g_sourceReserveControlTicks;
    if (cadence < 1) cadence = 1;
    if (tracked->nextControllerTick != 0 && (int)(g_tick - tracked->nextControllerTick) < 0) return;
    tracked->nextControllerTick = g_tick + (u32)cadence;

    // Active jobs cache task percentages when they are created. The native UI
    // edits those standing assignments in place, so refresh the cached policy
    // once the panel has been quiet, and periodically as a fallback. This makes
    // lowering a source reserve release an already-held ship without rebuilding
    // its route or sending it home.
    int editingThisOffice = (g_uiEditingOffice == office && g_uiEditingHeartbeatTick != 0);
    u32 uiEditAge = editingThisOffice ? (u32)(g_tick - g_uiEditingHeartbeatTick) : 0;
    int uiQuiet = !editingThisOffice || uiEditAge >= (u32)g_uiEditQuietTicks;
    int settledUiEdit = editingThisOffice && uiQuiet &&
                        tracked->lastPolicyRefreshHeartbeatTick != g_uiEditingHeartbeatTick;
    int periodicRefresh = tracked->lastTaskPolicyRefreshTick == 0 ||
                          (u32)(g_tick - tracked->lastTaskPolicyRefreshTick) >=
                              (u32)g_activePolicyRefreshTicks;
    if (tracked->activeJobCount > 0 && uiQuiet &&
        (settledUiEdit || periodicRefresh)) {
        if (RefreshActiveTaskPolicies(tracked, office, settledUiEdit)) {
            tracked->lastTaskPolicyRefreshTick = g_tick;
            if (settledUiEdit)
                tracked->lastPolicyRefreshHeartbeatTick = g_uiEditingHeartbeatTick;
        }
    }

    UpdateActiveJobs(tracked, office);
    if (ActiveJobNeedsImmediateControl(tracked))
        tracked->nextControllerTick = g_tick + (u32)g_sourceReserveControlTicks;

    // Never evaluate connected storage while the native Distribution Office
    // panel is actively mutating assignment vectors.  Keep active voyage
    // maintenance alive, but defer new dispatch until the panel has been quiet.
    if (g_uiEditingOffice == office &&
        (u32)(g_tick - g_uiEditingHeartbeatTick) < (u32)g_uiEditQuietTicks) return;

    int assignmentCount = AssignmentCount(office);
    int inPortCount = PointerVectorCount(office, B_VEH_BEGIN, B_VEH_END, 256);
    if (assignmentCount <= 0 || inPortCount <= 0) return;

    if ((u32)(g_tick - tracked->discoveredTick) < (u32)g_loadGraceTicks ||
        (u32)(g_tick - tracked->lastAssignmentChangeTick) < (u32)g_assignmentStableTicks) return;

    static AssignmentTask sources[MAX_MANAGER_TASKS];
    static AssignmentTask destinations[MAX_MANAGER_TASKS];
    ZeroBytes(sources, sizeof(sources));
    ZeroBytes(destinations, sizeof(destinations));
    int sourceCount = 0, destinationCount = 0;
    int tasksReady = ReadAssignmentTasks(office, sources, MAX_MANAGER_TASKS,
                                         destinations, MAX_MANAGER_TASKS,
                                         &sourceCount, &destinationCount);
    if (!tasksReady) {
        if (g_dispatchDiagnostics && H && H->log &&
            (u32)(g_tick - tracked->lastControllerLogTick) >= 1800u) {
            tracked->lastControllerLogTick = g_tick;
            H->log("DockDistributionOffice  manager task table incomplete: office=%p assignments=%d sources=%d destinations=%d in_port=%d",
                   office, assignmentCount, sourceCount, destinationCount, inPortCount);
            LogAssignmentHalfDiagnostics(office);
        }
        return;
    }

    int dispatched = 0;
    int demandCount = 0;
    int statsFailures = 0;
    int noMatchingSource = 0;
    int noCompatibleShip = 0;
    for (int d = 0; d < destinationCount && dispatched < g_maxDispatchPerPass; ++d) {
        AssignmentTask* destination = &destinations[d];
        if (TaskReserved(tracked, destination->target, destination->resource) ||
            DestinationReserved(tracked, destination->target)) continue;

        float amount = 0.0f, capacity = 0.0f;
        if (!SafeCargoStats(destination->target, destination->resource, &amount, &capacity)) {
            ++statsFailures;
            continue;
        }
        float fraction = amount / capacity;
        if (fraction + g_triggerEpsilon >= destination->threshold) continue;
        ++demandCount;

        AssignmentTask* selectedSource = 0;
        for (int sidx = 0; sidx < sourceCount; ++sidx) {
            if (sources[sidx].resource != destination->resource) continue;
            if (!SourceCanSupply(&sources[sidx])) continue;
            selectedSource = &sources[sidx];
            break;
        }
        if (!selectedSource) {
            ++noMatchingSource;
            continue;
        }

        void* ship = FindAvailableShip(office, destination->resource);
        if (!ship) {
            ++noCompatibleShip;
            break;
        }
        if (DispatchManagedJob(game, tracked, office, selectedSource, destination, ship)) {
            ++dispatched;
            tracked->lastJobTick = g_tick;
        }
    }

    if (g_dispatchDiagnostics && H && H->log &&
        (u32)(g_tick - tracked->lastControllerLogTick) >= 1800u) {
        tracked->lastControllerLogTick = g_tick;
        H->log("DockDistributionOffice  manager evaluation: office=%p assignments=%d sources=%d destinations=%d in_port=%d active=%d demand=%d no_source=%d no_ship=%d stats_fail=%d dispatched=%d next=%d ticks",
               office, assignmentCount, sourceCount, destinationCount, inPortCount,
               tracked->activeJobCount, demandCount, noMatchingSource,
               noCompatibleShip, statsFailures, dispatched, cadence);
    }
}

static int OfficeIsUsable(void* office) {
    if (!office || !IsReadable((u8*)office + B_DEMOLISHED, 1)) return 0;
    if (*((u8*)office + B_DEMOLISHED)) return 0;
    if (IsReadable((u8*)office + B_FINISHED, sizeof(float)) &&
        *(float*)((u8*)office + B_FINISHED) < 0.999f) return 0;
    return 1;
}

static void DiscoverDistributionDocks(void* game) {
    if (!game || !IsReadable((u8*)game + G_BUILDINGS, 16)) return;
    void** begin = *(void***)((u8*)game + G_BUILDINGS);
    void** end = *(void***)((u8*)game + G_BUILDINGS_END);
    if (!begin || !end || end < begin) return;
    usize count = (usize)(end - begin);
    if (count > 200000 || (count && !IsReadable(begin, count * sizeof(void*)))) return;

    for (usize i = 0; i < count; ++i) {
        void* office = begin[i];
        if (!OfficeIsUsable(office) || !IsDistributionDock(office)) continue;
        if (!GetTrackedBuilding(office)) LogAssignmentChange(office);
    }
}

static void RunTrackedDistributionDocks(void* game) {
    if (!game) return;
    for (int i = 0; i < g_trackedCount; ++i) {
        void* office = g_tracked[i].building;
        if (!OfficeIsUsable(office) || !IsDistributionDock(office)) continue;
        RunPlannerForOffice(game, office);
    }
}

static void HookBuildingDispatch(void* game) {
    o_BuildingDispatch(game);
    if (!g_enabled || !g_dispatchEnabled || !game) return;
    ++g_tick;
    int stride = g_dispatchStride < 1 ? 1 : g_dispatchStride;
    if ((g_tick % (u32)stride) != 0) return;

    if (g_lastWorldScanTick == 0 ||
        (u32)(g_tick - g_lastWorldScanTick) >= (u32)g_worldScanTicks) {
        DiscoverDistributionDocks(game);
        g_lastWorldScanTick = g_tick;
    }
    RunTrackedDistributionDocks(game);
}

static void HookVehicleUpdate(void* vehicle) {
    // Safety rule inherited from the fallback branch: never mutate a shared building descriptor from a
    // vehicle-update hook.  The hook is not installed, and this pass-through
    // remains only so older source references compile cleanly.
    o_VehicleUpdate(vehicle);
}
// --------------------------------------------------------------------- type presentation and hook

static int SpoofDescriptorToRoadDo(void* descriptor, int* oldType) {
    if (!descriptor || !oldType || !IsReadable((u8*)descriptor + T_BUILDING_TYPE, sizeof(int))) return 0;
    *oldType = *(int*)((u8*)descriptor + T_BUILDING_TYPE);
    *(int*)((u8*)descriptor + T_BUILDING_TYPE) = TYPE_ROAD_DO;
    return 1;
}

static void RestoreDescriptorType(void* descriptor, int oldType) {
    if (!descriptor || !IsReadable((u8*)descriptor + T_BUILDING_TYPE, sizeof(int))) return;
    *(int*)((u8*)descriptor + T_BUILDING_TYPE) = oldType;
}

static void HookShipPanel(void* ui, void* window) {
    if (!g_enabled || !window) {
        o_ShipPanel(ui, window);
        return;
    }

    void* building = ReadPointer(window, W_BUILDING);
    if (!building || !IsDistributionDock(building)) {
        o_ShipPanel(ui, window);
        return;
    }

    g_activeGame = ui;
    UpdatePanelHotkeys(ui, building);
    LogAssignmentChange(building);
    g_uiEditingOffice = building;
    g_uiEditingHeartbeatTick = g_tick;

    if (!g_distributionMode) {
        o_ShipPanel(ui, window);
        return;
    }

    void* descriptor = ReadPointer(building, B_TYPEDESC);
    int oldType = TYPE_SHIP_DOCK;
    if (!descriptor || !SpoofDescriptorToRoadDo(descriptor, &oldType)) {
        if (H && H->log) H->log("DockDistributionOffice  could not spoof descriptor; falling back to Fleet panel");
        o_ShipPanel(ui, window);
        return;
    }

    float clipTop = ReadFloatOr(window, W_CLIP_TOP, 0.0f);
    if (!g_loggedPanelOpen && H && H->log) {
        g_loggedPanelOpen = 1;
        H->log("DockDistributionOffice  opening native Distribution Office panel (correct 4-argument ABI)");
    }

    int assignmentCountBeforePanel = AssignmentCount(building);
    f_RoadDoPanel(ui, window, descriptor, clipTop);
    RestoreDescriptorType(descriptor, oldType);
    int assignmentCountAfterPanel = AssignmentCount(building);
    DrawOverseasButtons(ui, window, building,
                        assignmentCountAfterPanel != assignmentCountBeforePanel);
    LogAssignmentChange(building);
}

// --------------------------------------------------------------------- exact byte guards / exports

static const u8 EXPECT_BUILDING_DISPATCH[] = {
    0x48,0x8B,0xC4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x8D,0xA8,0x78,0xFA,0xFF,0xFF
};
static const u8 EXPECT_ROAD_DO_DISPATCH[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x48,0x08,0x55,0x53,0x56,0x57,
    0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57
};
static const u8 EXPECT_VEHICLE_UPDATE[] = {
    0x48,0x89,0x4C,0x24,0x08,0x55,0x53,0x56,0x57,
    0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57
};
static const u8 EXPECT_HIGHLIGHT_BUILDING[] = {
    0x40,0x55,0x53,0x56,0x57,0x48,0x8D,0xAC,0x24,0xF8,0xE5,0xFF,0xFF
};

static const u8 EXPECT_SHIP_PANEL[] = {
    0x4C,0x8B,0xDC,0x55,0x56,0x41,0x55,0x41,0x57,
    0x49,0x8D,0xAB,0xC8,0xF7,0xFF,0xFF
};

static const u8 EXPECT_ROAD_DO_PANEL[] = {
    0x48,0x8B,0xC4,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x8D,0xA8,0xB8,0xF1,0xFF,0xFF
};

static const u8 EXPECT_DO_ADD_SELECTOR[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x48,0x08,0x55,0x53,0x56,0x57,
    0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57
};

static const u8 EXPECT_ASSIGN_PUSH[] = {
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x48,0x8B,0xD9
};
static const u8 EXPECT_ASSIGN_INIT[] = {
    0x33,0xC0,0x48,0x89,0x41,0x08,0x48,0x89,0x41,0x10,0x48,0x89,0x41,0x18
};
static const u8 EXPECT_QWORD_PUSH[] = {
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x41,0x08
};
static const u8 EXPECT_CARGO_CAPACITY[] = {
    0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x54,0x41,0x56
};
static const u8 EXPECT_CARGO_AMOUNT[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18
};

EXPORT unsigned TsmPluginApiVersion(void) { return TSM_API_VERSION; }

EXPORT int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info) {
    H = host;
    if (!H || H->apiVersion != TSM_API_VERSION || !H->exeBase || !H->installInlineHook) return 1;
    EXE = H->exeBase;

    info->name = "DockDistributionOffice";
    info->version = "1.0.0";

    g_enabled = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "enabled", 1);
    g_probe = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "probe", 1);
    g_logAssignments = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "log_assignment_changes", 1);
    g_scanBytes = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "descriptor_scan_bytes", 8192);
    g_hotkeyEnabled = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "enable_panel_hotkey", 1);
    g_toggleVk = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "panel_toggle_vk", 119);
    g_selectorEnabled = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "enable_ship_harbour_selector", 1);
    g_overseasEnabled = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "enable_overseas_targets", 1);
    g_overseasButtons = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "show_overseas_buttons", 1);
    g_autoSeedOverseas = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "auto_seed_overseas_targets", 0);
    g_autoSeedOverseas = 0; // v0.5+: endpoints are buttons/hotkeys only.
    g_overseasHotkeys = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "enable_overseas_hotkeys", 0);
    g_overseasHotkeys = 0; // stable fallback: border nodes are button-only
    g_sovietVk = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "soviet_target_vk", 117);
    g_westernVk = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "western_target_vk", 118);
    g_selectorDiagnostics = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "selector_diagnostics", 1);

    // v0.7.3 owns dispatch itself. The legacy enable_native_dispatch switch no
    // longer controls this hook; enable_manager_controller does.
    g_managerEnabled = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "enable_manager_controller", 1);
    g_dispatchEnabled = g_managerEnabled;
    int requestedLifecycle = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "enable_vehicle_lifecycle_adapter", 0);
    g_vehicleLifecycleEnabled = 0;
    if (requestedLifecycle && H->log) {
        H->log("DockDistributionOffice  vehicle lifecycle adapter forcibly disabled: confirmed crash path removed");
    }
    g_dispatchDiagnostics = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "dispatch_diagnostics", 1);
    g_dispatchStride = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "dispatch_every_ticks", 1);
    g_fleetStableTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "fleet_stable_ticks", 300);
    g_plannerCooldownTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "planner_cooldown_ticks", 300);
    g_customPolicyEnabled = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "enable_custom_ship_policy", 1);
    g_customPolicyDiagnostics = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "custom_policy_diagnostics", 1);
    g_minDepartureLoadPermille = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "minimum_departure_load_permille", -1);
    if (g_minDepartureLoadPermille < 0) {
        int legacyPercent = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "minimum_departure_load_percent", 90);
        g_minDepartureLoadPermille = legacyPercent * 10;
    }
    g_waitUntilEmpty = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "wait_until_fully_unloaded", 1);
    g_taskReservations = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "enable_task_reservations", 1);
    g_reconstructAssignedVoyages = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "reconstruct_assigned_voyages", 1);
    g_ownershipGraceTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "ownership_grace_ticks", 1800);

    // Manager-owned controller cadence and save/load reconstruction.
    g_idleControllerTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "idle_controller_ticks", 300);
    g_activeControllerTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "active_controller_ticks", 15);
    g_uiEditQuietTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "ui_edit_quiet_ticks", 300);
    g_worldScanTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "world_scan_ticks", 600);
    g_loadGraceTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "manager_load_grace_ticks", 1200);
    g_assignmentStableTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "manager_assignment_stable_ticks", 300);
    g_maxDispatchPerPass = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "max_dispatch_per_pass", 3);
    g_reconstructWorldVehicles = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "reconstruct_world_vehicle_routes", 1);
    g_directRepeatEnabled = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "direct_repeat_if_below_trigger", 1);
    g_sourceReserveControlTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "source_reserve_control_ticks", 1);
    g_activePolicyRefreshTicks = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "active_task_policy_refresh_ticks", 300);
    int triggerEpsilonPermille = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "trigger_epsilon_permille", 2);
    g_triggerEpsilon = (float)triggerEpsilonPermille / 1000.0f;

    // Legacy native-planner settings are parsed only so old INIs remain harmless.
    g_demandGateEnabled = 0;
    g_plannerBurstCalls = 0;
    g_idleDemandRecheckTicks = 0;
    g_activeDemandRecheckTicks = 0;
    g_demandLoadGraceTicks = 0;
    g_demandAssignmentStableTicks = 0;
    g_demandEpsilon = 0.0f;
    g_blockPlannerWhileActive = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "block_planner_while_voyage_active", 0);
    if (g_dispatchStride < 1) g_dispatchStride = 1;
    if (g_fleetStableTicks < 30) g_fleetStableTicks = 30;
    if (g_plannerCooldownTicks < 30) g_plannerCooldownTicks = 30;
    if (g_plannerBurstCalls < 30) g_plannerBurstCalls = 30;
    if (g_plannerBurstCalls > 10000) g_plannerBurstCalls = 10000;
    if (g_idleDemandRecheckTicks < 30) g_idleDemandRecheckTicks = 30;
    if (g_activeDemandRecheckTicks < 1) g_activeDemandRecheckTicks = 1;
    if (g_worldScanTicks < 60) g_worldScanTicks = 60;
    if (g_demandLoadGraceTicks < 60) g_demandLoadGraceTicks = 60;
    if (g_demandAssignmentStableTicks < 30) g_demandAssignmentStableTicks = 30;
    if (g_demandEpsilon < 0.0f) g_demandEpsilon = 0.0f;
    if (g_demandEpsilon > 0.05f) g_demandEpsilon = 0.05f;
    if (g_minDepartureLoadPermille < 10) g_minDepartureLoadPermille = 10;
    if (g_ownershipGraceTicks < 300) g_ownershipGraceTicks = 300;
    if (g_minDepartureLoadPermille > 1000) g_minDepartureLoadPermille = 1000;
    if (g_idleControllerTicks < 30) g_idleControllerTicks = 30;
    if (g_activeControllerTicks < 1) g_activeControllerTicks = 1;
    if (g_uiEditQuietTicks < 30) g_uiEditQuietTicks = 30;
    if (g_loadGraceTicks < 300) g_loadGraceTicks = 300;
    if (g_assignmentStableTicks < 30) g_assignmentStableTicks = 30;
    if (g_maxDispatchPerPass < 1) g_maxDispatchPerPass = 1;
    if (g_maxDispatchPerPass > 8) g_maxDispatchPerPass = 8;
    if (g_sourceReserveControlTicks < 1) g_sourceReserveControlTicks = 1;
    if (g_sourceReserveControlTicks > 15) g_sourceReserveControlTicks = 15;
    if (g_activePolicyRefreshTicks < 30) g_activePolicyRefreshTicks = 30;
    if (g_activePolicyRefreshTicks > 3600) g_activePolicyRefreshTicks = 3600;
    if (g_triggerEpsilon < 0.0f) g_triggerEpsilon = 0.0f;
    if (g_triggerEpsilon > 0.05f) g_triggerEpsilon = 0.05f;
    g_buttonXOffset = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "overseas_button_x", 394);
    g_buttonSourceYOffset = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "overseas_button_source_y", 290);
    g_buttonDestinationYOffset = g_buttonSourceYOffset; // horizontal layout
    g_buttonSize = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "overseas_button_size", 42);
    g_buttonGap = H->configInt("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "overseas_button_gap", 4);
    if (g_buttonSize < 16) g_buttonSize = 16;
    if (g_buttonSize > 64) g_buttonSize = 64;
    if (g_buttonGap < 0) g_buttonGap = 0;
    if (g_buttonGap > 64) g_buttonGap = 64;
    H->configString("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "descriptor_marker",
                    g_marker, (int)sizeof(g_marker), "DockDistributionOffice");

    char panel[32];
    H->configString("plugins\\DockDistributionOffice.ini", "DockDistributionOffice", "default_panel",
                    panel, (int)sizeof(panel), "distribution");
    g_distributionMode = EqualsNoCase(panel, "fleet") ? 0 : 1;

    if (!g_enabled) {
        if (H->log) H->log("DockDistributionOffice  disabled in plugins\\DockDistributionOffice.ini");
        return 1;
    }

    if (H->log) {
        H->log("DockDistributionOffice  init v1.0.0 release marker=%s default=%s",
               g_marker, g_distributionMode ? "distribution" : "fleet");
    }
    return 0;
}

EXPORT int TsmPluginStart(void) {
    ResolvedHookSite shipPanelSite;
    ResolvedHookSite selectorSite;
    ResolvedHookSite dispatchSite;
    ZeroBytes(&shipPanelSite, sizeof(shipPanelSite));
    ZeroBytes(&selectorSite, sizeof(selectorSite));
    ZeroBytes(&dispatchSite, sizeof(dispatchSite));

    if (!ResolveHookSite(RVA_SHIP_PANEL, EXPECT_SHIP_PANEL, sizeof(EXPECT_SHIP_PANEL),
                         "ship panel", &shipPanelSite)) {
        if (H->log) H->log("DockDistributionOffice  refused: ship-panel hook target could not be verified");
        return 1;
    }
    if (g_selectorEnabled &&
        !ResolveHookSite(RVA_DO_ADD_SELECTOR, EXPECT_DO_ADD_SELECTOR, sizeof(EXPECT_DO_ADD_SELECTOR),
                         "Distribution Office selector", &selectorSite)) {
        if (H->log) H->log("DockDistributionOffice  refused: selector hook target could not be verified");
        return 1;
    }
    if (g_dispatchEnabled &&
        !ResolveHookSite(RVA_BUILDING_DISPATCH, EXPECT_BUILDING_DISPATCH, sizeof(EXPECT_BUILDING_DISPATCH),
                         "building dispatcher", &dispatchSite)) {
        g_dispatchEnabled = 0;
        g_managerEnabled = 0;
        if (H->log) H->log("DockDistributionOffice  manager controller disabled: dispatcher hook target unresolved");
    }

    u8* roadPanelAddress = ResolveCallable(RVA_ROAD_DO_PANEL, EXPECT_ROAD_DO_PANEL,
                                           sizeof(EXPECT_ROAD_DO_PANEL), "Distribution Office panel");
    u8* assignPushAddress = ResolveCallable(RVA_ASSIGN_PUSH, EXPECT_ASSIGN_PUSH,
                                            sizeof(EXPECT_ASSIGN_PUSH), "assignment vector push");
    u8* assignInitAddress = ResolveCallable(RVA_ASSIGN_INIT, EXPECT_ASSIGN_INIT,
                                            sizeof(EXPECT_ASSIGN_INIT), "assignment config init");
    u8* qwordPushAddress = ResolveCallable(RVA_QWORD_PUSH, EXPECT_QWORD_PUSH,
                                           sizeof(EXPECT_QWORD_PUSH), "qword vector push");
    u8* cargoCapacityAddress = ResolveCallable(RVA_CARGO_CAPACITY, EXPECT_CARGO_CAPACITY,
                                               sizeof(EXPECT_CARGO_CAPACITY), "cargo capacity");
    u8* cargoAmountAddress = ResolveCallable(RVA_CARGO_AMOUNT, EXPECT_CARGO_AMOUNT,
                                             sizeof(EXPECT_CARGO_AMOUNT), "cargo amount");
    u8* buildingStatsBaseAddress = EXE + RVA_BUILDING_STATS_BASE;
    u8* buildingHasExtraAddress = EXE + RVA_BUILDING_HAS_EXTRA;
    u8* buildingStatsExtraAddress = EXE + RVA_BUILDING_STATS_EXTRA;
    if (!IsReadable(buildingStatsBaseAddress, 16) || !IsReadable(buildingHasExtraAddress, 16) ||
        !IsReadable(buildingStatsExtraAddress, 16)) {
        buildingStatsBaseAddress = 0;
        buildingHasExtraAddress = 0;
        buildingStatsExtraAddress = 0;
    }
    g_highlightAddress = ResolveCallable(RVA_HIGHLIGHT_BUILDING, EXPECT_HIGHLIGHT_BUILDING,
                                         sizeof(EXPECT_HIGHLIGHT_BUILDING), "valid building highlight");

    if (!roadPanelAddress || !assignPushAddress || !assignInitAddress || !qwordPushAddress) {
        if (H->log) H->log("DockDistributionOffice  refused: one or more UI/assignment call targets could not be verified");
        return 1;
    }
    if (g_customPolicyEnabled && (!cargoCapacityAddress || !cargoAmountAddress)) {
        g_customPolicyEnabled = 0;
        if (H->log) H->log("DockDistributionOffice  custom ship policy disabled: vehicle cargo call targets unresolved");
    }
    if (g_managerEnabled && (!buildingStatsBaseAddress || !buildingHasExtraAddress || !buildingStatsExtraAddress)) {
        g_managerEnabled = 0;
        g_dispatchEnabled = 0;
        if (H->log) H->log("DockDistributionOffice  manager disabled: verified building-storage call targets unreadable");
    }

    // Direct native schedule construction uses calls verified against the
    // supplied decompile at these RVAs. They are not hook sites, so a readable
    // target is required and the VALIDATION report records the decompiled ABI
    // and structure offsets used by each call.
    if (g_managerEnabled &&
        (!IsReadable(EXE + RVA_ROUTE_TARGET_RESIZE, 16) ||
         !IsReadable(EXE + RVA_ROUTE_CONFIG_RESIZE, 16) ||
         !IsReadable(EXE + RVA_ROUTE_CONFIG_INIT, 16) ||
         !IsReadable(EXE + RVA_CONFIG_HALF_COPY, 16) ||
         !IsReadable(EXE + RVA_ROUTE_REFRESH, 16) ||
         !IsReadable(EXE + RVA_VEHICLE_ROUTE_START, 16))) {
        g_managerEnabled = 0;
        g_dispatchEnabled = 0;
        if (H->log) H->log("DockDistributionOffice  manager controller disabled: one or more verified route calls are unreadable");
    }

    f_RoadDoPanel = (FnRoadDoPanel)roadPanelAddress;
    f_RoadDoDispatch = (FnTwo)(EXE + RVA_ROAD_DO_DISPATCH);
    f_CargoCapacity = (FnCargoCapacity)cargoCapacityAddress;
    f_CargoAmount = (FnCargoAmount)cargoAmountAddress;
    f_BuildingStatsBase = (FnBuildingStatsBase)buildingStatsBaseAddress;
    f_BuildingHasExtra = (FnBuildingHasExtra)buildingHasExtraAddress;
    f_BuildingStatsExtra = (FnBuildingStatsExtra)buildingStatsExtraAddress;
    f_RouteTargetResize = (FnVectorResizeQword)(EXE + RVA_ROUTE_TARGET_RESIZE);
    f_RouteConfigResize = (FnVectorResizeConfig)(EXE + RVA_ROUTE_CONFIG_RESIZE);
    f_RouteConfigInit = (FnRouteConfigInit)(EXE + RVA_ROUTE_CONFIG_INIT);
    f_ConfigHalfCopy = (FnConfigHalfCopy)(EXE + RVA_CONFIG_HALF_COPY);
    f_RouteRefresh = (FnRouteRefresh)(EXE + RVA_ROUTE_REFRESH);
    f_VehicleRouteStart = (FnVehicleRouteStart)(EXE + RVA_VEHICLE_ROUTE_START);
    f_AssignPush = (FnAssignPush)assignPushAddress;
    f_AssignInit = (FnAssignInit)assignInitAddress;
    f_QwordPush = (FnQwordPush)qwordPushAddress;
    f_Malloc = (FnMalloc)ReadIatFunction(H->exeModule, "api-ms-win-crt-heap-l1-1-0.dll", "malloc");
    if (!f_Malloc && g_overseasEnabled) {
        if (H->log) H->log("DockDistributionOffice  refused: game malloc import not resolved for overseas targets");
        return 1;
    }

    ResolveKernelPatchFunctions();
    ResolveHotkeyFunction();
    if (g_overseasButtons && !ResolvePanelImports()) g_overseasButtons = 0;
    if (g_highlightAddress) {
        if (!BuildValidHighlightThunk() && H->log)
            H->log("DockDistributionOffice  selector message fix active, but green redraw thunk could not be allocated");
    }
    else if (H->log) {
        H->log("DockDistributionOffice  selector message fix active, but highlight target unresolved");
    }

    int ok = InstallResolvedHook(&shipPanelSite,
                                 (void*)&HookShipPanel,
                                 (void**)&o_ShipPanel,
                                 sizeof(EXPECT_SHIP_PANEL),
                                 "DockDistributionOffice UI graft");
    if (!ok) {
        if (H->log) H->log("DockDistributionOffice  refused: chain-safe ship-panel hook failed");
        return 1;
    }

    if (g_selectorEnabled) {
        ok = InstallResolvedHook(&selectorSite,
                                 (void*)&HookDoAddSelector,
                                 (void**)&o_DoAddSelector,
                                 sizeof(EXPECT_DO_ADD_SELECTOR),
                                 "DockDistributionOffice ship-harbour selector");
        if (!ok) {
            if (H->log) H->log("DockDistributionOffice  refused: chain-safe selector hook failed");
            return 1;
        }
    }

    if (g_dispatchEnabled) {
        ok = InstallResolvedHook(&dispatchSite,
                                 (void*)&HookBuildingDispatch,
                                 (void**)&o_BuildingDispatch,
                                 sizeof(EXPECT_BUILDING_DISPATCH),
                                 "DockDistributionOffice manager controller");
        if (!ok) {
            g_dispatchEnabled = 0;
            g_managerEnabled = 0;
            if (H->log) H->log("DockDistributionOffice  manager controller disabled: chain-safe dispatcher hook failed");
        }
    }

    if (H->log) {
        H->log("DockDistributionOffice  started: manager-owned controller with live task-policy refresh, direct repeat cycles, and route-pinned source reserves");
        H->log("DockDistributionOffice  building stats calls verified against decompile: base=exe+0x1E82E0 linked_test=exe+0x1E5010 linked_extra=exe+0x1E9270; vehicle cargo helpers retained for ships only");
        H->log("DockDistributionOffice  hook resolver: signature scan + Tesmio FF25 chain-pointer support active");
        H->log("DockDistributionOffice  pointer guard: canonical user range + sentinel/overflow rejection active");
        H->log("DockDistributionOffice  F8 panels; Soviet and Western buttons are horizontal; native-mutation suppression active");
        H->log("DockDistributionOffice  manager policy=%d departure=%.1f%% wait_empty=%d source_reserve=enforced reservations=%d reconstruct_world_routes=%d ownership_grace=%d route_dirty_writes=one-shot-only",
               g_customPolicyEnabled, (float)g_minDepartureLoadPermille / 10.0f, g_waitUntilEmpty,
               g_taskReservations, g_reconstructWorldVehicles, g_ownershipGraceTicks);
        H->log("DockDistributionOffice  manager cadence idle=%d active=%d reserve_control=%d policy_refresh=%d world_scan=%d load_grace=%d assignment_stable=%d ui_quiet=%d max_dispatch=%d trigger_epsilon=%.3f",
               g_idleControllerTicks, g_activeControllerTicks, g_sourceReserveControlTicks,
               g_activePolicyRefreshTicks, g_worldScanTicks, g_loadGraceTicks, g_assignmentStableTicks,
               g_uiEditQuietTicks, g_maxDispatchPerPass, g_triggerEpsilon);
        H->log("DockDistributionOffice  cycle policy: direct_repeat_if_below_trigger=%d; source departure pinned until 99.9%% target",
               g_directRepeatEnabled);
        H->log("DockDistributionOffice  live task policy: panel-close refresh + periodic fallback every %d ticks",
               g_activePolicyRefreshTicks);
        H->log("DockDistributionOffice  task parser: native halves load=+0x0 unload=+0xC8 wildcard_unload=source_resources building_stats=FUN_1401e82e0+FUN_1401e9270 saved_half_recovery=%d diagnostics=%d",
               g_savedTaskHalfFallback, g_taskParseDiagnostics);
        H->log("DockDistributionOffice  persistence: native assignments/routes are saved by game; runtime manager reconstructs from global vehicle routes");
    }
    return 0;
}

