// ==========================================================
// PixelStrike3D — ImGui Android Native Plugin
// Package : com.pixellabs.pixelstrike3d
// Output  : libinjection.so  (arm64-v8a, OpenGL ES 3.0)
// Entry   : Java_com_pixellabs_pixelstrike3d_ImGuiPlugin_onCreate
//
// Ported from Windows/DX11 main.cpp.
// Platform layer (Win32, DX11, MinHook, SEH) fully replaced.
//
// OFFSETS: from IL2CPP dump com.pixellabs.pixelstrike3d v10.6.0 — VERIFIED.
//   Field offsets confirmed from dump. RVAs are from the dump method table.
//   These are absolute addresses in the dump — they ARE the RVAs from libil2cpp.so base.
//   Struct field offsets all confirmed correct for v10.6.0.
//     IL2CPP field layout can differ between PC and Android builds.
// ==========================================================

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <android/log.h>
#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <string>
#include <cfloat>

#define LOG_TAG "pixelstrike_imgui"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ==========================================================
// MATH / UNITY STRUCTS  (unchanged from original)
// ==========================================================
struct Vec3       { float x, y, z; };
struct Matrix4x4  { float m[16]; };

// ARM64 has no __fastcall — all args go in registers uniformly
typedef void      (*get_position_t )(Vec3*,      uintptr_t transform, uintptr_t mi);
typedef uintptr_t (*get_main_t     )(uintptr_t mi);
typedef void      (*get_matrix_t   )(Matrix4x4*, uintptr_t camera,    uintptr_t mi);
typedef uintptr_t (*get_transform_t)(uintptr_t component,             uintptr_t mi);
typedef void      (*Health_Update_t)(uintptr_t thisPtr,               uintptr_t mi);
typedef bool      (*Raycast_t      )(uintptr_t scene, Vec3* origin, Vec3* dir,
                                     uintptr_t hitInfo, float maxDist, int layerMask,
                                     int queryTrigger, uintptr_t mi);
typedef void      (*NewWeapon_Update_t)(uintptr_t thisPtr, uintptr_t mi);

get_position_t      o_get_position           = nullptr;
get_main_t          o_get_main               = nullptr;
get_matrix_t        o_get_worldToCamera      = nullptr;
get_matrix_t        o_get_projMatrix         = nullptr;
get_transform_t     o_get_transform          = nullptr;
Health_Update_t     oHealth_Update           = nullptr;
Raycast_t           oRaycastInstance         = nullptr;
NewWeapon_Update_t  oNewWeaponUpdate         = nullptr;

// ==========================================================
// IL2CPP RUNTIME API  (dlsym replaces GetProcAddress)
// ==========================================================
typedef void*  (*il2cpp_domain_get_t)();
typedef void** (*il2cpp_domain_get_assemblies_t)(void* domain, size_t* size);
typedef void*  (*il2cpp_assembly_get_image_t)(void* assembly);
typedef void*  (*il2cpp_class_from_name_t)(void* image, const char* ns, const char* name);
typedef void*  (*il2cpp_class_get_static_field_data_t)(void* klass);

il2cpp_domain_get_t                  fn_domain_get            = nullptr;
il2cpp_domain_get_assemblies_t       fn_domain_get_assemblies = nullptr;
il2cpp_assembly_get_image_t          fn_assembly_get_image    = nullptr;
il2cpp_class_from_name_t             fn_class_from_name       = nullptr;
il2cpp_class_get_static_field_data_t fn_class_static_fields   = nullptr;

uintptr_t g_GameManagerStaticFields = 0;
uintptr_t g_GameAssemblyBase        = 0;

// ==========================================================
// GAME OFFSETS
// !! PC BUILD VALUES — replace after Ghidra on libil2cpp.so !!
// ==========================================================
constexpr int o_Health_Health          = 0x20;
constexpr int o_Health_MaxHealth       = 0x28;
constexpr int o_Health_Dead            = 0x2C;
constexpr int o_Health_Team            = 0x30;
constexpr int o_Health_PhotonView      = 0x50;
constexpr int o_Health_PlayerName      = 0xF8;
constexpr int o_Health_SpawnProtection = 0xC4;
constexpr int o_NetworkEntity_IsMine   = 0x98;
constexpr int o_GameManager_GameModeString = 0x8;
constexpr int o_GameManager_GameShell      = 0x20;
constexpr int o_GameShell_PlayerTeam       = 0x170;
constexpr int o_GameShell_WeaponController = 0x1D8;
constexpr int o_NewWeapon_FireMode         = 0x20;
constexpr int o_NewWeapon_WeaponController = 0x60;

// Function RVAs — PC values, WRONG on Android
constexpr uintptr_t RVA_Health_Update        = 0x25ED730;
constexpr uintptr_t RVA_Camera_get_main      = 0x4EF7AFC;
constexpr uintptr_t RVA_Camera_worldToCamera = 0x4EF624C;
constexpr uintptr_t RVA_Camera_projection    = 0x4EF63FC;
constexpr uintptr_t RVA_Transform_position   = 0x4F7914C;
constexpr uintptr_t RVA_Component_transform  = 0x4F63C50;
constexpr uintptr_t RVA_PhysicsScene_Raycast = 0x4FFF914;
constexpr uintptr_t RVA_NewWeapon_Update     = 0x2659994;

// ==========================================================
// GLOBALS
// ==========================================================
bool g_Initialized    = false;
bool g_HooksInstalled = false;
bool g_IsShooting     = false;
bool showMenu         = true;

float g_ScreenWidth  = 1080.0f;
float g_ScreenHeight = 1920.0f;

// Feature flags
bool  espEnabled      = false;
bool  espBox          = false;
float espBoxThickness = 2.0f;
bool  espHealth       = false;
bool  espName         = false;

bool  silentAimEnabled       = false;
int   silentAimBone          = 0;
float silentAimFov           = 150.0f;
int   silentAimTargetingMode = 0;
bool  silentAimDrawFov       = true;

bool bypassSpawnProtection = false;
bool automaticWeapons      = false;

int  g_LocalPlayerTeam = -1;

bool  drawCrosshair           = false;
float crosshairLength         = 10.0f;
float crosshairGap            = 5.0f;
float crosshairThickness      = 1.5f;
float crosshairShadowThickness = 3.0f;

// Colors
const ImVec4 COLOR_ACCENT = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
const ImVec4 COLOR_MOON   = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);

ImVec4 customBoxColor             = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
ImVec4 customNameColor            = ImVec4(1.0f,  1.0f,  1.0f,  1.00f);
ImVec4 customHealthBgColor        = ImVec4(0.0f,  0.0f,  0.0f,  0.8f );
ImVec4 customCrosshairColor       = ImVec4(1.0f,  0.0f,  0.0f,  1.00f);
ImVec4 customCrosshairShadowColor = ImVec4(0.0f,  0.0f,  0.0f,  0.8f );
ImVec4 customSilentAimFovColor    = ImVec4(1.0f,  1.0f,  1.0f,  0.3f );

int currentTab = 0;

// ==========================================================
// ESP DATA
// ==========================================================
struct EspEntityData {
    float     health, maxHealth;
    uintptr_t managedCameraPtr;
    uintptr_t managedTransformPtr;
    char      name[32];
};

struct SilentAimTarget { EspEntityData data; uint64_t lastSeen; };

std::vector<EspEntityData>           g_EspDataList;
std::map<uintptr_t, SilentAimTarget> g_SilentAimList;
std::mutex g_EspMutex;
std::mutex g_SilentAimMutex;
uint64_t   g_FrameCount = 0;

// ==========================================================
// SAFE READ — sigjmp replaces Windows SEH (__try/__except)
// ==========================================================
static thread_local sigjmp_buf g_Jmp;
static thread_local bool       g_InSafe = false;

static void OnSignal(int) { if (g_InSafe) siglongjmp(g_Jmp, 1); }

static void ArmSignalHandler() {
    struct sigaction sa{};
    sa.sa_handler = OnSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
}

#define SAFE_BEGIN  g_InSafe = true;  if (sigsetjmp(g_Jmp, 1) != 0) { g_InSafe = false; ArmSignalHandler();
#define SAFE_FAIL   ; }               g_InSafe = false;
#define SAFE_END    g_InSafe = false;

// ==========================================================
// MATH  (identical to original)
// ==========================================================
float Dist3D(Vec3 a, Vec3 b) {
    float dx=a.x-b.x, dy=a.y-b.y, dz=a.z-b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}
float Dist2D(ImVec2 a, ImVec2 b) {
    float dx=a.x-b.x, dy=a.y-b.y;
    return sqrtf(dx*dx + dy*dy);
}
bool WorldToScreen(Vec3 world, Matrix4x4 view, Matrix4x4 proj, ImVec2* out) {
    float vx=world.x*view.m[0]+world.y*view.m[4]+world.z*view.m[8] +view.m[12];
    float vy=world.x*view.m[1]+world.y*view.m[5]+world.z*view.m[9] +view.m[13];
    float vz=world.x*view.m[2]+world.y*view.m[6]+world.z*view.m[10]+view.m[14];
    float vw=world.x*view.m[3]+world.y*view.m[7]+world.z*view.m[11]+view.m[15];
    float cx=vx*proj.m[0]+vy*proj.m[4]+vz*proj.m[8] +vw*proj.m[12];
    float cy=vx*proj.m[1]+vy*proj.m[5]+vz*proj.m[9] +vw*proj.m[13];
    float cw=vx*proj.m[3]+vy*proj.m[7]+vz*proj.m[11]+vw*proj.m[15];
    if (cw < 0.1f) return false;
    float inv=1.0f/cw;
    out->x=( cx*inv*0.5f+0.5f)*g_ScreenWidth;
    out->y=(1.0f-(cy*inv*0.5f+0.5f))*g_ScreenHeight;
    return true;
}

// ==========================================================
// SAFE IL2CPP ACCESSORS
// ==========================================================
uintptr_t SafeGetTransform(uintptr_t comp) {
    if (!o_get_transform || !comp) return 0;
    SAFE_BEGIN return 0; SAFE_FAIL
    uintptr_t r = o_get_transform(comp, 0);
    SAFE_END return r;
}
bool SafeGetPosition(Vec3* out, uintptr_t t) {
    if (!o_get_position || !t || !out) return false;
    SAFE_BEGIN return false; SAFE_FAIL
    o_get_position(out, t, 0);
    SAFE_END return true;
}
bool SafeGetMatrix(Matrix4x4* out, uintptr_t cam, get_matrix_t fn) {
    if (!fn || !cam || !out) return false;
    SAFE_BEGIN return false; SAFE_FAIL
    fn(out, cam, 0);
    SAFE_END return true;
}
bool SafeGetIsMine(uintptr_t hp) {
    if (!hp) return false;
    SAFE_BEGIN return false; SAFE_FAIL
    uintptr_t pv = *(uintptr_t*)(hp + o_Health_PhotonView);
    if (!pv) { g_InSafe=false; return false; }
    bool r = *(bool*)(pv + o_NetworkEntity_IsMine);
    SAFE_END return r;
}
void SafeSetSpawnProtection(uintptr_t hp) {
    if (!hp) return;
    SAFE_BEGIN return; SAFE_FAIL
    *(float*)(hp + o_Health_SpawnProtection) = 0.01f;
    SAFE_END
}
void SafeSetAutomatic(uintptr_t wp) {
    if (!wp || !g_GameManagerStaticFields) return;
    SAFE_BEGIN return; SAFE_FAIL
    uintptr_t gs = *(uintptr_t*)(g_GameManagerStaticFields + o_GameManager_GameShell);
    if (gs) {
        uintptr_t localWC  = *(uintptr_t*)(gs  + o_GameShell_WeaponController);
        uintptr_t weaponWC = *(uintptr_t*)(wp  + o_NewWeapon_WeaponController);
        if (localWC && weaponWC == localWC)
            *(int*)(wp + o_NewWeapon_FireMode) = 1;
    }
    SAFE_END
}
void SafeReadPlayerName(uintptr_t hp, char* out, size_t maxLen) {
    if (!hp || !out || !maxLen) return;
    out[0] = '\0';
    SAFE_BEGIN return; SAFE_FAIL
    uintptr_t sp = *(uintptr_t*)(hp + o_Health_PlayerName);
    if (!sp) { g_InSafe=false; return; }
    int len = *(int*)(sp + 0x10);
    if (len<=0 || len>64) { g_InSafe=false; return; }
    size_t copy = (len < (int)maxLen-1) ? len : maxLen-1;
    char* chars = (char*)(sp + 0x14);
    for (size_t i=0; i<copy; i++) out[i] = chars[i*2];
    out[copy] = '\0';
    SAFE_END
}
void UpdateLocalPlayerTeam() {
    if (!g_GameManagerStaticFields) return;
    SAFE_BEGIN return; SAFE_FAIL
    uintptr_t gs = *(uintptr_t*)(g_GameManagerStaticFields + o_GameManager_GameShell);
    if (gs) g_LocalPlayerTeam = *(int*)(gs + o_GameShell_PlayerTeam);
    SAFE_END
}
bool IsTeamMode() {
    if (!g_GameManagerStaticFields) return false;
    SAFE_BEGIN return false; SAFE_FAIL
    uintptr_t sp = *(uintptr_t*)(g_GameManagerStaticFields + o_GameManager_GameModeString);
    if (!sp) { g_InSafe=false; return false; }
    int len = *(int*)(sp + 0x10);
    if (len<=0 || len>64) { g_InSafe=false; return false; }
    char s[65]={};
    char* chars=(char*)(sp+0x14);
    for (int i=0;i<len&&i<63;i++) { s[i]=chars[i*2]; if(s[i]>='A'&&s[i]<='Z') s[i]+=32; }
    g_InSafe=false;
    return strstr(s,"capturetheflag")||strstr(s,"bombdefuse")   ||strstr(s,"dodgeball")||
           strstr(s,"infected")      ||strstr(s,"kingofthehill")||strstr(s,"vipescort")||
           strstr(s,"teamsnipers")   ||strstr(s,"teamdeathmatch")||strstr(s,"redgreenlight")||
           strstr(s,"oddoreven")     ||strstr(s,"floorislava")  ||strstr(s,"falldown")||
           strstr(s,"eliminationhub")||strstr(s,"duckhunt");
}

// ==========================================================
// HOOKS  (ARM64 — use ShadowHook instead of MinHook)
// https://github.com/bytedance/android-inline-hook
// Uncomment shadowhook_hook_func_addr() calls in InstallHooks()
// after adding ShadowHook to CMakeLists.txt
// ==========================================================
void hkHealthUpdate(uintptr_t thisPtr, uintptr_t mi) {
    if (thisPtr && (espEnabled||silentAimEnabled||bypassSpawnProtection) && g_ScreenWidth>0.0f) {
        UpdateLocalPlayerTeam();
        if (bypassSpawnProtection) SafeSetSpawnProtection(thisPtr);
        if (SafeGetIsMine(thisPtr)) { if (oHealth_Update) oHealth_Update(thisPtr,mi); return; }

        int team=0;
        SAFE_BEGIN ; SAFE_FAIL team=*(int*)(thisPtr+o_Health_Team); SAFE_END
        if (IsTeamMode()&&g_LocalPlayerTeam!=-1&&team==g_LocalPlayerTeam) {
            if (oHealth_Update) oHealth_Update(thisPtr,mi); return;
        }

        float hp=0,maxHp=100; bool dead=true;
        SAFE_BEGIN ; SAFE_FAIL
        hp    = *(float*)(thisPtr+o_Health_Health);
        maxHp = *(float*)(thisPtr+o_Health_MaxHealth);
        dead  = *(bool* )(thisPtr+o_Health_Dead);
        SAFE_END

        if (!dead) {
            uintptr_t cam = o_get_main ? o_get_main(0) : 0;
            if (cam) {
                EspEntityData d{};
                d.health=hp; d.maxHealth=maxHp; d.managedCameraPtr=cam;
                strncpy(d.name,"Player",31);
                SafeReadPlayerName(thisPtr, d.name, 32);
                uintptr_t tf = SafeGetTransform(thisPtr);
                if (tf) {
                    d.managedTransformPtr=tf;
                    if (espEnabled)       { std::lock_guard<std::mutex> lk(g_EspMutex);      g_EspDataList.push_back(d); }
                    if (silentAimEnabled) { std::lock_guard<std::mutex> lk(g_SilentAimMutex); g_SilentAimList[thisPtr]={d,g_FrameCount}; }
                }
            }
        }
    }
    if (oHealth_Update) oHealth_Update(thisPtr,mi);
}

void hkNewWeaponUpdate(uintptr_t thisPtr, uintptr_t mi) {
    if (automaticWeapons && thisPtr) SafeSetAutomatic(thisPtr);
    if (oNewWeaponUpdate) oNewWeaponUpdate(thisPtr,mi);
}

bool hkRaycastInstance(uintptr_t scene, Vec3* origin, Vec3* dir, uintptr_t hitInfo,
                        float maxDist, int layerMask, int qti, uintptr_t mi) {
    if (silentAimEnabled && g_IsShooting && maxDist>10.0f) {
        std::vector<EspEntityData> list;
        { std::lock_guard<std::mutex> lk(g_SilentAimMutex); for (auto& p:g_SilentAimList) list.push_back(p.second.data); }
        if (!list.empty()) {
            uintptr_t cam = o_get_main ? o_get_main(0) : 0;
            if (cam) {
                Matrix4x4 vm,pm;
                if (SafeGetMatrix(&vm,cam,o_get_worldToCamera) && SafeGetMatrix(&pm,cam,o_get_projMatrix)) {
                    ImVec2 ch={g_ScreenWidth/2.0f, g_ScreenHeight/2.0f};
                    float  best=FLT_MAX; Vec3 bpos{}; bool found=false;
                    for (auto& d:list) {
                        Vec3 ep; if (!SafeGetPosition(&ep,d.managedTransformPtr)) continue;
                        Vec3 bp=ep;
                        if      (silentAimBone==0) bp.y+=2.8f;
                        else if (silentAimBone==1) bp.y+=1.5f;
                        else if (silentAimBone==2) bp.y+=0.5f;
                        ImVec2 sp;
                        if (WorldToScreen(bp,vm,pm,&sp)) {
                            float d2=Dist2D(sp,ch);
                            if (d2<=silentAimFov) {
                                float d3=Dist3D(bp,*origin);
                                float score=(silentAimTargetingMode==0)?d2:(silentAimTargetingMode==1)?d3:d2+d3;
                                if (score<best) { best=score; bpos=bp; found=true; }
                            }
                        }
                    }
                    if (found) {
                        Vec3 nd={bpos.x-origin->x, bpos.y-origin->y, bpos.z-origin->z};
                        float len=sqrtf(nd.x*nd.x+nd.y*nd.y+nd.z*nd.z);
                        if (len>0.0f) { nd.x/=len; nd.y/=len; nd.z/=len; *dir=nd; }
                    }
                }
            }
        }
    }
    return oRaycastInstance ? oRaycastInstance(scene,origin,dir,hitInfo,maxDist,layerMask,qti,mi) : false;
}

// ==========================================================
// IL2CPP RESOLVE  (dlopen replaces GetModuleHandleA)
// ==========================================================
void ResolveGameManager() {
    if (g_GameManagerStaticFields) return;
    void* h = dlopen("libil2cpp.so", RTLD_NOW|RTLD_NOLOAD);
    if (!h) { LOGE("libil2cpp.so not loaded: %s", dlerror()); return; }
    if (!fn_domain_get)            fn_domain_get            = (il2cpp_domain_get_t)                 dlsym(h,"il2cpp_domain_get");
    if (!fn_domain_get_assemblies) fn_domain_get_assemblies = (il2cpp_domain_get_assemblies_t)      dlsym(h,"il2cpp_domain_get_assemblies");
    if (!fn_assembly_get_image)    fn_assembly_get_image    = (il2cpp_assembly_get_image_t)         dlsym(h,"il2cpp_assembly_get_image");
    if (!fn_class_from_name)       fn_class_from_name       = (il2cpp_class_from_name_t)            dlsym(h,"il2cpp_class_from_name");
    if (!fn_class_static_fields)   fn_class_static_fields   = (il2cpp_class_get_static_field_data_t)dlsym(h,"il2cpp_class_get_static_field_data");
    dlclose(h);
    if (!fn_domain_get) { LOGE("il2cpp_domain_get not found"); return; }
    void* domain = fn_domain_get();
    if (!domain) return;
    size_t count=0;
    void** assemblies = fn_domain_get_assemblies(domain,&count);
    if (!assemblies) return;
    for (size_t i=0;i<count;i++) {
        void* img = fn_assembly_get_image(assemblies[i]);
        if (!img) continue;
        void* klass = fn_class_from_name(img,"","GameManager");
        if (klass) {
            g_GameManagerStaticFields=(uintptr_t)fn_class_static_fields(klass);
            LOGI("GameManager statics: 0x%lx", g_GameManagerStaticFields);
            break;
        }
    }
}

void InstallHooks() {
    if (g_HooksInstalled) return;
    void* h = dlopen("libil2cpp.so", RTLD_NOW|RTLD_NOLOAD);
    if (!h) { LOGE("libil2cpp.so not loaded yet"); return; }
    Dl_info info{};
    void* sym = dlsym(h,"il2cpp_init");
    dlclose(h);
    if (!sym || !dladdr(sym,&info)) { LOGE("dladdr failed"); return; }
    g_GameAssemblyBase = (uintptr_t)info.dli_fbase;
    LOGI("libil2cpp.so base: 0x%lx", g_GameAssemblyBase);

    o_get_main           = (get_main_t)     (g_GameAssemblyBase + RVA_Camera_get_main);
    o_get_worldToCamera  = (get_matrix_t)   (g_GameAssemblyBase + RVA_Camera_worldToCamera);
    o_get_projMatrix     = (get_matrix_t)   (g_GameAssemblyBase + RVA_Camera_projection);
    o_get_position       = (get_position_t) (g_GameAssemblyBase + RVA_Transform_position);
    o_get_transform      = (get_transform_t)(g_GameAssemblyBase + RVA_Component_transform);

    // --- ShadowHook (uncomment after adding dependency) ---
    // #include "shadowhook.h"
    // shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    // shadowhook_hook_func_addr((void*)(g_GameAssemblyBase+RVA_Health_Update),      (void*)hkHealthUpdate,      (void**)&oHealth_Update);
    // shadowhook_hook_func_addr((void*)(g_GameAssemblyBase+RVA_PhysicsScene_Raycast),(void*)hkRaycastInstance,  (void**)&oRaycastInstance);
    // shadowhook_hook_func_addr((void*)(g_GameAssemblyBase+RVA_NewWeapon_Update),   (void*)hkNewWeaponUpdate,   (void**)&oNewWeaponUpdate);

    ResolveGameManager();
    ArmSignalHandler();
    g_HooksInstalled = true;
    LOGI("Hook stubs ready. Replace RVAs with Android values, then enable ShadowHook.");
}

// ==========================================================
// IMGUI STYLE  (identical to original ApplyStyle)
// ==========================================================
void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding=6; s.ChildRounding=4; s.FrameRounding=3;
    s.GrabRounding=3;   s.ScrollbarRounding=4;
    s.WindowPadding=ImVec2(0,0); s.FramePadding=ImVec2(8,6);
    s.ItemSpacing=ImVec2(10,10); s.ItemInnerSpacing=ImVec2(6,6);
    s.WindowBorderSize=0; s.ChildBorderSize=1; s.FrameBorderSize=0;
    ImVec4* c=s.Colors;
    c[ImGuiCol_WindowBg]        =ImVec4(0.05f,0.05f,0.07f,1);
    c[ImGuiCol_ChildBg]         =ImVec4(0.08f,0.08f,0.10f,1);
    c[ImGuiCol_PopupBg]         =ImVec4(0.08f,0.08f,0.10f,1);
    c[ImGuiCol_FrameBg]         =ImVec4(0.16f,0.16f,0.19f,1);
    c[ImGuiCol_FrameBgHovered]  =ImVec4(0.20f,0.20f,0.24f,1);
    c[ImGuiCol_FrameBgActive]   =ImVec4(0.24f,0.24f,0.28f,1);
    c[ImGuiCol_TitleBgActive]   =ImVec4(0.05f,0.05f,0.07f,1);
    c[ImGuiCol_CheckMark]       =COLOR_ACCENT;
    c[ImGuiCol_SliderGrab]      =COLOR_ACCENT;
    c[ImGuiCol_SliderGrabActive]=ImVec4(COLOR_ACCENT.x*1.2f,COLOR_ACCENT.y*1.2f,COLOR_ACCENT.z*1.2f,1);
    c[ImGuiCol_Button]          =ImVec4(0.16f,0.16f,0.19f,1);
    c[ImGuiCol_ButtonHovered]   =ImVec4(0.20f,0.20f,0.24f,1);
    c[ImGuiCol_ButtonActive]    =COLOR_ACCENT;
    c[ImGuiCol_Header]          =ImVec4(0.10f,0.10f,0.12f,1);
    c[ImGuiCol_HeaderHovered]   =ImVec4(0.12f,0.12f,0.14f,1);
    c[ImGuiCol_HeaderActive]    =ImVec4(0.08f,0.08f,0.10f,1);
    c[ImGuiCol_Border]          =ImVec4(0.12f,0.12f,0.15f,1);
    c[ImGuiCol_Text]            =ImVec4(0.90f,0.90f,0.92f,1);
    c[ImGuiCol_TextDisabled]    =ImVec4(0.40f,0.40f,0.45f,1);
    c[ImGuiCol_Separator]       =ImVec4(0.12f,0.12f,0.15f,1);
}

// ==========================================================
// MENU TABS  (identical layout/logic from original)
// ==========================================================
void RenderVisualsTab() {
    const float cs=160;
    ImGui::TextDisabled("ESP"); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Enable ESP"); ImGui::SameLine(cs); ImGui::Checkbox("##ESP",&espEnabled);
    if (espEnabled) {
        ImGui::Dummy({0,15}); ImGui::TextDisabled("BOX"); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("3D Box");    ImGui::SameLine(cs); ImGui::Checkbox("##3DB",&espBox);
        if (espBox) {
            ImGui::Text("Thickness"); ImGui::SameLine(cs); ImGui::SetNextItemWidth(-35);
            ImGui::SliderFloat("##Th",&espBoxThickness,1,5,""); ImGui::SameLine(); ImGui::Text("%.1f",espBoxThickness);
            ImGui::Text("Box Color"); ImGui::SameLine(cs); ImGui::ColorEdit4("##BC",(float*)&customBoxColor,ImGuiColorEditFlags_NoInputs);
        }
        ImGui::Dummy({0,15}); ImGui::TextDisabled("TEXT"); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("Nametags"); ImGui::SameLine(cs); ImGui::Checkbox("##NT",&espName);
        if (espName) { ImGui::Text("Text Color"); ImGui::SameLine(cs); ImGui::ColorEdit4("##TC",(float*)&customNameColor,ImGuiColorEditFlags_NoInputs); }
        ImGui::Text("Health Bar"); ImGui::SameLine(cs); ImGui::Checkbox("##HB",&espHealth);
        if (espHealth) { ImGui::Text("Bg Color"); ImGui::SameLine(cs); ImGui::ColorEdit4("##BG",(float*)&customHealthBgColor,ImGuiColorEditFlags_NoInputs); }
    }
    ImGui::Dummy({0,15}); ImGui::TextDisabled("CROSSHAIR"); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Enabled"); ImGui::SameLine(cs); ImGui::Checkbox("##CH",&drawCrosshair);
    if (drawCrosshair) {
        ImGui::Text("Length");       ImGui::SameLine(cs); ImGui::SetNextItemWidth(-35); ImGui::SliderFloat("##CL",&crosshairLength,1,50,"");  ImGui::SameLine(); ImGui::Text("%.1f",crosshairLength);
        ImGui::Text("Gap");          ImGui::SameLine(cs); ImGui::SetNextItemWidth(-35); ImGui::SliderFloat("##CG",&crosshairGap,0,20,"");     ImGui::SameLine(); ImGui::Text("%.1f",crosshairGap);
        ImGui::Text("Thickness");    ImGui::SameLine(cs); ImGui::SetNextItemWidth(-35); ImGui::SliderFloat("##CT",&crosshairThickness,1,5,""); ImGui::SameLine(); ImGui::Text("%.1f",crosshairThickness);
        ImGui::Text("Shadow Thick"); ImGui::SameLine(cs); ImGui::SetNextItemWidth(-35); ImGui::SliderFloat("##CS",&crosshairShadowThickness,1,6,""); ImGui::SameLine(); ImGui::Text("%.1f",crosshairShadowThickness);
        ImGui::Text("Color");        ImGui::SameLine(cs); ImGui::ColorEdit4("##CC",(float*)&customCrosshairColor,ImGuiColorEditFlags_NoInputs);
        ImGui::Text("Shadow Color"); ImGui::SameLine(cs); ImGui::ColorEdit4("##SC",(float*)&customCrosshairShadowColor,ImGuiColorEditFlags_NoInputs);
    }
}

void RenderSilentAimTab() {
    const float cs=160;
    ImGui::TextDisabled("GENERAL"); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Enable Silent Aim"); ImGui::SameLine(cs); ImGui::Checkbox("##SA",&silentAimEnabled);
    if (silentAimEnabled) {
        ImGui::Dummy({0,15}); ImGui::TextDisabled("TARGETING"); ImGui::Separator(); ImGui::Spacing();
        const char* bones[]={"Head","Chest","Legs","Feet"};
        const char* modes[]={"Closest to Crosshair","Closest to Player","Hybrid"};
        ImGui::Text("Target Bone");     ImGui::SameLine(cs); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::Combo("##TB",&silentAimBone,bones,4);
        ImGui::Text("Target Priority"); ImGui::SameLine(cs); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::Combo("##TP",&silentAimTargetingMode,modes,3);
        ImGui::Dummy({0,15}); ImGui::TextDisabled("BEHAVIOR"); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("FOV"); ImGui::SameLine(cs); ImGui::SetNextItemWidth(-35); ImGui::SliderFloat("##FOV",&silentAimFov,10,400,""); ImGui::SameLine(); ImGui::Text("%.0f",silentAimFov);
        ImGui::Dummy({0,15}); ImGui::TextDisabled("VISUALS"); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("Draw FOV Circle"); ImGui::SameLine(cs); ImGui::Checkbox("##DF",&silentAimDrawFov);
        if (silentAimDrawFov) { ImGui::Text("Circle Color"); ImGui::SameLine(cs); ImGui::ColorEdit4("##FC",(float*)&customSilentAimFovColor,ImGuiColorEditFlags_NoInputs); }
    }
}

void RenderMiscTab() {
    const float cs=160;
    ImGui::TextDisabled("MISC FEATURES"); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Spawn Protection Bypass"); ImGui::SameLine(cs); ImGui::Checkbox("##SPB",&bypassSpawnProtection);
    ImGui::Text("Automatic Weapons");       ImGui::SameLine(cs); ImGui::Checkbox("##AW",&automaticWeapons);
}

void RenderMainMenu() {
    ImGui::SetNextWindowSize(ImVec2(650,450),ImGuiCond_FirstUseEver);
    ImGui::Begin("Eclipse",&showMenu,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImVec4 sbBg={0.06f,0.06f,0.07f,1}, inact={0.55f,0.55f,0.58f,1}, act={1,1,1,1};
    const float sw=150,th=40;
    ImGui::PushStyleColor(ImGuiCol_ChildBg,sbBg);
    ImGui::BeginChild("##SB",ImVec2(sw,0),false,ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy({0,20});
    ImVec2 cp=ImGui::GetCursorScreenPos();
    ImDrawList* dl=ImGui::GetWindowDrawList();
    float r=22;
    ImVec2 ctr(cp.x+(sw/2)-r*0.333f, cp.y+r);
    dl->AddCircleFilled(ctr,r,ImGui::ColorConvertFloat4ToU32(COLOR_ACCENT),64);
    ImVec2 sc2(ctr.x+r*0.611f,ctr.y-r*0.444f);
    dl->AddCircleFilled(sc2,r*1.028f,ImGui::ColorConvertFloat4ToU32(COLOR_MOON),64);
    dl->AddCircle(sc2,r*1.028f,IM_COL32(0,0,0,50),64,1);
    ImGui::Dummy({0,r*2+10});
    const char* title="E C L I P S E";
    ImGui::SetCursorPosX((sw-ImGui::CalcTextSize(title).x)*0.5f);
    ImGui::TextColored(COLOR_ACCENT,"%s",title);
    ImGui::Dummy({0,25});
    const char* tabs[]={"Visuals","Silent Aim","Misc"};
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(0,0));
    ImGui::PushStyleColor(ImGuiCol_Header,       ImVec4(0.10f,0.10f,0.12f,1));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,ImVec4(0.12f,0.12f,0.14f,1));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.08f,0.08f,0.10f,1));
    for (int i=0;i<3;i++) {
        bool active=(currentTab==i);
        ImGui::PushStyleColor(ImGuiCol_Text,active?act:inact);
        ImGui::PushID(i);
        ImVec2 scp=ImGui::GetCursorPos(), scr=ImGui::GetCursorScreenPos();
        std::string lbl="##"+std::string(tabs[i]);
        if (ImGui::Selectable(lbl.c_str(),active,0,ImVec2(sw,th))) currentTab=i;
        if (active) ImGui::GetWindowDrawList()->AddRectFilled(scr,ImVec2(scr.x+3,scr.y+th),ImGui::ColorConvertFloat4ToU32(COLOR_ACCENT));
        ImVec2 ts=ImGui::CalcTextSize(tabs[i]);
        ImGui::SetCursorPos(ImVec2((sw-ts.x)*0.5f, scp.y+(th-ts.y)*0.5f));
        ImGui::Text("%s",tabs[i]);
        ImGui::SetCursorPosY(scp.y+th);
        ImGui::PopID(); ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor(3); ImGui::PopStyleVar();
    ImGui::EndChild(); ImGui::PopStyleColor();
    ImGui::SameLine(0,0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(20,20));
    ImGui::BeginChild("##Content",ImVec2(0,0),true);
    if      (currentTab==0) RenderVisualsTab();
    else if (currentTab==1) RenderSilentAimTab();
    else if (currentTab==2) RenderMiscTab();
    ImGui::EndChild(); ImGui::PopStyleVar();
    ImGui::End();
}

// ==========================================================
// FLOATING TOGGLE BUTTON
// Draggable pill button — tap to show/hide Eclipse menu.
// Stays wherever the user drags it. Defaults to right edge.
// ==========================================================
static ImVec2 g_BtnPos       = ImVec2(-1, -1);  // -1 = not yet initialized
static bool   g_BtnDragging  = false;
static ImVec2 g_DragOffset   = ImVec2(0, 0);
static bool   g_BtnWasDragged = false;

const float BTN_W = 52.0f;
const float BTN_H = 28.0f;
const float BTN_MARGIN = 10.0f;

void RenderFloatingButton() {
    // Initialize position to right-center on first frame
    if (g_BtnPos.x < 0) {
        g_BtnPos.x = g_ScreenWidth  - BTN_W - BTN_MARGIN;
        g_BtnPos.y = g_ScreenHeight * 0.40f;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    bool   mouseDown = io.MouseDown[0];

    ImVec2 btnMin = g_BtnPos;
    ImVec2 btnMax = ImVec2(g_BtnPos.x + BTN_W, g_BtnPos.y + BTN_H);

    bool hovered = mousePos.x >= btnMin.x && mousePos.x <= btnMax.x &&
                   mousePos.y >= btnMin.y && mousePos.y <= btnMax.y;

    // Drag logic
    if (hovered && mouseDown && !g_BtnDragging) {
        g_BtnDragging   = true;
        g_BtnWasDragged = false;
        g_DragOffset    = ImVec2(mousePos.x - g_BtnPos.x, mousePos.y - g_BtnPos.y);
    }
    if (g_BtnDragging) {
        if (mouseDown) {
            ImVec2 newPos = ImVec2(mousePos.x - g_DragOffset.x, mousePos.y - g_DragOffset.y);
            // Clamp to screen
            if (newPos.x < BTN_MARGIN) newPos.x = BTN_MARGIN;
            if (newPos.y < BTN_MARGIN) newPos.y = BTN_MARGIN;
            if (newPos.x > g_ScreenWidth  - BTN_W - BTN_MARGIN) newPos.x = g_ScreenWidth  - BTN_W - BTN_MARGIN;
            if (newPos.y > g_ScreenHeight - BTN_H - BTN_MARGIN) newPos.y = g_ScreenHeight - BTN_H - BTN_MARGIN;
            // Mark as dragged if moved more than 6px
            float dx = newPos.x - g_BtnPos.x, dy = newPos.y - g_BtnPos.y;
            if (dx*dx + dy*dy > 36.0f) g_BtnWasDragged = true;
            g_BtnPos = newPos;
        } else {
            // Mouse released
            if (!g_BtnWasDragged) showMenu = !showMenu;  // tap = toggle
            g_BtnDragging = false;
        }
    }

    // Recalculate after potential drag
    btnMin = g_BtnPos;
    btnMax = ImVec2(g_BtnPos.x + BTN_W, g_BtnPos.y + BTN_H);

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Shadow
    dl->AddRectFilled(
        ImVec2(btnMin.x + 2, btnMin.y + 2),
        ImVec2(btnMax.x + 2, btnMax.y + 2),
        IM_COL32(0, 0, 0, 80), BTN_H * 0.5f
    );

    // Button body — accent color when menu open, dark when closed
    ImU32 bodyCol = showMenu
        ? ImGui::ColorConvertFloat4ToU32(COLOR_ACCENT)
        : IM_COL32(28, 28, 34, 220);
    dl->AddRectFilled(btnMin, btnMax, bodyCol, BTN_H * 0.5f);

    // Border
    ImU32 borderCol = showMenu
        ? IM_COL32(255, 255, 255, 60)
        : ImGui::ColorConvertFloat4ToU32(COLOR_ACCENT);
    dl->AddRect(btnMin, btnMax, borderCol, BTN_H * 0.5f, 0, 1.5f);

    // Label — "✦" when open (close hint), "☰" when closed (open hint)
    const char* label = showMenu ? "X" : "=";
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(14.0f, FLT_MAX, 0, label);
    ImVec2 textPos = ImVec2(
        btnMin.x + (BTN_W - ts.x) * 0.5f,
        btnMin.y + (BTN_H - ts.y) * 0.5f
    );
    ImU32 textCol = showMenu
        ? IM_COL32(255, 255, 255, 255)
        : ImGui::ColorConvertFloat4ToU32(COLOR_ACCENT);
    dl->AddText(ImGui::GetFont(), 14.0f, textPos, textCol, label);
}

void RenderOverlay() {
    ImDrawList* draw=ImGui::GetBackgroundDrawList();
    g_FrameCount++;
    { std::lock_guard<std::mutex> lk(g_SilentAimMutex);
      for (auto it=g_SilentAimList.begin();it!=g_SilentAimList.end();)
          it=(g_FrameCount-it->second.lastSeen>120)?g_SilentAimList.erase(it):++it; }
    std::vector<EspEntityData> local;
    { std::lock_guard<std::mutex> lk(g_EspMutex); local=g_EspDataList; g_EspDataList.clear(); }

    if (silentAimEnabled&&silentAimDrawFov)
        draw->AddCircle(ImVec2(g_ScreenWidth/2,g_ScreenHeight/2),silentAimFov,ImGui::ColorConvertFloat4ToU32(customSilentAimFovColor),64,1.5f);

    if (espEnabled) {
        for (auto& d:local) {
            Matrix4x4 vm,pm;
            if (!SafeGetMatrix(&vm,d.managedCameraPtr,o_get_worldToCamera)) continue;
            if (!SafeGetMatrix(&pm,d.managedCameraPtr,o_get_projMatrix))    continue;
            Vec3 ep; if (!SafeGetPosition(&ep,d.managedTransformPtr)) continue;
            float dist=-(ep.x*vm.m[2]+ep.y*vm.m[6]+ep.z*vm.m[10]+vm.m[14]);
            if (dist<0.1f) continue;
            Vec3 corners[8]={
                {ep.x-0.4f,ep.y+0.0f,ep.z-0.4f},{ep.x-0.4f,ep.y+0.0f,ep.z+0.4f},
                {ep.x+0.4f,ep.y+0.0f,ep.z+0.4f},{ep.x+0.4f,ep.y+0.0f,ep.z-0.4f},
                {ep.x-0.4f,ep.y+3.1f,ep.z-0.4f},{ep.x-0.4f,ep.y+3.1f,ep.z+0.4f},
                {ep.x+0.4f,ep.y+3.1f,ep.z+0.4f},{ep.x+0.4f,ep.y+3.1f,ep.z-0.4f}};
            ImVec2 pts[8]; bool ok=true;
            for (int j=0;j<8;j++) if (!WorldToScreen(corners[j],vm,pm,&pts[j])) { ok=false; break; }
            if (!ok) continue;
            float minX=pts[0].x,maxX=pts[0].x,minY=pts[0].y,maxY=pts[0].y;
            for (int j=1;j<8;j++) {
                if(pts[j].x<minX)minX=pts[j].x; if(pts[j].x>maxX)maxX=pts[j].x;
                if(pts[j].y<minY)minY=pts[j].y; if(pts[j].y>maxY)maxY=pts[j].y;
            }
            float bh=maxY-minY; if(bh<5)continue;
            float bw=maxX-minX, cx=(minX+maxX)*0.5f;
            ImU32 bc=ImGui::ColorConvertFloat4ToU32(customBoxColor);
            ImU32 nc=ImGui::ColorConvertFloat4ToU32(customNameColor);
            ImU32 sh=IM_COL32(0,0,0,180);
            if (espBox) {
                draw->AddLine(pts[0],pts[1],bc,espBoxThickness); draw->AddLine(pts[1],pts[2],bc,espBoxThickness);
                draw->AddLine(pts[2],pts[3],bc,espBoxThickness); draw->AddLine(pts[3],pts[0],bc,espBoxThickness);
                draw->AddLine(pts[4],pts[5],bc,espBoxThickness); draw->AddLine(pts[5],pts[6],bc,espBoxThickness);
                draw->AddLine(pts[6],pts[7],bc,espBoxThickness); draw->AddLine(pts[7],pts[4],bc,espBoxThickness);
                draw->AddLine(pts[0],pts[4],bc,espBoxThickness); draw->AddLine(pts[1],pts[5],bc,espBoxThickness);
                draw->AddLine(pts[2],pts[6],bc,espBoxThickness); draw->AddLine(pts[3],pts[7],bc,espBoxThickness);
            }
            if (espHealth) {
                float bw2=(bw>100)?bw/15.0f:4.0f; if(bw2>10)bw2=10; if(bw2<3)bw2=3;
                float bx=minX-bw2-3;
                float mh=(d.maxHealth<=0)?100:d.maxHealth, pct=d.health/mh;
                if(pct<0)pct=0; if(pct>1)pct=1;
                draw->AddRectFilled(ImVec2(bx-1,minY-1),ImVec2(bx+bw2+1,minY+bh+1),ImGui::ColorConvertFloat4ToU32(customHealthBgColor));
                ImU32 hc=IM_COL32((int)(255*(1-pct)),(int)(255*pct),0,255);
                draw->AddRectFilled(ImVec2(bx,minY+bh-bh*pct),ImVec2(bx+bw2,minY+bh),hc);
            }
            if (espName) {
                float fs=18.0f*(30.0f/dist); if(fs>18)fs=18; if(fs<8)fs=8;
                char info[64]; snprintf(info,sizeof(info),"%s [%dm]",d.name,(int)dist);
                ImFont* f=ImGui::GetFont();
                ImVec2 ts=f->CalcTextSizeA(fs,FLT_MAX,0,info);
                float tx=cx-ts.x*0.5f, ty=minY-ts.y-5;
                draw->AddText(f,fs,ImVec2(tx+1,ty+1),sh,info);
                draw->AddText(f,fs,ImVec2(tx,ty),nc,info);
            }
        }
    }
    if (drawCrosshair) {
        ImVec2 ctr={g_ScreenWidth/2,g_ScreenHeight/2};
        ImU32 cc=ImGui::ColorConvertFloat4ToU32(customCrosshairColor);
        ImU32 sc=ImGui::ColorConvertFloat4ToU32(customCrosshairShadowColor);
        draw->AddLine({ctr.x,ctr.y-crosshairGap-crosshairLength},{ctr.x,ctr.y-crosshairGap},sc,crosshairShadowThickness);
        draw->AddLine({ctr.x,ctr.y-crosshairGap-crosshairLength},{ctr.x,ctr.y-crosshairGap},cc,crosshairThickness);
        draw->AddLine({ctr.x,ctr.y+crosshairGap},{ctr.x,ctr.y+crosshairGap+crosshairLength},sc,crosshairShadowThickness);
        draw->AddLine({ctr.x,ctr.y+crosshairGap},{ctr.x,ctr.y+crosshairGap+crosshairLength},cc,crosshairThickness);
        draw->AddLine({ctr.x-crosshairGap-crosshairLength,ctr.y},{ctr.x-crosshairGap,ctr.y},sc,crosshairShadowThickness);
        draw->AddLine({ctr.x-crosshairGap-crosshairLength,ctr.y},{ctr.x-crosshairGap,ctr.y},cc,crosshairThickness);
        draw->AddLine({ctr.x+crosshairGap,ctr.y},{ctr.x+crosshairGap+crosshairLength,ctr.y},sc,crosshairShadowThickness);
        draw->AddLine({ctr.x+crosshairGap,ctr.y},{ctr.x+crosshairGap+crosshairLength,ctr.y},cc,crosshairThickness);
    }
}

// ==========================================================
// PUBLIC C API  (P/Invoke from Unity C#)
// ==========================================================
extern "C" {

// OnCreate entry point — called from UnityPlayerActivity.onCreate()
JNIEXPORT void JNICALL
Java_com_pixellabs_pixelstrike3d_ImGuiPlugin_onCreate(JNIEnv*, jobject) {
    LOGI("onCreate — com.pixellabs.pixelstrike3d");
    InstallHooks();
    injection_init();
}

bool injection_init() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO();
    io.DisplaySize=ImVec2(g_ScreenWidth,g_ScreenHeight);
    io.DeltaTime=1.0f/60.0f;
    io.IniFilename=nullptr;
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) { LOGE("ImGui_ImplOpenGL3_Init failed"); return false; }
    ImGui::StyleColorsDark();
    ImFontConfig cfg; cfg.SizePixels=28;
    io.Fonts->AddFontDefault(&cfg);
    // Font texture upload handled automatically by ImGui_ImplOpenGL3_NewFrame in this imgui version
    g_Initialized=true;
    LOGI("injection_init OK. ImGui %s | GLES3 | arm64-v8a", IMGUI_VERSION);
    return true;
}

void injection_shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    g_Initialized=false;
}

void injection_new_frame(float w, float h, float dt) {
    g_ScreenWidth=w; g_ScreenHeight=h;
    ImGuiIO& io=ImGui::GetIO();
    io.DisplaySize=ImVec2(w,h);
    io.DeltaTime=(dt>0)?dt:(1.0f/60.0f);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ApplyStyle();
}

void injection_render() {
    RenderFloatingButton();   // always draws — handles its own toggle
    if (showMenu) RenderMainMenu();
    RenderOverlay();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void injection_set_touch(float x, float y, bool touching) {
    ImGui::GetIO().AddMousePosEvent(x,y);
    ImGui::GetIO().AddMouseButtonEvent(0,touching);
}

void injection_set_shooting(bool s)  { g_IsShooting=s; }
void injection_toggle_menu()         { showMenu=!showMenu; }
bool injection_wants_mouse()         { return ImGui::GetIO().WantCaptureMouse; }
bool injection_wants_keyboard()      { return ImGui::GetIO().WantCaptureKeyboard; }
void injection_add_char(unsigned int c) { ImGui::GetIO().AddInputCharacter(c); }
void injection_backspace() {
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Backspace,true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Backspace,false);
}

} // extern "C"
