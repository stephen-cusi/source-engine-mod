/*
** Garry's Mod platform layer: CLuaInterface / ILuaBase ABI and the binary
** module loader.
**
** This file gives the stock LuaJIT VM the object layout and the virtual
** tables the reference engine exposes to binary modules.
** A module does  L->luabase  to reach its ILuaBase, casts it to
** GarrysMod::Lua::ILuaBase* and calls fixed vtable slots.  The slot order is
** the ABI, so every method below is emitted in the shipping order even when
** the body is only a safe stand-in.
**
** Behaviour follows the reference implementation: each method mirrors what
** the reference build does for that slot (the reference is a thin wrapper
** over the Lua JIT C API for most of ILuaBase).  Slots that need the host
** game (console/convar/command creation, std::string sinks) are implemented
** as safe stand-ins that report once and return a benign default; none is a
** null pointer, so a module that calls one cannot crash the VM.
**
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#define glua_stricmp _stricmp
#else
#include <dlfcn.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#define glua_stricmp strcasecmp
#endif

#define lib_gmod_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"

/* ------------------------------------------------------------------------ */
/* GMod scalar value layout (GarrysMod::Lua exposes these as 3 floats).     */

typedef struct GLuaVector { float x, y, z; } GLuaVector;
typedef struct GLuaAngle  { float x, y, z; } GLuaAngle;
typedef struct GLuaColor  { unsigned char r, g, b, a; } GLuaColor;

/* UserData prefix the reference writes for every typed userdata:
**   +0  void*  data      (payload pointer)
**   +8  u8     type      (GarrysMod::Lua::Type byte)
*/
typedef struct GLuaUserData {
  void *data;
  unsigned char type;
} GLuaUserData;

/* Realm values (GarrysMod::Lua::State). */
#define GLUA_REALM_CLIENT 0
#define GLUA_REALM_SERVER 1
#define GLUA_REALM_MENU   2

/* Host calls waiting for the next frame. */
typedef struct GLuaThreadedNode {
  void *call;
  struct GLuaThreadedNode *next;
} GLuaThreadedNode;

/* GarrysMod::Lua::Type numeric values used directly below. */
#define GT_NONE          (-1)
#define GT_NIL           0
#define GT_BOOL          1
#define GT_LIGHTUSERDATA 2
#define GT_NUMBER        3
#define GT_STRING        4
#define GT_TABLE         5
#define GT_FUNCTION      6
#define GT_USERDATA      7
#define GT_THREAD        8
#define GT_VECTOR        10
#define GT_ANGLE         11
#define GT_TYPE_COUNT    44

struct CLuaInterface;

/* ------------------------------------------------------------------------ */
/* ILuaObject: reference-counted handle to a Lua value.  Our concrete       */
/* object stores a registry reference; the vtable follows the published     */
/* GarrysMod::Lua::ILuaObject declaration order so module calls land on the */
/* right slot.                                                              */

typedef struct GLuaObject {
  const void *vtable;
  struct CLuaInterface *iface;
  int ref;                              /* LUA_NOREF when unbound */
} GLuaObject;

/* ------------------------------------------------------------------------ */
/* ILuaGameCallback default implementation.  The reference forwards console */
/* output and object creation to the host game; without a host we route the */
/* text to stdout and allocate our own object.                              */

typedef struct GLuaGameCallback {
  const void *vtable;
} GLuaGameCallback;

/* ------------------------------------------------------------------------ */
/* CLuaInterface.  Offset 0 is the vtable, offset 8 is the lua_State* that   */
/* the reference keeps as ILuaBase's private state member.  The remaining   */
/* fields are ours and are only reached from this translation unit.         */

typedef struct CLuaReturn {
  GLuaObject *objs[4];
} GLuaReturn;

typedef struct CLuaInterface {
  const void *vtable;                   /* +0  */
  lua_State *state;                     /* +8  (ILuaBase::state) */

  unsigned char realm;                  /* +0x10 realm byte */
  GLuaGameCallback callback;            /* borrowed default callback */
  void *gameCallback;                   /* engine ILuaGameCallback from Init */
  char pathID[32];                      /* +0x18 path id string */
  #define GLUA_PATH_DEPTH 64
char pathStack[GLUA_PATH_DEPTH][260];  /* include dir stack (GetPath/PushPath) */
  int pushedPaths;                      /* path stack depth */
  char fileStack[GLUA_PATH_DEPTH][260];  /* currently-running file stack */
  int pushedFiles;                      /* file stack depth */

  GLuaReturn returns;                   /* GetReturn slots */
  int tempIndex;                        /* NewTemporaryObject ring cursor */
  GLuaObject *temp[32];                 /* NewTemporaryObject ring */

  GLuaObject *global;                   /* Global() table object (+0x1a0) */
  GLuaObject *pooled;                   /* pooled-string object  (+0x1a8) */
  int metaIDCounter;                    /* first free type id    (+0x1b0) */
  GLuaObject *metaTables[255];          /* per-type metatable objects (+0x1b8) */
  unsigned char metaTableBase[255]; /* 1 once the canonical metatable took the id */

  /* Pending host calls.  Each entry is a host object whose method table
  ** answers "should this run now" and "run it"; Cycle drains the queue once a
  ** frame. */
  GLuaThreadedNode *threadedHead;
  GLuaThreadedNode *threadedTail;
  int threadedCount;

  int colorMetaID;                      /* metatable id for GLuaColor */
  int savedMetas;                       /* guard for repeated Init */
  int errorFuncRef;                     /* registry ref of the error handler */
} CLuaInterface;

typedef void (*glua_vfn)(void);
typedef int (*GLuaCFunc)(lua_State *L);

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */

#define GLUA_IFACE(self)   ((self)->state)

/* Crash forensics: append a line to a trace file.  Used while bringing the
** VM up inside a real engine, where a bad object pointer otherwise turns
** into a silent access violation.  Enabled only when OPGLUA_TRACE is defined;
** it writes to a fixed path and is a bring-up aid, not a shipping feature. */
#if defined(_WIN32) && defined(OPGLUA_TRACE)
static void glua_tracef(const char *tag, void *a, void *b, void *c)
{
  static FILE *f = NULL;
  static int tried = 0;
  if (!tried) {
    tried = 1;
    f = fopen("D:\\tmp\\glua\\opglua_trace.txt", "a");
  }
  if (f != NULL) {
    fprintf(f, "%s a=%p b=%p c=%p\n", tag, a, b, c);
    fflush(f);
  }
}
#else
#define glua_tracef(t,a,b,c) ((void)0)
#endif

static const char *glua_type_name(int type);

/* The interface vtable, defined near the end; referenced by gobj_push for the
** foreign-object guard. */
extern const glua_vfn glua_interface_vtable[132];

/* GetType semantics: stock Lua types pass through, typed userdata report
** their stored type byte when it is a GMod type (>= 9). */
static int glua_gettype(lua_State *L, int idx)
{
  int t = lua_type(L, idx);
  /* An absent index yields LUA_TNONE (-1); the reference reports that as 0
  ** (Nil).  The engine relies on this: e.g. AddCSLuaFile's no-argument guard
  ** is GetType(1) == 0. */
  if (t < 0) return 0;
  if (t == GT_USERDATA) {
    GLuaUserData *ud = (GLuaUserData *)lua_touserdata(L, idx);
    if (ud != NULL && ud->type >= 9)
      return ud->type;
    return GT_USERDATA;
  }
  return t;
}

/* The reference name table (44 entries) plus the 'none'/'unknown' bounds. */
static const char *glua_typename_table[GT_TYPE_COUNT] = {
  "nil", "bool", "lightuserdata", "number", "string", "table", "function",
  "userdata", "thread", "entity", "vector", "angle", "physobj", "save",
  "restore", "damageinfo", "effectdata", "movedata", "recipientfilter",
  "usercmd", "vehicle", "material", "panel", "particle", "particleemitter",
  "texture", "usermsg", "convar", "mesh", "matrix", "sound",
  "pixelvishandle", "dlight", "video", "file", "locomotion", "path",
  "navarea", "soundhandle", "navladder", "particlesystem",
  "projectedtexture", "physcollide", "surfaceinfo"
};

static const char *glua_type_name(int type)
{
  if (type < 0) return "none";
  if (type >= GT_TYPE_COUNT) return "unknown";
  return glua_typename_table[type];
}

/* Script-callback pooled strings.  Numeric ids passed across the ABI map to
** these names; the table order is the reference order (index 196 is
** FireAnimationEvent).  Values are read-only. */
static const char *glua_pooled_strings[] = {
    "AcceptInput", "AdjustMouseSensitivity", "AllowPlayerPickup", "CalcMainActivity",
    "CalcView", "CalcViewModelView", "CanExitVehicle", "CanPlayerEnterVehicle",
    "CanPlayerSuicide", "ChatText", "ChatTextChanged", "CheckPassword",
    "ClientSignOnStateChanged", "CreateClientsideRagdoll", "CreateEntityRagdoll", "CreateMove",
    "DoAnimationEvent", "DoPlayerDeath", "DrawMonitors", "DrawPhysgunBeam",
    "EndEntityDriving", "EntityEmitSound", "EntityFireBullets", "PostEntityFireBullets",
    "EntityKeyValue", "EntityNetworkedVarChanged", "EntityRemoved", "EntityTakeDamage",
    "PostEntityTakeDamage", "FindUseEntity", "FinishChat", "FinishMove",
    "GUIMouseDoublePressed", "GUIMousePressed", "GUIMouseReleased", "GetFallDamage",
    "GetGameDescription", "GetMotionBlurValues", "GetPreferredCarryAngles", "GetTeamColor",
    "GravGunOnDropped", "GravGunOnPickedUp", "GravGunPickupAllowed", "GravGunPunt",
    "HUDAmmoPickedUp", "HUDDrawScoreBoard", "HUDItemPickedUp", "HUDPaint",
    "HUDPaintBackground", "DrawOverlay", "CaptureVideo", "HUDShouldDraw",
    "HUDWeaponPickedUp", "InitPostEntity", "InputMouseApply", "KeyPress",
    "KeyRelease", "Move", "NeedsDepthPass", "NetworkEntityCreated",
    "NetworkIDValidated", "NotifyShouldTransmit", "OnAchievementAchieved", "OnChatTab",
    "OnCrazyPhysics", "OnDamagedByExplosion", "OnEntityCreated", "OnEntityWaterLevelChanged",
    "OnNPCKilled", "OnNPCDropItem", "OnPhysgunFreeze", "OnPhysgunReload",
    "OnPlayerChat", "OnPlayerHitGround", "PlayerChangedTeam", "OnViewModelChanged",
    "PhysgunDrop", "PhysgunPickup", "OnPhysgunPickup", "PlayerAuthed",
    "PlayerBindPress", "PlayerButtonDown", "PlayerButtonUp", "PlayerCanHearPlayersVoice",
    "PlayerCanPickupItem", "PlayerCanPickupWeapon", "PlayerCanSeePlayersChat", "PlayerConnect",
    "PlayerDeath", "PlayerDeathSound", "PlayerDeathThink", "PlayerDroppedWeapon",
    "PlayerDisconnected", "PlayerDriveAnimate", "PlayerEndVoice", "PlayerEnteredVehicle",
    "PlayerFootstep", "PlayerHurt", "PlayerInitialSpawn", "PlayerLeaveVehicle",
    "PlayerNoClip", "PlayerPostThink", "PlayerSay", "PlayerSelectSpawn",
    "PlayerShouldTakeDamage", "PlayerShouldTaunt", "PlayerSilentDeath", "PlayerSpawn",
    "PlayerSpray", "PlayerStartTaunt", "PlayerStartVoice", "PlayerStepSoundTime",
    "PlayerSwitchFlashlight", "PlayerSwitchWeapon", "PlayerTick", "PlayerTraceAttack",
    "PlayerUse", "PostCleanupMap", "PostDraw2DSkyBox", "PostDrawEffects",
    "PostDrawHUD", "PostDrawOpaqueRenderables", "PostDrawSkyBox", "PostDrawTranslucentRenderables",
    "PostDrawViewModel", "PostPlayerDeath", "PostPlayerDraw", "PostRender",
    "PostRenderVGUI", "PreCleanupMap", "PreDrawEffects", "PreDrawHUD",
    "PreDrawOpaqueRenderables", "PreDrawSkyBox", "PreDrawTranslucentRenderables", "PreDrawViewModel",
    "PreDrawViewModels", "PrePlayerDraw", "PreRender", "PreventScreenClicks",
    "PropBreak", "RenderScene", "RenderScreenspaceEffects", "Restored",
    "Saved", "ScaleNPCDamage", "ScalePlayerDamage", "ScoreboardHide",
    "ScoreboardShow", "SetupMove", "SetupPlayerVisibility", "SetupSkyboxFog",
    "SetupWorldFog", "ShouldCollide", "ShouldDrawLocalPlayer", "ShowHelp",
    "ShowSpare1", "ShowSpare2", "ShowTeam", "ShutDown",
    "SpawniconGenerated", "StartChat", "StartCommand", "StartEntityDriving",
    "Think", "Tick", "TranslateActivity", "UpdateAnimation",
    "VGUIMousePressAllowed", "VGUIMousePressed", "VehicleMove", "ViewPunch",
    "WeaponEquip", "PlayerAmmoChanged", "PlayerClassChanged", "PlayerHandleAnimEvent",
    "PlayerFireAnimationEvent", "OnPlayerPhysicsPickup", "OnPlayerPhysicsDrop", "OnPlayerJump",
    "OnCloseCaptionEmit", "HandlePlayerArmorReduction", "OnPauseMenuShow", "OnClientLuaError",
    "OnChangeActivity", "BehaveUpdate", "Blocked", "BodyUpdate",
    "Deploy", "DoImpactEffect", "Draw", "DrawTranslucent",
    "EndTouch", "Equip", "EquipAmmo", "ExpressionFinished",
    "FireAnimationEvent", "FreezeMovement", "GetAttackSpread", "GetCapabilities",
    "GetRelationship", "GetRenderMesh", "HandleAnimEvent", "Holster",
    "ImpactTrace", "KeyValue", "OnCondition", "OnContact",
    "OnIgnite", "OnInjured", "OnKilled", "OnTraceAttack",
    "OnLandOnGround", "OnLeaveGround", "OnNavAreaChanged", "OnOtherKilled",
    "OnStuck", "OnTakeDamage", "OnUnStuck", "PassesDamageFilter",
    "PassesFilter", "PassesTriggerFilters", "PhysicsCollide", "PhysicsSimulate",
    "PhysicsUpdate", "RunEngineTask", "StartEngineSchedule", "StartEngineTask",
    "StartTouch", "TestCollision", "Touch", "UpdateTransmitState",
    "Use", "IsJumpLegal", "CalcWeaponProficiency", "GetShadowCastDirection",
    "OnEntitySight", "OnEntitySightLost", "OnMovementFailed", "OnMovementComplete",
    "OnActiveWeaponChanged", "RunAI", "OnTaskFailed", "OverrideMove",
    "OverrideMoveFacing", "TranslateSchedule", "TranslateNavGoal", "OnStateChange",
    "CustomAmmoDisplay", "ResolveCustomFlyCollision", "DrawHUD", "DrawHUDBackground",
    "DrawWorldModel", "DrawWorldModelTranslucent", "DrawWeaponSelection", "DoDrawCrosshair",
    "ShouldDrawViewModel", "GetTracerOrigin", "OnDrop", "OnRemove",
    "PrimaryAttack", "SecondaryAttack", "Reload", "RenderScreen",
    "ViewModelDrawn", "TranslateFOV", "SetWeaponHoldType", "CanBePickedUpByNPCs",
    "NPCShoot_Primary", "NPCShoot_Secondary", "GetNPCRestTimes", "GetNPCBurstSettings",
    "GetNPCBulletSpread", "ShouldDropOnDie", "ActionSignal", "AllowInput",
    "ApplySchemeSettings", "ConsoleMessage", "DoClick", "OnActivate",
    "OnBeginLoadingDocument", "OnCallback", "OnChangeAddressBar", "OnChangeTargetURL",
    "OnChangeTitle", "OnChildViewCreated", "OnCursorEntered", "OnCursorExited",
    "OnCursorMoved", "OnDeactivate", "OnDeletion", "OnDocumentReady",
    "OnFinishLoadingDocument", "OnFocusChanged", "OnGetFocus", "OnKeyCodePressed",
    "OnKeyCodeReleased", "OnKeyCodeTyped", "OnLoseFocus", "OnMousePressed",
    "OnMouseReleased", "OnMouseWheeled", "OnScreenSizeChanged", "OnSizeChanged",
    "OnTextChanged", "OnTextClicked", "PerformLayout", "ResourceLoaded",
    "TestHover", "DTVar_ReceiveProxyGL", "Float", "Int",
    "Bool", "Vector", "Angle", "String",
    "Entity"
};

#define GLUA_POOLED_COUNT ((int)(sizeof(glua_pooled_strings)/sizeof(glua_pooled_strings[0])))

/* ------------------------------------------------------------------------ */
/* Default game callback (stdout sink).                                     */

static GLuaObject *glua_object_alloc(CLuaInterface *self);

static GLuaObject *gc_CreateLuaObject(GLuaGameCallback *cb)
{
  CLuaInterface *self = (CLuaInterface *)((char *)cb - offsetof(CLuaInterface, callback));
  return glua_object_alloc(self);
}

static void gc_DestroyLuaObject(GLuaGameCallback *cb, GLuaObject *obj)
{
  (void)cb; (void)obj;                    /* freed by the object vtable */
}

/* Report through the HOST callback, not this process's stdout: the engine owns
** the console and the error queue, and anything written elsewhere is invisible
** to the player and to console.log.  A host callback that leaves a slot NULL
** falls back to the default sink below, which is only used by standalone runs.
**
** Callback slots: 2 ErrorPrint(text, bool), 3 Msg(text, bool),
** 4 MsgColour(text, Color).
*/
enum { CB_SLOT_ERRORPRINT = 2, CB_SLOT_MSG = 3, CB_SLOT_MSGCOLOUR = 4 };

static void glua_report(CLuaInterface *self, const char *text, int err)
{
  void *cb;
  if (self == NULL || text == NULL) return;
  cb = self->gameCallback;
  if (cb != NULL) {
    void **vt = *(void ***)cb;
    int slot = err ? CB_SLOT_ERRORPRINT : CB_SLOT_MSG;
    if (vt != NULL && vt[slot] != NULL) {
      ((void (*)(void *, const char *, int))vt[slot])(cb, text, 1);
      return;
    }
  }
  fputs(text, err ? stderr : stdout);
  fflush(err ? stderr : stdout);
}

static void glua_report_colour(CLuaInterface *self, const char *text, const void *color)
{
  void *cb;
  if (self == NULL || text == NULL) return;
  cb = self->gameCallback;
  if (cb != NULL) {
    void **vt = *(void ***)cb;
    if (vt != NULL && vt[CB_SLOT_MSGCOLOUR] != NULL) {
      ((void (*)(void *, const char *, const void *))vt[CB_SLOT_MSGCOLOUR])(cb, text, color);
      return;
    }
  }
  fputs(text, stdout);
  fflush(stdout);
}

static void gc_ErrorPrint(GLuaGameCallback *cb, const char *error, bool print)
{
  (void)cb;
  if (print) { fputs(error, stderr); fflush(stderr); }
}

static void gc_Msg(GLuaGameCallback *cb, const char *msg, bool useless)
{
  (void)cb; (void)useless;
  fputs(msg, stdout); fflush(stdout);
}

static void gc_MsgColour(GLuaGameCallback *cb, const char *msg, const void *color)
{
  (void)cb; (void)color;
  fputs(msg, stdout); fflush(stdout);
}

static void gc_LuaError(GLuaGameCallback *cb, const void *err) { (void)cb; (void)err; }
static void gc_InterfaceCreated(GLuaGameCallback *cb, void *iface) { (void)cb; (void)iface; }

static const glua_vfn glua_callback_vtable[7] = {
  (glua_vfn)gc_CreateLuaObject,
  (glua_vfn)gc_DestroyLuaObject,
  (glua_vfn)gc_ErrorPrint,
  (glua_vfn)gc_Msg,
  (glua_vfn)gc_MsgColour,
  (glua_vfn)gc_LuaError,
  (glua_vfn)gc_InterfaceCreated
};

/* ------------------------------------------------------------------------ */
/* ILuaObject implementation.  A bound object owns a registry reference.    */

static void gobj_bind_stack(CLuaInterface *self, GLuaObject *obj, int stackIndex)
{
  lua_State *L = self->state;
  lua_pushvalue(L, stackIndex);
  if (obj->ref != LUA_NOREF && obj->ref != LUA_REFNIL)
    luaL_unref(L, LUA_REGISTRYINDEX, obj->ref);
  obj->ref = luaL_ref(L, LUA_REGISTRYINDEX);
  obj->iface = self;
}

/* Our ILuaObject and ILuaInterface vtables; defined lower down.  Comparing a
** caller-supplied object's vtable pointer against the object vtable tells us
** whether the object is one of ours, without touching any further field.  The
** reference ILuaObject is a pure interface (its callers invoke its virtual
** methods and never read its fields), so an object handed to our slots is not
** guaranteed to share our layout -- only the leading vtable pointer is safe to
** read. */
extern const glua_vfn glua_object_vtable[102];
extern const glua_vfn glua_interface_vtable[132];

/* Optional diagnostics: -opglua-log 1 enables lifecycle messages and errors;
** -opglua-log 2 adds per-call tracing. No option means no log files or marker
** writes. -opglua-logfile overrides the default beside the host executable. */

#if defined(OPGLUA_LOG)
enum { GLUA_LOG_OFF = 0, GLUA_LOG_LIFECYCLE = 1, GLUA_LOG_VERBOSE = 2 };
static wchar_t glua_log_path[32768];

static int glua_log_level(void)
{
  static int state = -1;
  if (state < 0) {
    int argc = 0, i;
    LPWSTR *argv;
    state = GLUA_LOG_OFF;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != NULL) {
      for (i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"-opglua-log") == 0) {
          state = GLUA_LOG_LIFECYCLE;
          if (i + 1 < argc && argv[i + 1][0] != L'-' && argv[i + 1][0] != L'+') {
            const wchar_t *value = argv[++i];
            if (wcscmp(value, L"2") == 0 || _wcsicmp(value, L"verbose") == 0)
              state = GLUA_LOG_VERBOSE;
            else if (wcscmp(value, L"1") != 0 && _wcsicmp(value, L"lifecycle") != 0)
              state = GLUA_LOG_OFF;
          }
        } else if (_wcsicmp(argv[i], L"-opglua-logfile") == 0 && i + 1 < argc) {
          wcsncpy(glua_log_path, argv[++i], 32767);
          glua_log_path[32767] = L'\0';
        }
      }
      LocalFree(argv);
    }
    if (state != GLUA_LOG_OFF && glua_log_path[0] == L'\0') {
      wchar_t *sep;
      DWORD n = GetModuleFileNameW(NULL, glua_log_path, 32768);
      if (n == 0 || n >= 32768) glua_log_path[0] = L'\0';
      sep = wcsrchr(glua_log_path, L'\\');
      if (sep != NULL) sep[1] = L'\0';
      else glua_log_path[0] = L'\0';
      if (wcslen(glua_log_path) + 11 < 32768) wcscat(glua_log_path, L"opglua.log");
    }
  }
  return state;
}

static int glua_log_enabled(void) { return glua_log_level() != GLUA_LOG_OFF; }

static FILE *glua_log_stream(void)
{
  static FILE *f = NULL;
  static int tried = 0;
  if (!glua_log_enabled()) return NULL;
  if (!tried) { tried = 1; f = _wfopen(glua_log_path, L"a"); }
  return f;
}

/* Session markers share the optional log and never write when it is disabled. */
static void glua_boot_marker(const char *what)
{
  FILE *f;
  char self[512];
  char host[512];
  DWORD n;
  /* Keyed by the event name, so the attach line and the later lifecycle lines
  ** all appear instead of one suppressing the rest. */
  static char seen[8][24];
  static int seenCount = 0;
  int i;
  const char *tag = what ? what : "(null)";
  if (!glua_log_enabled()) return;
  for (i = 0; i < seenCount; i++) {
    if (strncmp(seen[i], tag, sizeof(seen[0]) - 1) == 0) return;
  }
  if (seenCount < 8) {
    snprintf(seen[seenCount], sizeof(seen[0]), "%s", tag);
    seenCount++;
  }
  self[0] = '\0';
  host[0] = '\0';
  n = GetModuleFileNameA(NULL, host, (DWORD)sizeof(host));
  if (n == 0 || n >= sizeof(host)) host[0] = '\0';
  {
    HMODULE mod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(const void *)glua_boot_marker, &mod)) {
      n = GetModuleFileNameA(mod, self, (DWORD)sizeof(self));
      if (n == 0 || n >= sizeof(self)) self[0] = '\0';
    }
  }
  f = glua_log_stream();
  if (f == NULL) return;
  fprintf(f, "event pid=%lu what=%s log=%s module=%s host=%s\n",
          (unsigned long)GetCurrentProcessId(), tag,
          (glua_log_level() == GLUA_LOG_VERBOSE) ? "verbose"
            : (glua_log_level() == GLUA_LOG_LIFECYCLE) ? "lifecycle" : "off",
          self, host);
  fflush(f);
}

static void glua_log(const char *fmt, ...)
{
  static unsigned int lines = 0;
  FILE *f;
  va_list ap;
  if (!glua_log_enabled()) return;
  f = glua_log_stream();
  if (f == NULL) return;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  if ((++lines & 15u) == 0) fflush(f);
}

/* Per-call trace.  Only in verbose mode. */
static void glua_log_v(const char *fmt, ...)
{
  FILE *f;
  va_list ap;
  if (glua_log_level() != GLUA_LOG_VERBOSE) return;
  f = glua_log_stream();
  if (f == NULL) return;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
}

/* Lua state allocator.  Every script allocation goes through here so the live
** and peak byte counts are known; the host asks for them when it reports
** memory, and the heartbeat prints them so a runaway script heap is visible
** without attaching anything. */
static size_t g_luaMemLive = 0;
static size_t g_luaMemPeak = 0;

static void *glua_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
  (void)ud;
  if (nsize == 0) {
    if (ptr != NULL) {
      g_luaMemLive = (g_luaMemLive >= osize) ? g_luaMemLive - osize : 0;
      free(ptr);
    }
    return NULL;
  }
  if (ptr == NULL) {
    void *p = malloc(nsize);
    if (p != NULL) {
      g_luaMemLive += nsize;
      if (g_luaMemLive > g_luaMemPeak) g_luaMemPeak = g_luaMemLive;
    }
    return p;
  }
  {
    void *p = realloc(ptr, nsize);
    if (p != NULL) {
      if (nsize > osize) {
        g_luaMemLive += nsize - osize;
        if (g_luaMemLive > g_luaMemPeak) g_luaMemPeak = g_luaMemLive;
      } else {
        g_luaMemLive -= (osize - nsize);
      }
    }
    return p;
  }
}

/* Memory heartbeat: written at most once every few seconds from the per-call
** path, so a runaway heap shows up in the lifecycle log without any per-call
** disk traffic. */
/* Lua is single threaded: the host creates the state on the main thread only,
** so a call from anywhere else means a script is being driven by a worker and
** the reference refuses to run it silently.  Report it once and carry on. */
static unsigned long glua_mainThreadId = 0;
static int glua_threadWarned = 0;

static int glua_onMainThread(void)
{
  if (glua_mainThreadId == 0) return 1;
  if (GetCurrentThreadId() == glua_mainThreadId) return 1;
  if (!glua_threadWarned) {
    glua_threadWarned = 1;
    glua_log("WARN calling Lua from a thread other than the main one");
  }
  return 0;
}

static void glua_log_heartbeat(void)
{
  static unsigned long long last = 0;
  unsigned long long now;
  FILE *f;
  if (!glua_log_enabled()) return;
  now = GetTickCount64();
  if (last != 0 && now - last < 5000ull) return;
  last = now;
  f = glua_log_stream();
  if (f == NULL) return;
  {
    /* Working set through the kernel's process query, resolved lazily so the
    ** module keeps no link dependency on the process helpers. */
    typedef struct {
      unsigned long cb;
      unsigned long cPageFault, cPeakPageFault, pagefileUsage, peakPagefile;
      unsigned long long quotaNP, quotaP, quotaPPeak;
      unsigned long long peakWS, ws, peakCopy, copy, peakPaged, paged,
                     peakNonPaged, nonPaged, pagefile, peakPagefile2;
      unsigned long long privateUsage;
    } memcounters;
    typedef long (WINAPI *getmem_fn)(void *, void *, unsigned long);
    static getmem_fn getmem = NULL;
    static int tried = 0;
    unsigned long long wsMb = 0;
    if (!tried) {
      tried = 1;
      getmem = (getmem_fn)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                         "K32GetProcessMemoryInfo");
    }
    if (getmem != NULL) {
      memcounters mc;
      memset(&mc, 0, sizeof(mc));
      mc.cb = sizeof(mc);
      if (getmem(GetCurrentProcess(), &mc, (unsigned long)sizeof(mc)))
        wsMb = mc.ws >> 20;
    }
    fprintf(f, "heartbeat t=%llus working_set_mb=%llu lua_mb=%llu lua_peak_mb=%llu\n",
            (unsigned long long)(now / 1000ull), wsMb,
            (unsigned long long)(g_luaMemLive >> 20), (unsigned long long)(g_luaMemPeak >> 20));
    fflush(f);
  }
}

/* Implemented in lib_luashared.cpp: std::string-typed ILuaShared tail slots
** and the LuaFile the engine's gamemode existence check consumes. */
extern void *opglua_luashared_loadfile(const void *path, const void *pathId,
                                       bool fromDatatable, bool fromFile);
extern bool opglua_luashared_scriptexists(const void *file, const void *path, bool unknown);
extern void opglua_luashared_findscripts(const void *path, const void *pathID, void *outVector);
extern void *opglua_luashared_getcache(const void *fileName);

extern void opglua_luashared_emptycache(void);
extern void opglua_luashared_invalidatecache(const void *name);
extern void opglua_luashared_mount_add(const char *root, const char *pathID);
extern void opglua_luashared_cache_stats(int *files, int *hits, int *failed,
                                         int *successful, int *disk);

static void glua_log_flush(void)
{
  FILE *f;
  if (!glua_log_enabled()) return;
  f = glua_log_stream();
  if (f != NULL) fflush(f);
}
#else
#define glua_log(...) ((void)0)
#define glua_log_v(...) ((void)0)
#define glua_log_heartbeat() ((void)0)
#define glua_log_flush() ((void)0)
#define glua_boot_marker(n) ((void)0)
#endif

/* ILuaObject instances belong to the HOST.  The engine's ILuaGameCallback
** creates them and the reference operates on them ONLY through the object's
** virtual methods (never by reading fields).  These helpers call those
** methods by the reference object-vtable ABI byte offsets:
**   +0x08 SetFromStack(int)   +0x10 UnReference()
**   +0x108 Push()             +0x118 isTable()
*/
#define GLUA_OBJ_VT(o)          (*(void ***)(o))
#define GLUA_OBJ_SETSTACK(o,i)  ((void (*)(void *,int))GLUA_OBJ_VT(o)[0x08/8])((o),(int)(i))
#define GLUA_OBJ_UNREF(o)       ((void (*)(void*))GLUA_OBJ_VT(o)[0x10/8])(o)
#define GLUA_OBJ_PUSH(o)        ((void (*)(void*))GLUA_OBJ_VT(o)[0x108/8])(o)
#define GLUA_OBJ_ISTABLE(o)     (((char (*)(void*))GLUA_OBJ_VT(o)[0x118/8])(o) != 0)

/* An object is OURS only when its vtable is our object vtable.  Any other
** object was handed to us by the host (the engine's game callback) and may
** only be driven through its virtual methods -- reading our fields on it, or
** freeing it, would corrupt the host heap. */
#define GLUA_OBJ_IS_OURS(o) \
  ((o) != NULL && *(const void **)(o) == (const void *)glua_object_vtable)

static void gobj_push(GLuaObject *obj);
static void gobj_UnReference(GLuaObject *obj);
static int gobj_GetType(GLuaObject *obj);

/* The engine's ILuaGameCallback (stored by Init slot 55): object factory. */
static void *gobj_create(CLuaInterface *self)
{
  if (self->gameCallback == NULL) return glua_object_alloc(self);
  return ((void *(*)(void*))GLUA_OBJ_VT(self->gameCallback)[0])(self->gameCallback);
}
static void gobj_destroy(CLuaInterface *self, void *obj)
{
  if (obj == NULL) return;
  if (self->gameCallback == NULL) { gobj_UnReference((GLuaObject *)obj); free(obj); return; }
  ((void (*)(void*,void*))GLUA_OBJ_VT(self->gameCallback)[1])(self->gameCallback, obj);
}
static void gobj_push_obj(CLuaInterface *self, void *obj)
{
  if (obj == NULL) { lua_pushnil(self->state); return; }
  if (GLUA_OBJ_IS_OURS(obj)) { gobj_push((GLuaObject *)obj); return; }
  ((void (*)(void*))GLUA_OBJ_VT(obj)[0x108/8])(obj);
}

/* Dispatch helpers that work for both our objects and host objects. */
static void gobj_setstack_any(CLuaInterface *self, void *o, int i)
{
  if (o == NULL) return;
  if (GLUA_OBJ_IS_OURS(o)) gobj_bind_stack(self, (GLuaObject *)o, i);
  else GLUA_OBJ_SETSTACK(o, i);
}
static void gobj_unref_any(CLuaInterface *self, void *o)
{
  if (o == NULL) return;
  if (GLUA_OBJ_IS_OURS(o)) gobj_UnReference((GLuaObject *)o);
  else GLUA_OBJ_UNREF(o);
}
static int gobj_istable_any(void *o)
{
  if (o == NULL) return 0;
  if (GLUA_OBJ_IS_OURS(o)) return gobj_GetType((GLuaObject *)o) == GT_TABLE;
  return GLUA_OBJ_ISTABLE(o) != 0;
}
static int gobj_gettype_any(void *o)
{
  if (o == NULL) return GT_NIL;
  if (GLUA_OBJ_IS_OURS(o)) return gobj_GetType((GLuaObject *)o);
  return (int)((int (*)(void*))GLUA_OBJ_VT(o)[0x18/8])(o);
}

static void gobj_push(GLuaObject *obj)
{
  lua_State *L;
  if (obj == NULL || obj->vtable != (const void *)glua_object_vtable) {
    glua_log("gobj_push: FOREIGN obj=%p vtable=%p (ours=%p)",
             (void *)obj, obj ? obj->vtable : NULL, (void *)glua_object_vtable);
    return;
  }
  L = (obj->iface != NULL) ? (lua_State *)((CLuaInterface *)obj->iface)->state : NULL;
  if (L == NULL) return;
  if (obj->ref != LUA_NOREF && obj->ref != LUA_REFNIL)
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->ref);
  else
    lua_pushnil(L);
}

/* Resolve an object's lua_State only when the object is one of ours (its
** vtable is our object vtable), so a foreign or stale pointer yields NULL and
** callers no-op instead of dereferencing garbage. */
static lua_State *gobj_state(GLuaObject *obj)
{
  if (obj == NULL || obj->vtable != (const void *)glua_object_vtable) return NULL;
  if (obj->iface == NULL) return NULL;
  return (lua_State *)((CLuaInterface *)obj->iface)->state;
}

static GLuaObject *glua_object_alloc(CLuaInterface *self)
{
  GLuaObject *obj = (GLuaObject *)malloc(sizeof(GLuaObject));
  if (obj == NULL) return NULL;
  obj->vtable = (const void *)glua_object_vtable;
  obj->iface = self;
  obj->ref = LUA_NOREF;
  return obj;
}

/* ------------------------------------------------------------------------ */
/* ILuaBase slots 0..54                                                     */

static int ci_slot_Top(CLuaInterface *self) { glua_log_v("SLOT:ci_slot_Top"); return lua_gettop(self->state); }

static void ci_slot_Push(CLuaInterface *self, int i) { glua_log_v("SLOT:ci_slot_Push"); lua_pushvalue(self->state, i); }

static void ci_slot_Pop(CLuaInterface *self, int amt)
{ glua_log_v("SLOT:ci_slot_Pop");
  lua_State *L = self->state;
  if (amt < 0) amt = 1;
  if (lua_gettop(L) < amt)
    luaL_error(L, "Lua stack over-popped!");
  lua_settop(L, -(amt) - 1);
}

static void ci_slot_GetTable(CLuaInterface *self, int i) { glua_log_v("SLOT:GetTable(i=%d top=%d)", i, lua_gettop(self->state)); lua_gettable(self->state, i); }
static void ci_slot_GetField(CLuaInterface *self, int i, const char *name)
{ glua_log_v("SLOT:GetField(i=%d name=%s top=%d)", i, name?name:"(null)", lua_gettop(self->state)); lua_getfield(self->state, i, name); }
static void ci_slot_SetField(CLuaInterface *self, int i, const char *name)
{ glua_log_v("SLOT:SetField(i=%d name=%s top=%d)", i, name?name:"(null)", lua_gettop(self->state));
  lua_setfield(self->state, i, name); }
static void ci_slot_CreateTable(CLuaInterface *self) { glua_log_v("SLOT:ci_slot_CreateTable"); lua_createtable(self->state, 0, 0); }
static void ci_slot_SetTable(CLuaInterface *self, int i) { glua_log_v("SLOT:ci_slot_SetTable"); lua_settable(self->state, i); }
static void ci_slot_SetMetaTable(CLuaInterface *self, int i) { glua_log_v("SLOT:ci_slot_SetMetaTable"); lua_setmetatable(self->state, i); }
static int  ci_slot_GetMetaTable(CLuaInterface *self, int i)
{ glua_log_v("SLOT:ci_slot_GetMetaTable"); return lua_getmetatable(self->state, i) != 0; }
static void ci_slot_Call(CLuaInterface *self, int args, int rets)
{ glua_log_v("SLOT:ci_slot_Call"); lua_call(self->state, args, rets); }
static int  ci_slot_PCall(CLuaInterface *self, int args, int rets, int errfunc)
{ glua_log_v("SLOT:ci_slot_PCall"); return lua_pcall(self->state, args, rets, errfunc); }
static int  ci_slot_Equal(CLuaInterface *self, int a, int b) { glua_log_v("SLOT:ci_slot_Equal"); return lua_equal(self->state, a, b); }
static int  ci_slot_RawEqual(CLuaInterface *self, int a, int b) { glua_log_v("SLOT:ci_slot_RawEqual"); return lua_rawequal(self->state, a, b); }
static void ci_slot_Insert(CLuaInterface *self, int i) { glua_log_v("SLOT:ci_slot_Insert"); lua_insert(self->state, i); }
static void ci_slot_Remove(CLuaInterface *self, int i) { glua_log_v("SLOT:ci_slot_Remove"); lua_remove(self->state, i); }
static int  ci_slot_Next(CLuaInterface *self, int i) { glua_log_v("SLOT:ci_slot_Next"); return lua_next(self->state, i); }
static void *ci_slot_NewUserdata(CLuaInterface *self, unsigned int sz)
{ glua_log_v("SLOT:ci_slot_NewUserdata"); return lua_newuserdata(self->state, (size_t)sz); }
static void ci_slot_ThrowError(CLuaInterface *self, const char *err)
{ glua_log_v("SLOT:ci_slot_ThrowError"); luaL_error(self->state, "%s", err ? err : ""); }
static void ci_slot_CheckType(CLuaInterface *self, int idx, int type)
{ glua_log_v("SLOT:ci_slot_CheckType");
  if (glua_gettype(self->state, idx) != type)
    luaL_typerror(self->state, idx, glua_type_name(type));
}
static void ci_slot_ArgError(CLuaInterface *self, int arg, const char *msg)
{ glua_log_v("SLOT:ci_slot_ArgError"); luaL_argerror(self->state, arg, msg); }
static void ci_slot_RawGet(CLuaInterface *self, int i) { glua_log_v("SLOT:ci_slot_RawGet"); lua_rawget(self->state, i); }
static void ci_slot_RawSet(CLuaInterface *self, int i) { glua_log_v("SLOT:ci_slot_RawSet"); lua_rawset(self->state, i); }
static const char *ci_slot_GetStringLen(CLuaInterface *self, int idx, unsigned int *outLen)
{ glua_log_v("SLOT:ci_slot_GetStringLen");
  size_t len = 0;
  const char *s = lua_tolstring(self->state, idx, &len);
  if (outLen) *outLen = (unsigned int)len;
  return s;
}
/* The host passes only the stack position here; asking for a length would
** write through whatever the third register happens to hold. */
static const char *ci_slot_GetString(CLuaInterface *self, int idx)
{ glua_log_v("SLOT:ci_slot_GetString");
  return lua_tolstring(self->state, idx, NULL);
}
static double ci_slot_GetNumber(CLuaInterface *self, int idx) { glua_log_v("SLOT:ci_slot_GetNumber"); return lua_tonumber(self->state, idx); }
static int ci_slot_GetBool(CLuaInterface *self, int idx) { glua_log_v("SLOT:ci_slot_GetBool"); return lua_toboolean(self->state, idx) != 0; }
static GLuaCFunc ci_slot_GetCFunction(CLuaInterface *self, int idx)
{ glua_log_v("SLOT:ci_slot_GetCFunction"); return (GLuaCFunc)lua_tocfunction(self->state, idx); }
static void *ci_slot_GetUserdata(CLuaInterface *self, int idx) { glua_log_v("SLOT:ci_slot_GetUserdata"); return lua_touserdata(self->state, idx); }
static void ci_slot_PushNil(CLuaInterface *self) { glua_log_v("SLOT:ci_slot_PushNil"); lua_pushnil(self->state); }
static void ci_slot_PushString(CLuaInterface *self, const char *val, unsigned int len)
{ glua_log_v("SLOT:ci_slot_PushString");
  if (val == NULL) val = "";
  if (len != 0) lua_pushlstring(self->state, val, len);
  else lua_pushstring(self->state, val);
}
static void ci_slot_PushNumber(CLuaInterface *self, double val) { glua_log_v("SLOT:ci_slot_PushNumber"); lua_pushnumber(self->state, val); }
static void ci_slot_PushBool(CLuaInterface *self, bool val) { glua_log_v("SLOT:ci_slot_PushBool"); lua_pushboolean(self->state, val); }
static void ci_slot_PushCFunction(CLuaInterface *self, int (*f)(lua_State *))
{ lua_pushcclosure(self->state, f, 0); }
static void ci_slot_PushCClosure(CLuaInterface *self, int (*f)(lua_State *), int n)
{ lua_pushcclosure(self->state, f, n); }
static void ci_slot_PushUserdata(CLuaInterface *self, void *p) { glua_log_v("SLOT:ci_slot_PushUserdata"); lua_pushlightuserdata(self->state, p); }
static int  ci_slot_ReferenceCreate(CLuaInterface *self) { int r = luaL_ref(self->state, LUA_REGISTRYINDEX); glua_log_v("SLOT:ReferenceCreate -> %d", r); return r; }
static void ci_slot_ReferenceFree(CLuaInterface *self, int i) { glua_log_v("SLOT:ReferenceFree(i=%d)", i); luaL_unref(self->state, LUA_REGISTRYINDEX, i); }
static void ci_slot_ReferencePush(CLuaInterface *self, int i)
{ glua_log_v("SLOT:ReferencePush(i=%d)", i); lua_rawgeti(self->state, LUA_REGISTRYINDEX, i); }
static void ci_slot_PushSpecial(CLuaInterface *self, int type)
{ glua_log_v("SLOT:ci_slot_PushSpecial");
  lua_State *L = self->state;
  switch (type) {
    case 0: lua_pushvalue(L, LUA_GLOBALSINDEX); break;
    case 1: lua_pushvalue(L, LUA_ENVIRONINDEX); break;
    case 2: lua_pushvalue(L, LUA_REGISTRYINDEX); break;
    default: lua_pushnil(L); break;
  }
}
static int ci_slot_IsType(CLuaInterface *self, int idx, int type)
{ glua_log_v("SLOT:ci_slot_IsType");
  int t = lua_type(self->state, idx);
  if (t == type) return 1;
  if (t == GT_USERDATA && type > GT_USERDATA) {
    GLuaUserData *ud = (GLuaUserData *)lua_touserdata(self->state, idx);
    return ud != NULL && ud->type == type;
  }
  return 0;
}
static int ci_slot_GetType(CLuaInterface *self, int idx) { glua_log_v("SLOT:ci_slot_GetType"); return glua_gettype(self->state, idx); }
static const char *ci_slot_GetTypeName(CLuaInterface *self, int type)
{ glua_log_v("SLOT:ci_slot_GetTypeName"); (void)self; return glua_type_name(type); }

static void ci_slot_CreateMetaTableType(CLuaInterface *self, const char *name, int type)
{ glua_log_v("SLOT:ci_slot_CreateMetaTableType");
  lua_State *L = self->state;
  if (luaL_newmetatable_type(L, name, type)) {
    if (type >= 0 && type < 255) {
      /* Reference behaviour: one metatable object per type id, re-pointed at
      ** every creation. */
      if (self->metaTables[type] == NULL) self->metaTables[type] = gobj_create(self);
      if (self->metaTables[type] != NULL)
        gobj_setstack_any(self, self->metaTables[type], -1);
    }
  }
  /* The metatable is left on top of the stack (-1) per GMod ABI contract. */
}

static const char *ci_slot_CheckString(CLuaInterface *self, int idx)
{ glua_log_v("SLOT:ci_slot_CheckString"); return luaL_checklstring(self->state, idx, NULL); }
static double ci_slot_CheckNumber(CLuaInterface *self, int idx) { glua_log_v("SLOT:ci_slot_CheckNumber"); return luaL_checknumber(self->state, idx); }
static int ci_slot_ObjLen(CLuaInterface *self, int idx) { glua_log_v("SLOT:ci_slot_ObjLen"); return (int)lua_objlen(self->state, idx); }

static const void *ci_slot_GetAngle(CLuaInterface *self, int idx)
{ glua_log_v("SLOT:ci_slot_GetAngle");
  static GLuaAngle zero = { 0.0f, 0.0f, 0.0f };
  GLuaUserData *ud = (GLuaUserData *)lua_touserdata(self->state, idx);
  if (ud == NULL || ud->data == NULL || ud->type != GT_ANGLE) return &zero;
  return ud->data;
}
static const void *ci_slot_GetVector(CLuaInterface *self, int idx)
{ glua_log_v("SLOT:ci_slot_GetVector");
  static GLuaVector zero = { 0.0f, 0.0f, 0.0f };
  GLuaUserData *ud = (GLuaUserData *)lua_touserdata(self->state, idx);
  if (ud == NULL || ud->data == NULL || ud->type != GT_VECTOR) return &zero;
  return ud->data;
}

/* Attaches the metatable for this type id.
**
** The reference keeps one metatable object per id and re-points it each time
** a metatable of that type is created, so the flavour created last is the one
** a value of that type receives. */
static void glua_attach_meta(CLuaInterface *self, lua_State *L, int type)
{
  /* The reference keeps one metatable object per type id and re-points it
  ** every time a metatable of that type is created, so the flavour created
  ** last is the one a value of that type gets.  Match it exactly. */
  if (L == NULL) return;
  if (type >= 0 && type < 255 && self->metaTables[type] != NULL) {
    gobj_push_obj(self, self->metaTables[type]);
    lua_setmetatable(L, -2);
  }
}
/* Typed value userdata: data points past the 16-byte UserData prefix. */
static void *ci_push_typed(CLuaInterface *self, const void *val, size_t size, int type)
{
  lua_State *L = self->state;
  GLuaUserData *ud = (GLuaUserData *)lua_newuserdata(L, 16 + size);
  void *payload = (char *)ud + 16;
  memcpy(payload, val, size);
  ud->data = payload;
  ud->type = (unsigned char)type;
  glua_attach_meta(self, L, type);
  return ud;
}

static void ci_slot_PushAngle(CLuaInterface *self, const GLuaAngle *val)
{ glua_log_v("SLOT:ci_slot_PushAngle"); ci_push_typed(self, val, sizeof(GLuaAngle), GT_ANGLE); }
static void ci_slot_PushVector(CLuaInterface *self, const GLuaVector *val)
{ glua_log_v("SLOT:ci_slot_PushVector"); ci_push_typed(self, val, sizeof(GLuaVector), GT_VECTOR); }

static void ci_slot_SetState(CLuaInterface *self, lua_State *L) { glua_log_v("SLOT:ci_slot_SetState"); glua_log_v("IFACE::SetState(%p)", (void*)L); glua_log_heartbeat(); self->state = L; }

static int ci_slot_CreateMetaTable(CLuaInterface *self, const char *name)
{ glua_log_v("SLOT:ci_slot_CreateMetaTable");
  lua_State *L = self->state;
  int id = self->metaIDCounter;
  if (luaL_newmetatable_type(L, name, id)) {
    if (id >= 0 && id < 255) {
      if (self->metaTables[id] == NULL) self->metaTables[id] = gobj_create(self);
      if (self->metaTables[id] != NULL) gobj_setstack_any(self, self->metaTables[id], -1);
    }
    self->metaIDCounter++;
    /* Leaves newly created metatable on stack (-1). */
    return id;
  }
  /* Already present: luaL_newmetatable_type already pushed it onto stack.
  ** Fetch its stored MetaID and leave the metatable at -1. */
  lua_getfield(L, -1, "MetaID");
  id = (int)lua_tonumber(L, -1);
  lua_pop(L, 1);
  return id;
}

static int ci_slot_PushMetaTable(CLuaInterface *self, int type)
{ glua_log_v("SLOT:ci_slot_PushMetaTable");
  lua_State *L = self->state;
  if (L == NULL) return 0;
  if (type < 0 || type >= 255 || self->metaTables[type] == NULL) return 0;
  gobj_push_obj(self, self->metaTables[type]);
  return 1;
}

static void ci_slot_PushUserType(CLuaInterface *self, void *data, int type)
{ glua_log_v("SLOT:ci_slot_PushUserType");
  lua_State *L = self->state;
  GLuaUserData *ud = (GLuaUserData *)lua_newuserdata(L, sizeof(GLuaUserData));
  ud->data = data;
  ud->type = (unsigned char)type;
  glua_attach_meta(self, L, type);
}

static void ci_slot_SetUserType(CLuaInterface *self, int idx, void *data)
{ glua_log_v("SLOT:ci_slot_SetUserType");
  GLuaUserData *ud = (GLuaUserData *)lua_touserdata(self->state, idx);
  if (ud != NULL) ud->data = data;
}

/* ------------------------------------------------------------------------ */
/* Forward declarations for slots implemented after the interface is fully  */
/* declared (Init, RunStringEx, ...).                                       */

static int  ci_slot_Init(CLuaInterface *self, void *cb, bool dedicated);
static void ci_slot_Shutdown(CLuaInterface *self);
static void ci_slot_Cycle(CLuaInterface *self);
static GLuaObject *ci_slot_Global(CLuaInterface *self);
static GLuaObject *ci_slot_GetObject(CLuaInterface *self, int index);
static void ci_slot_PushLuaObject(CLuaInterface *self, GLuaObject *obj);
static void ci_slot_PushLuaFunction(CLuaInterface *self, int (*f)(lua_State *));
static void ci_slot_LuaError(CLuaInterface *self, const char *err, int index);
static void ci_slot_TypeError(CLuaInterface *self, const char *name, int index);
static void ci_slot_CallInternal(CLuaInterface *self, int args, int rets);
static void ci_slot_CallInternalNoReturns(CLuaInterface *self, int args);
static int  ci_slot_CallInternalGetBool(CLuaInterface *self, int args);
static const char *ci_slot_CallInternalGetString(CLuaInterface *self, int args);
static int  ci_slot_CallInternalGet(CLuaInterface *self, int args, GLuaObject *obj);
static void ci_slot_NewGlobalTable(CLuaInterface *self, const char *name);
static GLuaObject *ci_slot_NewTemporaryObject(CLuaInterface *self);
static int  ci_slot_isUserData(CLuaInterface *self, int index);
static GLuaObject *ci_slot_GetMetaTableObjectName(CLuaInterface *self, const char *name, int type);
static GLuaObject *ci_slot_GetMetaTableObjectIndex(CLuaInterface *self, int index);
static GLuaObject *ci_slot_GetReturn(CLuaInterface *self, int index);
static int  ci_slot_IsServer(CLuaInterface *self);
static int  ci_slot_IsClient(CLuaInterface *self);
static int  ci_slot_IsMenu(CLuaInterface *self);
static void ci_slot_DestroyObject(CLuaInterface *self, GLuaObject *obj);
static GLuaObject *ci_slot_CreateObject(CLuaInterface *self);
static void ci_slot_SetMemberStrObj(CLuaInterface *self, GLuaObject *t, const char *k, GLuaObject *v);
static void ci_slot_SetMemberStr(CLuaInterface *self, GLuaObject *t, const char *k);
static void ci_slot_SetMemberNumObj(CLuaInterface *self, GLuaObject *t, float k, GLuaObject *v);
static void ci_slot_SetMemberNum(CLuaInterface *self, GLuaObject *t, float k);
static void ci_slot_SetMemberObjObj(CLuaInterface *self, GLuaObject *t, GLuaObject *k, GLuaObject *v);
static GLuaObject *ci_slot_GetNewTable(CLuaInterface *self);
static void ci_slot_SetType(CLuaInterface *self, unsigned char realm);
static void ci_slot_PushLong(CLuaInterface *self, long num);
static int  ci_slot_GetFlags(CLuaInterface *self, int index);
static int  ci_slot_FindOnObjectsMetaTable(CLuaInterface *self, int obj, int key);
static int  ci_slot_FindObjectOnTable(CLuaInterface *self, int tbl, int key);
static void ci_slot_SetMemberFast(CLuaInterface *self, GLuaObject *t, int k, int v);
static int  ci_slot_RunString(CLuaInterface *self, const char *fn, const char *path,
                              const char *str, bool run, bool showErrors);
static int  ci_slot_IsEqual(CLuaInterface *self, GLuaObject *a, GLuaObject *b);
static void ci_slot_Error(CLuaInterface *self, const char *err);
static const char *ci_slot_GetStringOrError(CLuaInterface *self, int index);
static int  ci_slot_RunLuaModule(CLuaInterface *self, const char *name);
static int  ci_slot_FindAndRunScript(CLuaInterface *self, const char *fn, bool run,
                                     bool showErrors, const char *str, bool noReturns);
static void ci_slot_SetPathID(CLuaInterface *self, const char *id);
static const char *ci_slot_GetPathID(CLuaInterface *self);
static void ci_slot_ErrorNoHalt(CLuaInterface *self, const char *fmt, ...);
static void ci_slot_Msg(CLuaInterface *self, const char *fmt, ...);
static void ci_slot_PushPath(CLuaInterface *self, const char *path);
static void ci_slot_PopPath(CLuaInterface *self);
static const char *ci_slot_GetPath(CLuaInterface *self);
extern int opglua_luashared_findhook(void *, const void *, const void *, void *);
static void *ci_slot_GetColor(CLuaInterface *self, void *out, int index);
static void *ci_slot_PushColor(CLuaInterface *self, GLuaColor color);
static int  ci_slot_GetStack(CLuaInterface *self, int level, lua_Debug *dbg);
static int  ci_slot_GetInfo(CLuaInterface *self, const char *what, lua_Debug *dbg);
static const char *ci_slot_GetLocal(CLuaInterface *self, lua_Debug *dbg, int n);
static const char *ci_slot_GetUpvalue(CLuaInterface *self, int funcIndex, int n);
static int  ci_slot_RunStringEx(CLuaInterface *self, const char *fn, const char *path,
                                const char *str, bool run, bool printErrors,
                                bool dontPushErrors, bool noReturns);
static size_t ci_slot_GetDataString(CLuaInterface *self, int index, const char **str);
static void ci_slot_ErrorFromLua(CLuaInterface *self, const char *fmt, ...);
static const char *ci_slot_GetCurrentLocation(CLuaInterface *self);
static void ci_slot_MsgColour(CLuaInterface *self, const GLuaColor *col, const char *fmt, ...);
static void ci_slot_GetCurrentFile(CLuaInterface *self, void *outStr);
static int  ci_slot_CompileString(CLuaInterface *self, void *dumper, const void *str);
static int  ci_slot_CallFunctionProtected(CLuaInterface *self, int args, int rets, bool printErr);
static void ci_slot_Require(CLuaInterface *self, const char *name);
static const char *ci_slot_GetActualTypeName(CLuaInterface *self, int type);
static void ci_slot_PreCreateTable(CLuaInterface *self, int arr, int narr);
static void ci_slot_PushPooledString(CLuaInterface *self, int index);
static const char *ci_slot_GetPooledString(CLuaInterface *self, int index);
static int  ci_slot_AddThreadedCall(CLuaInterface *self, void *call);
static void ci_slot_AppendStackTrace(CLuaInterface *self, char *buf, unsigned int size);
static void *ci_slot_CreateConVar(CLuaInterface *self, const char *name, const char *def,
                                  const char *help, int flags);
static void *ci_slot_CreateConCommand(CLuaInterface *self, const char *name, const char *help,
                                      int flags, void *cb, void *completion);
static const char *ci_slot_CheckStringOpt(CLuaInterface *self, int idx, const char *def);
static double ci_slot_CheckNumberOpt(CLuaInterface *self, int idx, double def);
static int  ci_slot_RegisterMetaTable(CLuaInterface *self, const char *name, GLuaObject *tbl);
static void ci_slot_dtor(CLuaInterface *self);

/* ------------------------------------------------------------------------ */
/* The 132-slot shipping vtable, emitted strictly in the reference order.   */

const glua_vfn glua_interface_vtable[132] = {
  /*   0 */ (glua_vfn)ci_slot_Top,
  /*   1 */ (glua_vfn)ci_slot_Push,
  /*   2 */ (glua_vfn)ci_slot_Pop,
  /*   3 */ (glua_vfn)ci_slot_GetTable,
  /*   4 */ (glua_vfn)ci_slot_GetField,
  /*   5 */ (glua_vfn)ci_slot_SetField,
  /*   6 */ (glua_vfn)ci_slot_CreateTable,
  /*   7 */ (glua_vfn)ci_slot_SetTable,
  /*   8 */ (glua_vfn)ci_slot_SetMetaTable,
  /*   9 */ (glua_vfn)ci_slot_GetMetaTable,
  /*  10 */ (glua_vfn)ci_slot_Call,
  /*  11 */ (glua_vfn)ci_slot_PCall,
  /*  12 */ (glua_vfn)ci_slot_Equal,
  /*  13 */ (glua_vfn)ci_slot_RawEqual,
  /*  14 */ (glua_vfn)ci_slot_Insert,
  /*  15 */ (glua_vfn)ci_slot_Remove,
  /*  16 */ (glua_vfn)ci_slot_Next,
  /*  17 */ (glua_vfn)ci_slot_NewUserdata,
  /*  18 */ (glua_vfn)ci_slot_ThrowError,
  /*  19 */ (glua_vfn)ci_slot_CheckType,
  /*  20 */ (glua_vfn)ci_slot_ArgError,
  /*  21 */ (glua_vfn)ci_slot_RawGet,
  /*  22 */ (glua_vfn)ci_slot_RawSet,
  /*  23 */ (glua_vfn)ci_slot_GetStringLen,
  /*  24 */ (glua_vfn)ci_slot_GetNumber,
  /*  25 */ (glua_vfn)ci_slot_GetBool,
  /*  26 */ (glua_vfn)ci_slot_GetCFunction,
  /*  27 */ (glua_vfn)ci_slot_GetUserdata,
  /*  28 */ (glua_vfn)ci_slot_PushNil,
  /*  29 */ (glua_vfn)ci_slot_PushString,
  /*  30 */ (glua_vfn)ci_slot_PushNumber,
  /*  31 */ (glua_vfn)ci_slot_PushBool,
  /*  32 */ (glua_vfn)ci_slot_PushCFunction,
  /*  33 */ (glua_vfn)ci_slot_PushCClosure,
  /*  34 */ (glua_vfn)ci_slot_PushUserdata,
  /*  35 */ (glua_vfn)ci_slot_ReferenceCreate,
  /*  36 */ (glua_vfn)ci_slot_ReferenceFree,
  /*  37 */ (glua_vfn)ci_slot_ReferencePush,
  /*  38 */ (glua_vfn)ci_slot_PushSpecial,
  /*  39 */ (glua_vfn)ci_slot_IsType,
  /*  40 */ (glua_vfn)ci_slot_GetType,
  /*  41 */ (glua_vfn)ci_slot_GetTypeName,
  /*  42 */ (glua_vfn)ci_slot_CreateMetaTableType,
  /*  43 */ (glua_vfn)ci_slot_GetString,
  /*  44 */ (glua_vfn)ci_slot_GetNumber,
  /*  45 */ (glua_vfn)ci_slot_ObjLen,
  /*  46 */ (glua_vfn)ci_slot_GetAngle,
  /*  47 */ (glua_vfn)ci_slot_GetVector,
  /*  48 */ (glua_vfn)ci_slot_PushAngle,
  /*  49 */ (glua_vfn)ci_slot_PushVector,
  /*  50 */ (glua_vfn)ci_slot_SetState,
  /*  51 */ (glua_vfn)ci_slot_CreateMetaTable,
  /*  52 */ (glua_vfn)ci_slot_PushMetaTable,
  /*  53 */ (glua_vfn)ci_slot_PushUserType,
  /*  54 */ (glua_vfn)ci_slot_SetUserType,
  /*  55 */ (glua_vfn)ci_slot_Init,
  /*  56 */ (glua_vfn)ci_slot_Shutdown,
  /*  57 */ (glua_vfn)ci_slot_Cycle,
  /*  58 */ (glua_vfn)ci_slot_Global,
  /*  59 */ (glua_vfn)ci_slot_GetObject,
  /*  60 */ (glua_vfn)ci_slot_PushLuaObject,
  /*  61 */ (glua_vfn)ci_slot_PushLuaFunction,
  /*  62 */ (glua_vfn)ci_slot_LuaError,
  /*  63 */ (glua_vfn)ci_slot_TypeError,
  /*  64 */ (glua_vfn)ci_slot_CallInternal,
  /*  65 */ (glua_vfn)ci_slot_CallInternalNoReturns,
  /*  66 */ (glua_vfn)ci_slot_CallInternalGetBool,
  /*  67 */ (glua_vfn)ci_slot_CallInternalGetString,
  /*  68 */ (glua_vfn)ci_slot_CallInternalGet,
  /*  69 */ (glua_vfn)ci_slot_NewGlobalTable,
  /*  70 */ (glua_vfn)ci_slot_NewTemporaryObject,
  /*  71 */ (glua_vfn)ci_slot_isUserData,
  /*  72 */ (glua_vfn)ci_slot_GetMetaTableObjectIndex,
  /*  73 */ (glua_vfn)ci_slot_GetMetaTableObjectName,
  /*  74 */ (glua_vfn)ci_slot_GetReturn,
  /*  75 */ (glua_vfn)ci_slot_IsServer,
  /*  76 */ (glua_vfn)ci_slot_IsClient,
  /*  77 */ (glua_vfn)ci_slot_IsMenu,
  /*  78 */ (glua_vfn)ci_slot_DestroyObject,
  /*  79 */ (glua_vfn)ci_slot_CreateObject,
  /*  80 */ (glua_vfn)ci_slot_SetMemberStrObj,
  /*  81 */ (glua_vfn)ci_slot_SetMemberStr,
  /*  82 */ (glua_vfn)ci_slot_SetMemberNumObj,
  /*  83 */ (glua_vfn)ci_slot_SetMemberNum,
  /*  84 */ (glua_vfn)ci_slot_SetMemberObjObj,
  /*  85 */ (glua_vfn)ci_slot_GetNewTable,
  /*  86 */ (glua_vfn)ci_slot_SetType,
  /*  87 */ (glua_vfn)ci_slot_PushLong,
  /*  88 */ (glua_vfn)ci_slot_GetFlags,
  /*  89 */ (glua_vfn)ci_slot_FindOnObjectsMetaTable,
  /*  90 */ (glua_vfn)ci_slot_FindObjectOnTable,
  /*  91 */ (glua_vfn)ci_slot_SetMemberFast,
  /*  92 */ (glua_vfn)ci_slot_RunString,
  /*  93 */ (glua_vfn)ci_slot_IsEqual,
  /*  94 */ (glua_vfn)ci_slot_Error,
  /*  95 */ (glua_vfn)ci_slot_GetStringOrError,
  /*  96 */ (glua_vfn)ci_slot_RunLuaModule,
  /*  97 */ (glua_vfn)ci_slot_FindAndRunScript,
  /*  98 */ (glua_vfn)ci_slot_SetPathID,
  /*  99 */ (glua_vfn)ci_slot_GetPathID,
  /* 100 */ (glua_vfn)ci_slot_ErrorNoHalt,
  /* 101 */ (glua_vfn)ci_slot_Msg,
  /* 102 */ (glua_vfn)ci_slot_PushPath,
  /* 103 */ (glua_vfn)ci_slot_PopPath,
  /* 104 */ (glua_vfn)ci_slot_GetPath,
  /* 105 */ (glua_vfn)ci_slot_GetColor,
  /* 106 */ (glua_vfn)ci_slot_PushColor,
  /* 107 */ (glua_vfn)ci_slot_GetStack,
  /* 108 */ (glua_vfn)ci_slot_GetInfo,
  /* 109 */ (glua_vfn)ci_slot_GetLocal,
  /* 110 */ (glua_vfn)ci_slot_GetUpvalue,
  /* 111 */ (glua_vfn)ci_slot_RunStringEx,
  /* 112 */ (glua_vfn)ci_slot_GetDataString,
  /* 113 */ (glua_vfn)ci_slot_ErrorFromLua,
  /* 114 */ (glua_vfn)ci_slot_GetCurrentLocation,
  /* 115 */ (glua_vfn)ci_slot_MsgColour,
  /* 116 */ (glua_vfn)ci_slot_GetCurrentFile,
  /* 117 */ (glua_vfn)ci_slot_CompileString,
  /* 118 */ (glua_vfn)ci_slot_CallFunctionProtected,
  /* 119 */ (glua_vfn)ci_slot_Require,
  /* 120 */ (glua_vfn)ci_slot_GetActualTypeName,
  /* 121 */ (glua_vfn)ci_slot_PreCreateTable,
  /* 122 */ (glua_vfn)ci_slot_PushPooledString,
  /* 123 */ (glua_vfn)ci_slot_GetPooledString,
  /* 124 */ (glua_vfn)ci_slot_AddThreadedCall,
  /* 125 */ (glua_vfn)ci_slot_AppendStackTrace,
  /* 126 */ (glua_vfn)ci_slot_CreateConVar,
  /* 127 */ (glua_vfn)ci_slot_CreateConCommand,
  /* 128 */ (glua_vfn)ci_slot_CheckStringOpt,
  /* 129 */ (glua_vfn)ci_slot_CheckNumberOpt,
  /* 130 */ (glua_vfn)ci_slot_RegisterMetaTable,
  /* 131 */ (glua_vfn)ci_slot_dtor
};

/* ------------------------------------------------------------------------ */
/* ILuaObject vtable (93 slots, published declaration order).  Bound-method */
/* entries are implemented; the remainder are safe stand-ins.               */

/* Generic stand-in for ILuaObject slots we do not implement: returns zero /
** NULL so a caller reads a benign default rather than garbage. */
static intptr_t gobj_stub(void) { return 0; }

static void gobj_Set(GLuaObject *obj, GLuaObject *other)
{
  if (gobj_state(obj) != NULL && obj->ref != LUA_NOREF && obj->ref != LUA_REFNIL)
    luaL_unref(obj->iface->state, LUA_REGISTRYINDEX, obj->ref);
  obj->ref = LUA_NOREF;
  if (other == NULL || other->iface == NULL || other->ref == LUA_NOREF) {
    obj->iface = other ? other->iface : obj->iface;
    return;
  }
  obj->iface = other->iface;
  gobj_push(other);
  obj->ref = luaL_ref(obj->iface->state, LUA_REGISTRYINDEX);
}

static void gobj_SetFromStack(GLuaObject *obj, int i)
{
  if (gobj_state(obj) != NULL) gobj_bind_stack(obj->iface, obj, i);
}

static void gobj_UnReference(GLuaObject *obj)
{
  if (gobj_state(obj) != NULL && obj->ref != LUA_NOREF && obj->ref != LUA_REFNIL)
    luaL_unref(obj->iface->state, LUA_REGISTRYINDEX, obj->ref);
  obj->ref = LUA_NOREF;
}

static int gobj_GetType(GLuaObject *obj)
{
  int t;
  if (gobj_state(obj) == NULL) return GT_NIL;
  gobj_push(obj);
  t = glua_gettype(obj->iface->state, -1);
  lua_pop(obj->iface->state, 1);
  return t;
}

static const char *gobj_GetString(GLuaObject *obj)
{
  const char *s;
  if (gobj_state(obj) == NULL) return NULL;
  gobj_push(obj);
  s = lua_tostring(obj->iface->state, -1);
  /* The value stays alive after popping: the object holds a registry
  ** reference to it. */
  lua_pop(obj->iface->state, 1);
  return s;
}

static float gobj_GetFloat(GLuaObject *obj)
{
  double d;
  if (gobj_state(obj) == NULL) return 0.0f;
  gobj_push(obj);
  d = lua_tonumber(obj->iface->state, -1);
  lua_pop(obj->iface->state, 1);
  return (float)d;
}

static int gobj_GetInt(GLuaObject *obj) { return (int)gobj_GetFloat(obj); }
static void *gobj_GetUserData(GLuaObject *obj)
{
  void *p;
  if (gobj_state(obj) == NULL) return NULL;
  gobj_push(obj);
  p = lua_touserdata(obj->iface->state, -1);
  lua_pop(obj->iface->state, 1);
  return p;
}

static void gobj_Push(GLuaObject *obj) { gobj_push(obj); }

/* Reads the member with the given name out of this object into another one.
** The host uses this on the global table to fetch a named global; the
** destination is handed in, so a missing member leaves it untouched. */
static void gobj_ReadMemberByName( GLuaObject *obj, const char *name, GLuaObject *out )
{
  CLuaInterface *lua;
  lua_State *L;
  if ( obj == NULL || out == NULL ) return;
  lua = obj->iface;
  if ( lua == NULL || lua->state == NULL ) return;
  L = lua->state;
  gobj_push( obj );
  if ( name != NULL ) lua_pushstring( L, name ); else lua_pushnil( L );
  lua_gettable( L, -2 );
  gobj_SetFromStack( out, -1 );
  lua_pop( L, 2 );
}

static int gobj_isNil(GLuaObject *obj) { return gobj_GetType(obj) == GT_NIL; }
static int gobj_isTable(GLuaObject *obj) { return gobj_GetType(obj) == GT_TABLE; }
static int gobj_isString(GLuaObject *obj) { return gobj_GetType(obj) == GT_STRING; }
static int gobj_isNumber(GLuaObject *obj) { return gobj_GetType(obj) == GT_NUMBER; }
static int gobj_isFunction(GLuaObject *obj) { return gobj_GetType(obj) == GT_FUNCTION; }
static int gobj_isUserData(GLuaObject *obj) { return gobj_GetType(obj) == GT_USERDATA; }

static void gobj_SetMemberStr(GLuaObject *obj, const char *name)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushvalue(obj->iface->state, -2);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 2);
}

static void gobj_SetMemberStrObj(GLuaObject *obj, const char *name, GLuaObject *val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  if (val) gobj_push(val); else lua_pushnil(obj->iface->state);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberStrNum(GLuaObject *obj, const char *name, float val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushnumber(obj->iface->state, val);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberStrBool(GLuaObject *obj, const char *name, int val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushboolean(obj->iface->state, val);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberStrStr(GLuaObject *obj, const char *name, const char *val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushstring(obj->iface->state, val);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberStrFn(GLuaObject *obj, const char *name, int (*f)(lua_State *))
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushcclosure(obj->iface->state, f, 0);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 1);
}

static int gobj_GetMemberBool(GLuaObject *obj, const char *name, int def)
{
  int r = def;
  if (gobj_state(obj) == NULL) return def;
  gobj_push(obj);
  lua_getfield(obj->iface->state, -1, name);
  if (lua_type(obj->iface->state, -1) == GT_BOOL) r = lua_toboolean(obj->iface->state, -1);
  lua_pop(obj->iface->state, 2);
  return r;
}

static int gobj_GetMemberInt(GLuaObject *obj, const char *name, int def)
{
  int r = def;
  if (gobj_state(obj) == NULL) return def;
  gobj_push(obj);
  lua_getfield(obj->iface->state, -1, name);
  if (lua_type(obj->iface->state, -1) == GT_NUMBER) r = (int)lua_tonumber(obj->iface->state, -1);
  lua_pop(obj->iface->state, 2);
  return r;
}

static float gobj_GetMemberFloat(GLuaObject *obj, const char *name, float def)
{
  float r = def;
  if (gobj_state(obj) == NULL) return def;
  gobj_push(obj);
  lua_getfield(obj->iface->state, -1, name);
  if (lua_type(obj->iface->state, -1) == GT_NUMBER) r = (float)lua_tonumber(obj->iface->state, -1);
  lua_pop(obj->iface->state, 2);
  return r;
}

static const char *gobj_GetMemberStr(GLuaObject *obj, const char *name, const char *def)
{
  const char *r = def;
  if (gobj_state(obj) == NULL) return def;
  gobj_push(obj);
  lua_getfield(obj->iface->state, -1, name);
  if (lua_type(obj->iface->state, -1) == GT_STRING) r = lua_tostring(obj->iface->state, -1);
  lua_pop(obj->iface->state, 2);
  return r;
}

static void gobj_SetMemberNum(GLuaObject *obj, float key)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushvalue(obj->iface->state, -2);
  lua_pushnumber(obj->iface->state, key);
  lua_settable(obj->iface->state, -3);
  lua_pop(obj->iface->state, 2);
}

static void gobj_SetMemberNumVal(GLuaObject *obj, float key, float val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushnumber(obj->iface->state, key);
  lua_pushnumber(obj->iface->state, val);
  lua_settable(obj->iface->state, -3);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberNumObj(GLuaObject *obj, float key, GLuaObject *val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushnumber(obj->iface->state, key);
  if (val) gobj_push(val); else lua_pushnil(obj->iface->state);
  lua_settable(obj->iface->state, -3);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberObjObj(GLuaObject *obj, GLuaObject *k, GLuaObject *v)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  if (k) gobj_push(k); else lua_pushnil(obj->iface->state);
  if (v) gobj_push(v); else lua_pushnil(obj->iface->state);
  lua_settable(obj->iface->state, -3);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetReference(GLuaObject *obj, int i)
{
  if (obj->iface) obj->ref = i;
}

static void gobj_SetFromGlobal(GLuaObject *obj, const char *name)
{
  if (gobj_state(obj) == NULL) return;
  lua_getglobal(obj->iface->state, name);
  gobj_bind_stack(obj->iface, obj, -1);
  lua_pop(obj->iface->state, 1);
}

static int gobj_Init(GLuaObject *obj)
{
  if (gobj_state(obj) != NULL && obj->ref != LUA_NOREF && obj->ref != LUA_REFNIL)
    luaL_unref(obj->iface->state, LUA_REGISTRYINDEX, obj->ref);
  obj->ref = LUA_NOREF;
  return 0;
}

static float gobj_GetFloat0(GLuaObject *obj) { return gobj_GetFloat(obj); }
static int gobj_GetBool0(GLuaObject *obj) { return gobj_GetType(obj) == GT_BOOL; }
static int gobj_RemoveMe(GLuaObject *obj) { (void)obj; return 0; }

static void gobj_SetFloat(GLuaObject *obj, float v)
{
  (void)obj; (void)v;
}
static void gobj_SetString(GLuaObject *obj, const char *v) { (void)obj; (void)v; }
static double gobj_GetDouble(GLuaObject *obj) { return (double)gobj_GetFloat(obj); }
/* The host passes only the object; a length out-parameter would be written
** through whatever the second register happens to hold, so report nothing. */
static int gobj_GetStringLen(GLuaObject *obj)
{
  if (obj == NULL || obj->iface == NULL) return 0;
  return 0;
}

const glua_vfn glua_object_vtable[102] = {
  /*   0 */ (glua_vfn)gobj_Set,
  /*   1 */ (glua_vfn)gobj_SetFromStack,
  /*   2 */ (glua_vfn)gobj_UnReference,
  /*   3 */ (glua_vfn)gobj_GetType,
  /*   4 */ (glua_vfn)gobj_GetString,
  /*   5 */ (glua_vfn)gobj_GetFloat,
  /*   6 */ (glua_vfn)gobj_GetInt,
  /*   7 */ (glua_vfn)gobj_GetUserData,
  /*   8 */ (glua_vfn)gobj_SetMemberStr,
  /*   9 */ (glua_vfn)gobj_SetMemberStrObj,
  /*  10 */ (glua_vfn)gobj_SetMemberStrNum,
  /*  11 */ (glua_vfn)gobj_SetMemberStrBool,
  /*  12 */ (glua_vfn)gobj_SetMemberStrStr,
  /*  13 */ (glua_vfn)gobj_SetMemberStrFn,
  /*  14 */ (glua_vfn)gobj_GetMemberBool,
  /*  15 */ (glua_vfn)gobj_GetMemberInt,
  /*  16 */ (glua_vfn)gobj_GetMemberFloat,
  /*  17 */ (glua_vfn)gobj_GetMemberStr,
  /*  18 */ (glua_vfn)gobj_stub,
  /*  19 */ (glua_vfn)gobj_stub,
  /*  20 */ (glua_vfn)gobj_stub,
  /*  21 */ (glua_vfn)gobj_stub,
  /*  22 */ (glua_vfn)gobj_GetMemberInt,
  /*  23 */ (glua_vfn)gobj_stub,
  /*  24 */ (glua_vfn)gobj_stub,
  /*  25 */ (glua_vfn)gobj_stub,
  /*  26 */ (glua_vfn)gobj_stub,
  /*  27 */ (glua_vfn)gobj_stub,
  /*  28 */ (glua_vfn)gobj_stub,
  /*  29 */ (glua_vfn)gobj_stub,
  /*  30 */ (glua_vfn)gobj_ReadMemberByName,
  /*  31 */ (glua_vfn)gobj_stub,
  /*  32 */ (glua_vfn)gobj_stub,
  /*  33 */ (glua_vfn)gobj_Push,
  /*  34 */ (glua_vfn)gobj_isNil,
  /*  35 */ (glua_vfn)gobj_isTable,
  /*  36 */ (glua_vfn)gobj_isString,
  /*  37 */ (glua_vfn)gobj_isNumber,
  /*  38 */ (glua_vfn)gobj_isFunction,
  /*  39 */ (glua_vfn)gobj_isUserData,
  /*  40 */ (glua_vfn)gobj_stub,
  /*  41 */ (glua_vfn)gobj_stub,
  /*  42 */ (glua_vfn)gobj_SetMemberNum,
  /*  43 */ (glua_vfn)gobj_SetMemberNumObj,
  /*  44 */ (glua_vfn)gobj_SetMemberNumVal,
  /*  45 */ (glua_vfn)gobj_stub,
  /*  46 */ (glua_vfn)gobj_stub,
  /*  47 */ (glua_vfn)gobj_stub,
  /*  48 */ (glua_vfn)gobj_stub,
  /*  49 */ (glua_vfn)gobj_SetMemberObjObj,
  /*  50 */ (glua_vfn)gobj_GetBool0,
  /*  51 */ (glua_vfn)gobj_stub,
  /*  52 */ (glua_vfn)gobj_stub,
  /*  53 */ (glua_vfn)gobj_SetFloat,
  /*  54 */ (glua_vfn)gobj_SetString,
  /*  55 */ (glua_vfn)gobj_GetDouble,
  /*  56 */ (glua_vfn)gobj_stub,
  /*  57 */ (glua_vfn)gobj_stub,
  /*  58 */ (glua_vfn)gobj_stub,
  /*  59 */ (glua_vfn)gobj_stub,
  /*  60 */ (glua_vfn)gobj_stub,
  /*  61 */ (glua_vfn)gobj_stub,
  /*  62 */ (glua_vfn)gobj_stub,
  /*  63 */ (glua_vfn)gobj_stub,
  /*  64 */ (glua_vfn)gobj_stub,
  /*  65 */ (glua_vfn)gobj_RemoveMe,
  /*  66 */ (glua_vfn)gobj_Init,
  /*  67 */ (glua_vfn)gobj_SetFromGlobal,
  /*  68 */ (glua_vfn)gobj_GetStringLen,
  /*  69 */ (glua_vfn)gobj_stub,
  /*  70 */ (glua_vfn)gobj_stub,
  /*  71 */ (glua_vfn)gobj_stub,
  /*  72 */ (glua_vfn)gobj_SetReference,
  /*  73 */ (glua_vfn)gobj_stub,
  /*  74 */ (glua_vfn)gobj_stub,
  /*  75 */ (glua_vfn)gobj_stub,
  /*  76 */ (glua_vfn)gobj_stub,
  /*  77 */ (glua_vfn)gobj_stub,
  /*  78 */ (glua_vfn)gobj_stub,
  /*  79 */ (glua_vfn)gobj_stub,
  /*  80 */ (glua_vfn)gobj_stub,
  /*  81 */ (glua_vfn)gobj_stub,
  /*  82 */ (glua_vfn)gobj_stub,
  /*  83 */ (glua_vfn)gobj_stub,
  /*  84 */ (glua_vfn)gobj_stub,
  /*  85 */ (glua_vfn)gobj_stub,
  /*  86 */ (glua_vfn)gobj_stub,
  /*  87 */ (glua_vfn)gobj_stub,
  /*  88 */ (glua_vfn)gobj_stub,
  /*  89 */ (glua_vfn)gobj_stub,
  /*  90 */ (glua_vfn)gobj_stub,
  /*  91 */ (glua_vfn)gobj_stub,
  /*  92 */ (glua_vfn)gobj_stub,
  /*  93 */ (glua_vfn)gobj_stub,
  /*  94 */ (glua_vfn)gobj_stub,
  /*  95 */ (glua_vfn)gobj_stub,
  /*  96 */ (glua_vfn)gobj_stub,
  /*  97 */ (glua_vfn)gobj_stub,
  /*  98 */ (glua_vfn)gobj_stub,
  /*  99 */ (glua_vfn)gobj_stub,
  /* 100 */ (glua_vfn)gobj_stub,
  /* 101 */ (glua_vfn)gobj_stub
};

/* ------------------------------------------------------------------------ */
/* ILuaInterface slots 55..131                                              */

static void glua_warn_once(const char *what)
{
  /* Small seen-set so alternating stand-ins each report once. */
  static const char *seen[16];
  static int seenCount = 0;
  int i;
  for (i = 0; i < seenCount; i++)
    if (seen[i] == what) return;
  if (seenCount < 16) seen[seenCount++] = what;
  fprintf(stdout, "[opglua] safe stand-in for %s (needs the host game)\n", what);
  fflush(stdout);
}

/* _LOADLIB holder used by the binary module loader. */
typedef struct GLuaLoadLib {
#ifdef _WIN32
  HMODULE hmod;
#else
  void *hmod;
#endif
} GLuaLoadLib;

static int loadlib_close(lua_State *L)
{
  GLuaLoadLib *holder = (GLuaLoadLib *)luaL_checkudata(L, 1, "_LOADLIB");
  if (holder->hmod != NULL) {
#ifdef _WIN32
    FARPROC closefn = GetProcAddress(holder->hmod, "gmod13_close");
    if (closefn != NULL) {
      lua_pushcclosure(L, (lua_CFunction)(void *)closefn, 0);
      lua_call(L, 0, 0);
    }
    FreeLibrary(holder->hmod);
#else
    {
      void *closefn = dlsym(holder->hmod, "gmod13_close");
      if (closefn != NULL) {
        lua_pushcclosure(L, (lua_CFunction)closefn, 0);
        lua_call(L, 0, 0);
      }
      dlclose(holder->hmod);
    }
#endif
    holder->hmod = NULL;
  }
  return 0;
}

/* Load a module and resolve a symbol.  Return values match the reference
** worker: 0 = symbol pushed (or load-only true), 1 = load failure,
** 2 = symbol not found. */
static int glua_open_module(lua_State *L, const char *path, const char *symbol)
{
  GLuaLoadLib *holder;

  lua_pushfstring(L, "LOADLIB: %s", path);
  lua_gettable(L, LUA_REGISTRYINDEX);
  if (lua_type(L, -1) == 0) {          /* not cached yet */
    lua_pop(L, 1);
    holder = (GLuaLoadLib *)lua_newuserdata(L, sizeof(GLuaLoadLib));
    holder->hmod = NULL;
    luaL_setmetatable(L, "_LOADLIB");
    lua_pushfstring(L, "LOADLIB: %s", path);
    lua_pushvalue(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);
  } else {
    holder = (GLuaLoadLib *)lua_touserdata(L, -1);
  }
  lua_pop(L, 1);

  if (holder->hmod == NULL) {
#ifdef _WIN32
    holder->hmod = LoadLibraryExA(path, NULL, 0);
#else
    holder->hmod = dlopen(path, RTLD_NOW);
#endif
    if (holder->hmod == NULL) {
      lua_pushfstring(L, "Couldn't load module library! (%s)", path);
      return 1;      /* load failure */
    }
  }

  if (symbol[0] == '*') {                    /* load-only request */
    lua_pushboolean(L, 1);
    return 0;
  }

#ifdef _WIN32
  {
    FARPROC fn = GetProcAddress(holder->hmod, symbol);
    if (fn != NULL) { lua_pushcclosure(L, (lua_CFunction)(void *)fn, 0); return 0; }
  }
#else
  {
    void *fn = dlsym(holder->hmod, symbol);
    if (fn != NULL) { lua_pushcclosure(L, (lua_CFunction)fn, 0); return 0; }
  }
#endif

  /* The named entry is absent.  The reference falls back to a luaopen_*
  ** derivation (strip up to the last '-', '.' becomes '_') resolved against
  ** the module, then the host process, then the caller's own image, and
  ** loads the result as a raw bytecode chunk. */
  {
    char derived[256];
    const char *base = strrchr(symbol, '-');
    const char *p;
    size_t n = 0;
    void *fn = NULL;
    base = (base != NULL) ? base + 1 : symbol;
    for (p = "luaopen_"; *p != '\0' && n < sizeof(derived) - 1; p++)
      derived[n++] = *p;
    for (p = base; *p != '\0' && n < sizeof(derived) - 1; p++)
      derived[n++] = (*p == '.') ? '_' : *p;
    derived[n] = '\0';
#ifdef _WIN32
    {
      FARPROC proc = NULL;
      HMODULE host = NULL;
      if (holder->hmod != NULL)
        proc = GetProcAddress(holder->hmod, derived);
      if (proc == NULL) {
        host = GetModuleHandleA(NULL);
        if (host != NULL) proc = GetProcAddress(host, derived);
      }
      if (proc == NULL &&
          GetModuleHandleExA(6, (LPCSTR)(const void *)&glua_open_module, &host))
        proc = GetProcAddress(host, derived);
      fn = (void *)proc;
    }
#else
    fn = dlsym(RTLD_DEFAULT, derived);
#endif
    if (fn != NULL) {
      if (luaL_loadbuffer(L, (const char *)fn, 0x7fffff00, symbol) != 0)
        return 3;
      return 0;
    }
  }

  lua_pushfstring(L, "Couldn't find function in library! (%s)", symbol);
  return 2;                                  /* symbol not found */
}

/* The exported loader.  Return codes match the reference:
**  -1 load failure, -2 gmod13_open missing, 1 called, 0 fallback. */
LUALIB_API int GMOD_LoadBinaryModule(lua_State *L, const char *path)
{
  int r = glua_open_module(L, path, "gmod13_open");
  if (r == 1) return -1;
  if (r == 2) return -2;
  if (r == 0) {
    lua_pushstring(L, path);
    lua_call(L, 1, 1);
    return 1;
  }
  lua_settop(L, -2);
  return 0;
}

/* Message handler for the interface's protected calls: the raised value plus a
** traceback of where it came from, returned as the single error result. */
static int glua_error_handler(lua_State *L)
{
  char buf[512];
  const char *msg;
  if (!lua_isstring(L, 1)) {
    snprintf(buf, sizeof(buf), "(error object is a %s value)", luaL_typename(L, 1));
    msg = buf;
  } else {
    msg = lua_tostring(L, 1);
  }
  luaL_traceback(L, L, msg, 1);
  return 1;
}

static int glua_print(lua_State *L)
{
  CLuaInterface *self = (CLuaInterface *)L->luabase;
  int count = lua_gettop(L);
  int i;
  lua_getglobal(L, "tostring");
  for (i = 1; i <= count; i++) {
    const char *text;
    lua_pushvalue(L, -1);
    lua_pushvalue(L, i);
    lua_call(L, 1, 1);
    text = lua_tostring(L, -1);
    if (text == NULL) return luaL_error(L, "'tostring' must return a string to 'print'");
    if (i > 1) glua_report(self, "\t", 0);
    glua_report(self, text, 0);
    lua_pop(L, 1);
  }
  glua_report(self, "\n", 0);
  return 0;
}

/* Init: build the state and hand it to the host.
**
** Order matters and is reproduced from the reference:
**   1. adopt the game callback and create the global table object through it
**   2. clear the return slots and the per-type metatable object table
**   3. create the state, install it, write the interface back pointer
**   4. install the panic handler and keep a reference to the error handler
**   5. check the FPU precision
**   6. build the 317-entry callback-name table and keep it as the pooled object
**   7. tell the host the interface exists -- BEFORE the libraries are opened,
**      because the host may start running scripts from this callback
**   8. publish VERSION and BRANCH
**   9. open the standard libraries
**  10. strip the debug introspection entry points
**
** A host that starts loading script from step 7 sees a state whose globals are
** still bare; that is exactly what the reference does, and the scripts the
** engine loads at that point expect it.
*/
static int ci_slot_Init(CLuaInterface *self, void *cb, bool dedicated)
{
  lua_State *L;
  int i;

  if (self == NULL) return 0;
  glua_log("IFACE::Init(cb=%p dedicated=%d state=%p)", cb, (int)dedicated, (void*)self->state);

  /* 1. adopt the callback first: object creation goes through it. */
  if (cb != NULL) self->gameCallback = cb;
  if (self->state != NULL) return 1;

  /* 2. clear the slots the host will fill. */
  for (i = 0; i < 4; i++) self->returns.objs[i] = NULL;
  for (i = 0; i < 255; i++) self->metaTableBase[i] = 0;
  for (i = 0; i < 255; i++) self->metaTables[i] = NULL;
  for (i = 0; i < 32; i++) self->temp[i] = NULL;
  self->tempIndex = 0;
  self->threadedHead = self->threadedTail = NULL;
  self->threadedCount = 0;
  self->metaIDCounter = GT_TYPE_COUNT;   /* 44, the first free type id */
  self->pushedPaths = 0;
  self->pushedFiles = 0;

  /* 3. the state itself. */
  L = lua_newstate(glua_lua_alloc, NULL);
  if (glua_mainThreadId == 0) glua_mainThreadId = GetCurrentThreadId();
  if (L == NULL) {
    glua_report(self, "Unable to create new Lua State! Couldn't alloc memory?\n", 1);
    return 0;
  }
  self->state = L;
  L->luabase = self;                     /* the module-visible back pointer */

  /* 4. panic handler and the error handler every protected call runs with. */
  lua_atpanic(L, (lua_CFunction)glua_error_handler);
  lua_pushcclosure(L, (lua_CFunction)glua_error_handler, 0);
  self->errorFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);

  /* 6. the pooled-string object backing PushPooledString/GetPooledString. */
  self->pooled = gobj_create(self);
  if (self->pooled != NULL) {
    lua_createtable(L, GLUA_POOLED_COUNT, 0);
    for (i = 0; i < GLUA_POOLED_COUNT; i++) {
      lua_pushinteger(L, i);
      lua_pushstring(L, glua_pooled_strings[i]);
      lua_settable(L, -3);
    }
    gobj_setstack_any(self, self->pooled, -1);
    lua_pop(L, 1);
  }

  /* The global table object, also created through the host. */
  self->global = gobj_create(self);
  glua_log("IFACE::Init: gameCallback=%p cb_vt0=%p global=%p", self->gameCallback,
           self->gameCallback ? GLUA_OBJ_VT(self->gameCallback)[0] : 0, (void *)self->global);
  if (self->global != NULL) {
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    gobj_setstack_any(self, self->global, -1);
    lua_pop(L, 1);
  }

  /* 7. hand the interface to the host before any library exists. */
  if (self->gameCallback != NULL) {
    void **vt = *(void ***)self->gameCallback;
    if (vt != NULL && vt[6] != NULL) {
      glua_log("IFACE::-> InterfaceCreated");
      ((void (*)(void *, CLuaInterface *))vt[6])(self->gameCallback, self);
      glua_log("IFACE::<- InterfaceCreated returned");
    }
  }

  /* 8. build identity in the globals. */
  lua_pushnumber(L, 1234.0);
  lua_setfield(L, LUA_GLOBALSINDEX, "VERSION");
  lua_pushstring(L, "unknown");
  lua_setfield(L, LUA_GLOBALSINDEX, "BRANCH");

  /* 9. libraries. */
  luaL_openlibs(L);
  lua_pushcfunction(L, glua_print);
  lua_setglobal(L, "print");

  /* The reference never exposes io to scripts; GMod routes file access through
  ** its own library. */
  lua_pushnil(L);
  lua_setglobal(L, "io");

  /* Register the loader's _LOADLIB metatable (handle cache for modules). */
  luaL_newmetatable(L, "_LOADLIB");
  lua_pushcclosure(L, loadlib_close, 0);
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);

  /* A metatable for GLuaColor so PushColor/GetColor round-trip. */
  self->colorMetaID = ci_slot_CreateMetaTable(self, "Color");
  lua_pop(L, 1);

  /* 10. strip the debug introspection entry points. */
  lua_getglobal(L, "debug");
  if (lua_istable(L, -1)) {
    static const char *const stripped[] = {
      "setlocal", "setupvalue", "upvalueid", "upvaluejoin"
    };
    size_t k;
    for (k = 0; k < sizeof(stripped) / sizeof(stripped[0]); k++) {
      lua_pushnil(L);
      lua_setfield(L, -2, stripped[k]);
    }
  }
  lua_pop(L, 1);

  self->savedMetas = 1;
  glua_log("IFACE::Init done, top=%d", lua_gettop(L));
  return 1;
}

static void ci_slot_Shutdown(CLuaInterface *self)
{ glua_log_v("SLOT:ci_slot_Shutdown");
  /* Only the state is closed here.  Destroying the object wrappers first was
  ** tried and crashed on exit: the host still holds references to them while
  ** the shutdown is walking its own teardown, and closing the state is what
  ** actually releases the registry entries they point at. */
  if (self->state != NULL) {
    unsigned long t0 = (unsigned long)GetTickCount();
    lua_close(self->state);
    glua_log("shutdown: lua_close realm=%d took %lums", (int)self->realm,
             (unsigned long)GetTickCount() - t0);
    self->state = NULL;
  }
  while (self->threadedHead) {
    GLuaThreadedNode *next = self->threadedHead->next;
    free(self->threadedHead);
    self->threadedHead = next;
  }
  self->threadedTail = NULL;
  self->threadedCount = 0;
  {
    int i;
    for (i = 0; i < 255; i++) self->metaTables[i] = NULL;
  }
}

/* Drain the pending host calls.  Each entry answers a "run now" question
** through its method table and, when it says yes, is run once with this
** interface and dropped; one that says no stays queued for a later frame.
** Entries queued while the drain runs wait for the next frame, so a call that
** re-queues itself cannot spin here. */
static void glua_pump_threaded(CLuaInterface *self)
{
  GLuaThreadedNode *node = self->threadedHead;
  GLuaThreadedNode *ready = NULL, *readyTail = NULL;
  self->threadedHead = self->threadedTail = NULL;
  self->threadedCount = 0;
  while (node != NULL) {
    GLuaThreadedNode *next = node->next;
    void **vt = node->call ? *(void ***)node->call : NULL;
    node->next = NULL;
    if (vt == NULL) {
      free(node);
    } else if (vt[1] && ((int (*)(void *))vt[1])(node->call)) {
      if (readyTail) readyTail->next = node; else ready = node;
      readyTail = node;
    } else {
      if (self->threadedTail) self->threadedTail->next = node;
      else self->threadedHead = node;
      self->threadedTail = node;
      self->threadedCount++;
    }
    node = next;
  }
  while (ready != NULL) {
    GLuaThreadedNode *next = ready->next;
    void **vt = *(void ***)ready->call;
    if (vt[2]) ((void (*)(void *, CLuaInterface *))vt[2])(ready->call, self);
    free(ready);
    ready = next;
  }
}

/* DoStackCheck: the reference treats a non-empty stack at frame end as a bug
** and reports every leftover value, tagged with the realm.  A script that
** leaks stack slots usually keeps the next chunk from compiling, so this
** report is the only clue the player gets. */
static void glua_stack_check(CLuaInterface *self)
{
  lua_State *L = self->state;
  int top, i;
  char detail[1024];
  size_t used = 0;

  if (L == NULL) return;
  top = lua_gettop(L);
  if (top <= 0) return;

  detail[0] = '\0';
  {
    int limit = (top > 0x32) ? 0x32 : top;
    for (i = 1; i <= limit && used + 48 < sizeof(detail); i++) {
      const char *typeName = "UNKNOWN";
      int type = lua_type(L, i);
      const char *tn = lua_typename(L, type);
      int n = 0;
      if (tn != NULL) typeName = tn;
      n = snprintf(detail + used, sizeof(detail) - used, "[%d]> %s ", i, typeName);
      if (n > 0) used += (size_t)n;
      /* A string value is the only one worth printing; anything else would
      ** need a formatter the runtime does not have. */
      if (type == GT_STRING) {
        const char *s = lua_tostring(L, i);
        if (s != NULL) {
          n = snprintf(detail + used, sizeof(detail) - used, "%s ", s);
          if (n > 0) used += (size_t)n;
        }
      }
    }
  }

  ci_slot_ErrorNoHalt(self, "%s Lua Stack Leak [%i]!\n%s\n",
                      (self->realm == GLUA_REALM_SERVER) ? "Server" : "Client",
                      top, detail);
  glua_log("IFACE::DoStackCheck leak top=%d %s", top, detail);
}

static void ci_slot_Cycle(CLuaInterface *self)
{ glua_log_v("SLOT:ci_slot_Cycle");
  int i;
  for (i = 0; i < 4; i++) self->returns.objs[i] = NULL;
  glua_stack_check(self);
  glua_pump_threaded(self);
}

static GLuaObject *ci_slot_Global(CLuaInterface *self) { if(self->global==NULL) glua_log("Global: NULL!"); return self->global; }

static GLuaObject *ci_slot_GetObject(CLuaInterface *self, int index)
{
  GLuaObject *obj = ci_slot_NewTemporaryObject(self);
  if (obj != NULL) gobj_setstack_any(self, obj, index);
  return obj;
}

static void ci_slot_PushLuaObject(CLuaInterface *self, GLuaObject *obj)
{ gobj_push_obj(self, obj); }

static void ci_slot_PushLuaFunction(CLuaInterface *self, int (*f)(lua_State *))
{ lua_pushcclosure(self->state, f, 0); }

static void ci_slot_LuaError(CLuaInterface *self, const char *err, int index)
{ glua_log_v("SLOT:ci_slot_LuaError");
  if (index != -1) luaL_argerror(self->state, index, err);
  else ci_slot_ErrorNoHalt(self, "%s", err);
}

static void ci_slot_TypeError(CLuaInterface *self, const char *name, int index)
{ glua_log_v("SLOT:ci_slot_TypeError"); luaL_typerror(self->state, index, name); }

/* Protected call helper shared by the CallInternal family. */
/* Report a failed protected call the way the reference does: hand the message
** to the game callback (the interface prints it) and leave the stack as the
** caller expects. */
static void glua_call_failed(CLuaInterface *self)
{
  lua_State *L = self->state;
  const char *msg = lua_tostring(L, -1);
  if (msg != NULL) ci_slot_ErrorNoHalt(self, "%s\n", msg);
  lua_pop(L, 1);
}

/* CallInternal: protected call with `rets` results turned into interface
** objects.  The return slots are cleared first, an object is prepared for each
** expected result (so a slot is never NULL on success), the call runs with the
** interface's error handler, and the results are bound to their objects. */
static void ci_slot_CallInternal(CLuaInterface *self, int args, int rets)
{ glua_log_v("SLOT:ci_slot_CallInternal");
  lua_State *L = self->state;
  int i;
  if (rets > 4) {
    ci_slot_ErrorNoHalt(self, "[CLuaInterface::Call] Expecting more returns than possible\n");
    return;
  }
  /* The reference refuses a call whose target is not a function before it
  ** touches the stack, and refuses to call from a thread other than the main
  ** one: both are player-visible errors, not silent failures. */
  if (lua_type(L, -(args + 1)) != GT_FUNCTION) {
    ci_slot_ErrorNoHalt(self, "Lua tried to call non functions\n");
    return;
  }
  for (i = 0; i < 4; i++) self->returns.objs[i] = NULL;
  for (i = 0; i < rets; i++) {
    self->returns.objs[i] = ci_slot_NewTemporaryObject(self);
  }
  if (!ci_slot_CallFunctionProtected(self, args, rets, true)) { lua_settop(L, lua_gettop(L) - 1); return; }
  for (i = 0; i < rets; i++) {
    GLuaObject *obj = self->returns.objs[i];
    if (obj == NULL) obj = self->returns.objs[i] = ci_slot_NewTemporaryObject(self);
    if (obj != NULL) gobj_setstack_any(self, obj, -1);
    lua_pop(L, 1);
  }
}

static void ci_slot_CallInternalNoReturns(CLuaInterface *self, int args)
{ glua_log_v("SLOT:ci_slot_CallInternalNoReturns");
  lua_State *L = self->state;
  if (!ci_slot_CallFunctionProtected(self, args, 0, true)) { lua_settop(L, lua_gettop(L) - 1); return; }
}

static int ci_slot_CallInternalGetBool(CLuaInterface *self, int args)
{ glua_log_v("SLOT:ci_slot_CallInternalGetBool");
  lua_State *L = self->state;
  int r = 0;
  if (!ci_slot_CallFunctionProtected(self, args, 1, true)) { lua_settop(L, lua_gettop(L) - 1); return 0; }
  if (lua_type(L, -1) != GT_BOOL) {
    lua_pop(L, 1);
    return 0;
  }
  r = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return r;
}

static const char *ci_slot_CallInternalGetString(CLuaInterface *self, int args)
{ glua_log_v("SLOT:ci_slot_CallInternalGetString");
  lua_State *L = self->state;
  const char *r = NULL;
  if (!ci_slot_CallFunctionProtected(self, args, 1, true)) { lua_settop(L, lua_gettop(L) - 1); return NULL; }
  if (lua_type(L, -1) != GT_STRING) {
    lua_pop(L, 1);
    return NULL;
  }
  r = lua_tostring(L, -1);
  lua_pop(L, 1);
  return r;
}

static int ci_slot_CallInternalGet(CLuaInterface *self, int args, GLuaObject *obj)
{ glua_log_v("SLOT:ci_slot_CallInternalGet");
  lua_State *L = self->state;
  if (!ci_slot_CallFunctionProtected(self, args, 1, true)) { lua_settop(L, lua_gettop(L) - 1); return 0; }
  if (obj != NULL) gobj_setstack_any(self, obj, -1);
  lua_pop(L, 1);
  return 1;
}


static void ci_slot_NewGlobalTable(CLuaInterface *self, const char *name)
{ glua_log_v("SLOT:ci_slot_NewGlobalTable");
  lua_State *L = self->state;
  lua_createtable(L, 0, 0);
  lua_setfield(L, LUA_GLOBALSINDEX, name);
}

static GLuaObject *ci_slot_NewTemporaryObject(CLuaInterface *self)
{
  void *obj;
  int idx = ++self->tempIndex;
  if (idx > 31) { idx = 0; self->tempIndex = 0; }
  obj = self->temp[idx];
  if (obj == NULL) {
    obj = gobj_create(self);            /* host-owned object */
    self->temp[idx] = (GLuaObject *)obj;
  } else {
    gobj_unref_any(self, obj);          /* recycle: obj->UnReference() */
  }
  return (GLuaObject *)obj;
}

static int ci_slot_isUserData(CLuaInterface *self, int index)
{ glua_log_v("SLOT:ci_slot_isUserData"); return lua_type(self->state, index) == GT_USERDATA; }

static GLuaObject *ci_slot_GetMetaTableObjectName(CLuaInterface *self, const char *name, int type)
{ glua_log_v("SLOT:GetMetaTableObjectName(name=%s type=%d)", name?name:"(null)", type);
  lua_State *L = self->state;
  lua_getfield(L, LUA_REGISTRYINDEX, name);
  if (lua_type(L, -1) != GT_TABLE) {
    lua_pop(L, 1);
    if (type == -1) { glua_log_v("SLOT:GetMetaTableObjectName -> NULL (not table, type=-1)"); return NULL; }
    ci_slot_CreateMetaTableType(self, name, type);
  }
  {
    GLuaObject *obj = ci_slot_NewTemporaryObject(self);
    if (obj != NULL) GLUA_OBJ_SETSTACK(obj, -1);
    lua_pop(L, 1);
    return obj;
  }
}

static GLuaObject *ci_slot_GetMetaTableObjectIndex(CLuaInterface *self, int index)
{
  lua_State *L = self->state;
  if (!lua_getmetatable(L, index)) return NULL;
  {
    GLuaObject *obj = ci_slot_NewTemporaryObject(self);
    if (obj != NULL) GLUA_OBJ_SETSTACK(obj, -1);
    lua_pop(L, 1);
    return obj;
  }
}

static GLuaObject *ci_slot_GetReturn(CLuaInterface *self, int index)
{ glua_log_v("SLOT:ci_slot_GetReturn");
  /* An out-of-range index or an empty return slot is a host programming error,
  ** not a script error: the reference aborts the host on both.  Report them in
  ** the same words and hand back nothing, so the cause is visible here instead
  ** of surfacing later as an access violation far from this call. */
  if (index < 0 || index > 3) {
    ci_slot_ErrorNoHalt(self, "Error: Calling GetReturn with an invalid index %d! Check code!!", index);
    return NULL;
  }
  if (self->returns.objs[index] == NULL) {
    ci_slot_ErrorNoHalt(self, "Error: Calling GetReturn on an invalid return! Check code!!");
    return NULL;
  }
  return self->returns.objs[index];
}

static int ci_slot_IsServer(CLuaInterface *self) { glua_log_v("SLOT:ci_slot_IsServer"); return self->realm == GLUA_REALM_SERVER; }
static int ci_slot_IsClient(CLuaInterface *self) { glua_log_v("SLOT:ci_slot_IsClient"); return self->realm == GLUA_REALM_CLIENT; }
static int ci_slot_IsMenu(CLuaInterface *self) { glua_log_v("SLOT:ci_slot_IsMenu"); return self->realm == GLUA_REALM_MENU; }

static void ci_slot_DestroyObject(CLuaInterface *self, GLuaObject *obj)
{ glua_log_v("SLOT:ci_slot_DestroyObject");
  if (obj == NULL) return;
  /* Host objects are owned by the engine callback; never free them here. */
  gobj_destroy(self, obj);
}

static GLuaObject *ci_slot_CreateObject(CLuaInterface *self) { return gobj_create(self); }
/* SetMember delegates to the object's own virtual methods so it works for
** host objects too.  The value to store is already on the top of the stack
** when the reference calls these (the caller pushed it).
*/
static void ci_slot_SetMemberStrObj(CLuaInterface *self, GLuaObject *t, const char *k, GLuaObject *v)
{
  lua_State *L = self->state;
  if (t == NULL || !gobj_istable_any(t)) return;
  gobj_push_obj(self, t);               /* table */
  lua_pushstring(L, k);                 /* key */
  if (v) gobj_push_obj(self, v); else lua_pushnil(L);
  lua_settable(L, -3);
  lua_pop(L, 1);
}

static void ci_slot_SetMemberStr(CLuaInterface *self, GLuaObject *t, const char *k)
{
  lua_State *L = self->state;
  if (t == NULL || !gobj_istable_any(t)) return;
  gobj_push_obj(self, t);
  lua_pushstring(L, k);
  lua_pushvalue(L, -3);                 /* value that was on top */
  lua_settable(L, -3);
  lua_pop(L, 2);
}

static void ci_slot_SetMemberNumObj(CLuaInterface *self, GLuaObject *t, float k, GLuaObject *v)
{
  lua_State *L = self->state;
  if (t == NULL) return;
  gobj_push_obj(self, t);
  lua_pushnumber(L, k);
  if (v) gobj_push_obj(self, v); else lua_pushnil(L);
  lua_settable(L, -3);
  lua_pop(L, 1);
}

static void ci_slot_SetMemberNum(CLuaInterface *self, GLuaObject *t, float k)
{
  lua_State *L = self->state;
  if (t == NULL) return;
  gobj_push_obj(self, t);
  lua_pushnumber(L, k);
  lua_pushvalue(L, -3);
  lua_settable(L, -3);
  lua_pop(L, 2);
}

static void ci_slot_SetMemberObjObj(CLuaInterface *self, GLuaObject *t, GLuaObject *k, GLuaObject *v)
{
  lua_State *L = self->state;
  if (t == NULL || k == NULL) return;
  gobj_push_obj(self, t);
  gobj_push_obj(self, k);
  if (v) gobj_push_obj(self, v); else lua_pushnil(L);
  lua_settable(L, -3);
  lua_pop(L, 1);
}

static GLuaObject *ci_slot_GetNewTable(CLuaInterface *self)
{
  lua_State *L = self->state;
  GLuaObject *obj;
  lua_createtable(L, 0, 0);
  obj = ci_slot_NewTemporaryObject(self);
  if (obj != NULL) GLUA_OBJ_SETSTACK(obj, -1);
  lua_pop(L, 1);
  return obj;
}

static void ci_slot_SetType(CLuaInterface *self, unsigned char realm) { self->realm = realm; }
static void ci_slot_PushLong(CLuaInterface *self, long num)
{ lua_pushnumber(self->state, (double)num); }

/* GetFlags: the reference ORs the numeric fields of the object's flag table.
** An object whose value is not a table answers 0; a table is walked with the
** host object's own iteration methods so engine-owned objects work too. */
static int ci_slot_GetFlags(CLuaInterface *self, int index)
{ glua_log_v("SLOT:ci_slot_GetFlags");
  lua_State *L = self->state;
  int flags = 0;
  int type;
  if (L == NULL) return 0;
  type = lua_type(L, index);
  /* The argument may be the flags themselves or a list of them. */
  if (type == LUA_TNUMBER) return (int)lua_tonumber(L, index);
  if (type != LUA_TTABLE) return 0;
  /* lua_next walks from an absolute position; the push below would shift a
  ** negative index under it. */
  if (index < 0) index = lua_gettop(L) + index + 1;
  if (index <= 0) return 0;
  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    /* Only the values carry flags; the keys are the list positions. */
    if (lua_isnumber(L, -1)) flags |= (int)lua_tonumber(L, -1);
    lua_pop(L, 1);
  }
  return flags;
}

static int ci_slot_FindOnObjectsMetaTable(CLuaInterface *self, int obj, int key)
{ glua_log_v("SLOT:ci_slot_FindOnObjectsMetaTable");
  lua_State *L = self->state;
  if (!lua_getmetatable(L, obj)) { lua_pushnil(L); return 0; }
  lua_pushvalue(L, key);
  lua_gettable(L, -2);
  lua_remove(L, -2);
  return lua_type(L, -1) != 0;
}

static int ci_slot_FindObjectOnTable(CLuaInterface *self, int tbl, int key)
{ glua_log_v("SLOT:ci_slot_FindObjectOnTable");
  lua_State *L = self->state;
  lua_pushvalue(L, tbl);
  lua_pushvalue(L, key);
  lua_gettable(L, -2);
  lua_remove(L, -2);
  return lua_type(L, -1) != 0;
}

static void ci_slot_SetMemberFast(CLuaInterface *self, GLuaObject *t, int k, int v)
{ glua_log_v("SLOT:ci_slot_SetMemberFast");
  lua_State *L = self->state;
  if (t == NULL) return;
  gobj_push_obj(self, t);
  lua_pushvalue(L, k);
  lua_pushvalue(L, v);
  lua_settable(L, -3);
  lua_pop(L, 1);
}

/* Compile + optionally run a string.  Bytecode (leading 0x1b) is refused,
** matching the reference. */
/* Assemble the DEFINE_BASECLASS sugar into "local BaseClass = baseclass.Get".
** The replacement is the same length or longer, which keeps line numbers
** intact for the error messages. */
static char *glua_rewrite_baseclass(const char *src)
{
  const char *needle = "DEFINE_BASECLASS";
  const char *repl = "local BaseClass = baseclass.Get";
  const size_t nlen = strlen(needle), rlen = strlen(repl);
  size_t len = strlen(src), count = 0, cap;
  const char *p;
  char *out, *q;
  for (p = src; (p = strstr(p, needle)) != NULL; p += nlen) count++;
  if (count == 0) return NULL;
  cap = len + 1 + count * (rlen > nlen ? rlen - nlen : 0);
  out = (char *)malloc(cap);
  if (out == NULL) return NULL;
  q = out;
  for (p = src; *p; ) {
    if (strncmp(p, needle, nlen) == 0) { memcpy(q, repl, rlen); q += rlen; p += nlen; }
    else *q++ = *p++;
  }
  *q = '\0';
  return out;
}

/* RunStringEx: compile and run a chunk.
**
** Contract reproduced from the reference:
**   - a null or empty body is refused with "Not running script <name> - it's
**     too short."; a body starting with the bytecode escape is refused with
**     "Cannot run byte code!"
**   - the chunk name is the caller's name with a leading '@' normalised, so a
**     traceback prints "file:line" rather than the raw name
**   - a compile failure reports through the game callback's Lua error slot,
**     tagged with the realm, and optionally leaves the message on the stack
**   - a run failure goes through the interface's protected-call slot, then the
**     same reporting path
**   - the globals SCRIPTNAME and SCRIPTPATH are cleared before the chunk runs
**     so a script cannot read a stale value from the previous one
*/
extern int opglua_report_lua_error(void *callback, const char *message, const char *side);

static void glua_report_script_error(CLuaInterface *self, const char *message)
{
  const char *side = self->realm == GLUA_REALM_SERVER ? "server"
                   : self->realm == GLUA_REALM_MENU ? "menu" : "client";
  if (!opglua_report_lua_error(self->gameCallback, message, side))
    glua_report(self, message ? message : "Unknown Lua error", 1);
}

static int glua_run_ex(CLuaInterface *self, const char *filename, const char *path,
                       const char *str, bool run, bool printErrors,
                       bool dontPushErrors, bool noReturns)
{ lua_State *L;
  char cn[1024];
  char *rewritten = NULL;
  const char *body;
  const char *name;
  int base;
  int loaded;

  if (self == NULL) return 0;
  L = self->state;
  if (L == NULL) return 0;

  glua_log_v("IFACE::RunString(fn=%s run=%d len=%d)", filename ? filename : "(null)",
           (int)run, str ? (int)strlen(str) : -1);

  /* A body that is absent or empty is not a compile error: it is refused with
  ** its own message, and the caller decides whether it becomes a stack value. */
  if (str == NULL || str[0] == '\0') {
    ci_slot_ErrorNoHalt(self, "Not running script %s - it's too short.\n",
                        filename ? filename : "");
    if (!dontPushErrors) ci_slot_PushString(self, "Invalid script - or too short.", 0);
    return 0;
  }
  if (str[0] == '\033') {
    ci_slot_ErrorNoHalt(self, "Cannot run byte code! %x\n", 0x1b);
    if (!dontPushErrors) ci_slot_PushString(self, "Cannot run byte code!", 0);
    return 0;
  }

  body = str;
  rewritten = glua_rewrite_baseclass(str);
  if (rewritten != NULL) body = rewritten;

  /* Chunk names carry '@': a leading one already present is not doubled. */
  name = (filename != NULL) ? filename : "";
  if (name[0] == '@') name++;
  if (path != NULL && path[0] != '\0') snprintf(cn, sizeof(cn), "@%s%s", path, name);
  else snprintf(cn, sizeof(cn), "@%s", name);

  base = lua_gettop(L);
  loaded = luaL_loadbufferx(L, body, strlen(body), cn, "t");
  free(rewritten);

  if (loaded != 0) {
    const char *msg = lua_tostring(L, -1);
    glua_log("IFACE::RunString COMPILE-ERROR fn=%s: %s", filename ? filename : "(null)",
             msg ? msg : "(no message)");
    if (printErrors) glua_report_script_error(self, msg);
    if (dontPushErrors) lua_pop(L, 1);
    return 0;
  }

  if (!run) return 1;

  /* The reference clears the two script-identity globals around the run. */
  lua_pushnil(L); lua_setfield(L, LUA_GLOBALSINDEX, "SCRIPTNAME");
  lua_pushnil(L); lua_setfield(L, LUA_GLOBALSINDEX, "SCRIPTPATH");

  if (!ci_slot_CallFunctionProtected(self, 0, LUA_MULTRET, false)) {
    const char *msg = lua_tostring(L, -1);
    glua_log("IFACE::RunString RUNTIME-ERROR fn=%s: %s", filename ? filename : "(null)",
             msg ? msg : "(no message)");
    if (printErrors) glua_report_script_error(self, msg);
    if (dontPushErrors || noReturns) lua_settop(L, base);
    return 0;
  }

  if (noReturns) lua_settop(L, base);

  lua_pushnil(L); lua_setfield(L, LUA_GLOBALSINDEX, "SCRIPTNAME");
  lua_pushnil(L); lua_setfield(L, LUA_GLOBALSINDEX, "SCRIPTPATH");

  glua_log_v("IFACE::RunString OK fn=%s", filename ? filename : "(null)");
  return 1;
}

static int ci_slot_RunString(CLuaInterface *self, const char *fn, const char *path,
                             const char *str, bool run, bool showErrors)
{ glua_log_v("SLOT:ci_slot_RunString"); return glua_run_ex(self, fn, path, str, run, showErrors, true, true); }

static int ci_slot_RunStringEx(CLuaInterface *self, const char *fn, const char *path,
                               const char *str, bool run, bool printErrors,
                               bool dontPushErrors, bool noReturns)
{ glua_log_v("SLOT:ci_slot_RunStringEx"); return glua_run_ex(self, fn, path, str, run, printErrors, dontPushErrors, noReturns); }

static int ci_slot_IsEqual(CLuaInterface *self, GLuaObject *a, GLuaObject *b)
{ glua_log_v("SLOT:ci_slot_IsEqual");
  lua_State *L = self->state;
  int r;
  if (a == NULL || b == NULL) return 0;
  if (gobj_gettype_any(a) != gobj_gettype_any(b)) return 0;
  gobj_push_obj(self, a);
  gobj_push_obj(self, b);
  r = lua_equal(L, -1, -2);
  lua_pop(L, 2);
  return r;
}

static void ci_slot_Error(CLuaInterface *self, const char *err)
{ glua_log_v("SLOT:ci_slot_Error"); luaL_error(self->state, "%s", err ? err : ""); }

static const char *ci_slot_GetStringOrError(CLuaInterface *self, int index)
{ glua_log_v("SLOT:ci_slot_GetStringOrError");
  const char *s = lua_tostring(self->state, index);
  if (s == NULL) {
    ci_slot_TypeError(self, "string", index);
    return "";
  }
  return s;
}

/* Load includes/modules/<name>.lua once, caching in _MODULES. */
static int ci_slot_RunLuaModule(CLuaInterface *self, const char *name)
{ glua_log_v("SLOT:ci_slot_RunLuaModule"); return ci_slot_FindAndRunScript(self, name, true, true, "!MODULE", false); }

/* Resolve the game root from this DLL's own location:
**   <root>/bin/win64/lua_shared.dll  ->  <root>
** so script paths can be found relative to the install even when the process
** working directory is something else. */
static const char *glua_game_root(void)
{
  static char root[512];
  static int tried = 0;
  if (!tried) {
    char mod[512];
    char *p;
    tried = 1;
    root[0] = '\0';
#ifdef _WIN32
    {
      HMODULE self = NULL;
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             (LPCSTR)(const void *)&glua_game_root, &self) && self != NULL &&
          GetModuleFileNameA(self, mod, (DWORD)sizeof(mod)) != 0) {
        for (p = mod; *p; p++) if (*p == '\\') *p = '/';
        /* strip "/bin/win64/lua_shared.dll" (or "/bin/lua_shared.dll") */
        p = strstr(mod, "/bin/");
        if (p != NULL) { *p = '\0'; snprintf(root, sizeof(root), "%s", mod); }
      }
    }
#endif
  }
  return root;
}

static FILE *glua_open_script_id(CLuaInterface *self, const char *name,
                                 const char *pathID, char *outPath, size_t outSize);

/* Open a script by its engine-relative name.  Like the reference, a relative
** name first resolves against the current include directory (the path stack
** top), then against the conventional roots under the install. */
static FILE *glua_open_script(CLuaInterface *self, const char *fn, char *outPath, size_t outSize)
{
  const char *root = glua_game_root();
  const char *curdir = (self != NULL && self->pushedPaths > 0) ? self->pathStack[self->pushedPaths - 1] : NULL;
  char cand[1024];
  const char *roots[5];
  int i, n = 0;
  if (curdir != NULL && curdir[0] != '\0') {
    snprintf(cand, sizeof(cand), "%s%s", curdir, fn);
    { FILE *f = fopen(cand, "rb"); if (f) { if (outPath) snprintf(outPath, outSize, "%s", cand); return f; } }
  }
  if (root[0] != '\0') {
    /* The engine's file system exposes several search roots; emulate them. */
    static const char *subroots[] = {
      "/garrysmod/lua/",
      "/garrysmod/gamemodes/",
      "/garrysmod/",
      "/lua/",
      "/"
    };
    int k;
    for (k = 0; k < (int)(sizeof(subroots)/sizeof(subroots[0])); k++) {
      FILE *f;
      snprintf(cand, sizeof(cand), "%s%s%s", root, subroots[k], fn);
      f = fopen(cand, "rb");
      if (f) { if (outPath) snprintf(outPath, outSize, "%s", cand); return f; }
    }
  }
  roots[n++] = "garrysmod/lua/";
  roots[n++] = "lua/";
  roots[n++] = "";
  for (i = 0; i < n; i++) {
    FILE *f;
    snprintf(cand, sizeof(cand), "%s%s", roots[i], fn);
    f = fopen(cand, "rb");
    if (f) { if (outPath) snprintf(outPath, outSize, "%s", cand); return f; }
  }
  return NULL;
}

/* File-read helper exported for the C++ ILuaShared bridge: resolve a script
** name against the same roots FindAndRunScript uses and return its bytes in a
** malloc'd buffer.  Returns the byte length, or 0 when not found. */
int opglua_read_script(const char *name, char **outData)
{
  char full[1024];
  FILE *f;
  char *buf;
  long size;
  if (outData) *outData = NULL;
  if (name == NULL) return 0;
  f = glua_open_script_id(NULL, name, NULL, full, sizeof(full));
  if (f == NULL) return 0;
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) { fclose(f); return 0; }
  buf = (char *)malloc((size_t)size + 1);
  if (buf == NULL) { fclose(f); return 0; }
  if (fread(buf, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(buf); return 0; }
  buf[size] = '\0';
  fclose(f);
  if (outData) *outData = buf;
  else free(buf);
  return (int)size;
}

/* ------------------------------------------------------------------------ */
/* Lua script search roots.                                                 */
/*                                                                          */
/* The engine registers its search directories with the ILuaShared mounting  */
/* slots (MountLua / MountLuaAdd) before it asks for any script, and every   */
/* later request (LoadFile, ScriptExists, FindScripts) carries the same path */
/* id.  Keeping those registrations is what makes a name resolve against the */
/* directories the engine actually mounted for that id -- in particular the  */
/* gamemode's own "entities" and "weapons" folders, which is how the scripted */
/* entity and weapon scripts become reachable at all.                        */

#define GLUA_MAX_SCRIPT_ROOTS   128
#define GLUA_MAX_PATHIDS        16

/* Report-callback used by the directory walk below: (context, entry name,
** is folder).  Kept as a plain function pointer because the C side owns the
** walk and the C++ side owns the result vector it fills. */
typedef void (*glua_find_cb)(void *ctx, const char *name, int isFolder);

typedef struct GLuaScriptRoot {
  char name[260];                       /* engine-relative directory */
  int  pathID;                          /* index into glua_pathid_names */
} GLuaScriptRoot;

static GLuaScriptRoot glua_script_roots[GLUA_MAX_SCRIPT_ROOTS];
static int glua_script_root_count = 0;
static char glua_pathid_names[GLUA_MAX_PATHIDS][40];
static int glua_pathid_count = 0;

static int glua_tolower_ascii(int c)
{
  return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int glua_pathid_index(const char *id, int create)
{
  int i;
  if (id == NULL) id = "";
  for (i = 0; i < glua_pathid_count; i++) {
    if (glua_stricmp(glua_pathid_names[i], id) == 0) return i;
  }
  if (!create || glua_pathid_count >= GLUA_MAX_PATHIDS) return -1;
  snprintf(glua_pathid_names[glua_pathid_count], sizeof(glua_pathid_names[0]), "%s", id);
  return glua_pathid_count++;
}

static void glua_script_root_add(const char *name, const char *pathID)
{
  int id;
  int i;
  if (name == NULL || name[0] == '\0') return;
  id = glua_pathid_index(pathID, 1);
  if (id < 0) return;
  for (i = 0; i < glua_script_root_count; i++) {
    if (glua_script_roots[i].pathID == id &&
        glua_stricmp(glua_script_roots[i].name, name) == 0) return;
  }
  if (glua_script_root_count >= GLUA_MAX_SCRIPT_ROOTS) return;
  snprintf(glua_script_roots[glua_script_root_count].name,
           sizeof(glua_script_roots[0].name), "%s", name);
  glua_script_roots[glua_script_root_count].pathID = id;
  glua_script_root_count++;
}

static void glua_script_root_remove(const char *pathID)
{
  int id = glua_pathid_index(pathID, 0);
  int i, w = 0;
  if (id < 0) return;
  for (i = 0; i < glua_script_root_count; i++) {
    if (glua_script_roots[i].pathID != id) {
      if (w != i) glua_script_roots[w] = glua_script_roots[i];
      w++;
    }
  }
  glua_script_root_count = w;
}

static int glua_dir_exists(const char *path)
{
#ifdef _WIN32
  DWORD a = GetFileAttributesA(path);
  return (a != INVALID_FILE_ATTRIBUTES) && ((a & FILE_ATTRIBUTE_DIRECTORY) != 0);
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* Content directory: <install>/garrysmod, or the install itself when the
** content lives directly under it.  Every engine-relative name is resolved
** below this point. */
static const char *glua_content_root(void)
{
  static char buf[512];
  static int tried = 0;
  if (!tried) {
    const char *root = glua_game_root();
    tried = 1;
    snprintf(buf, sizeof(buf), "%s/garrysmod", root);
    if (!glua_dir_exists(buf)) snprintf(buf, sizeof(buf), "%s", root);
  }
  return buf;
}

/* One candidate for one root: try the directory of the currently running
** include (relative names inside it), then the root under the content
** directory, then the name directly under the content directory. */
static FILE *glua_try_root(CLuaInterface *self, const char *root, const char *name,
                           char *outPath, size_t outSize)
{
  char cand[1024];
  FILE *f;
  const char *content = glua_content_root();
  const char *curdir = (self != NULL && self->pushedPaths > 0)
                         ? self->pathStack[self->pushedPaths - 1] : NULL;
  if (curdir != NULL && curdir[0] != '\0') {
    snprintf(cand, sizeof(cand), "%s%s", curdir, name);
    f = fopen(cand, "rb");
    if (f) { if (outPath) snprintf(outPath, outSize, "%s", cand); return f; }
  }
  if (root != NULL && root[0] != '\0') {
    snprintf(cand, sizeof(cand), "%s/%s/%s", content, root, name);
    f = fopen(cand, "rb");
    if (f) { if (outPath) snprintf(outPath, outSize, "%s", cand); return f; }
  }
  snprintf(cand, sizeof(cand), "%s/%s", content, name);
  f = fopen(cand, "rb");
  if (f) { if (outPath) snprintf(outPath, outSize, "%s", cand); return f; }
  return NULL;
}

/* Resolve a script name for a path id: every root registered for that id in
** registration order, then the conventional roots.  NULL pathID means "try
** every registered root". */
static FILE *glua_open_script_id(CLuaInterface *self, const char *name,
                                 const char *pathID, char *outPath, size_t outSize)
{
  int id = (pathID != NULL) ? glua_pathid_index(pathID, 0) : -1;
  int i;
  FILE *f;
  if (name == NULL) return NULL;
  for (i = 0; i < glua_script_root_count; i++) {
    if (id >= 0 && glua_script_roots[i].pathID != id) continue;
    f = glua_try_root(self, glua_script_roots[i].name, name, outPath, outSize);
    if (f != NULL) return f;
  }
  if (id >= 0) {
    for (i = 0; i < glua_script_root_count; i++) {
      if (glua_script_roots[i].pathID == id) continue;
      f = glua_try_root(self, glua_script_roots[i].name, name, outPath, outSize);
      if (f != NULL) return f;
    }
  }
  return glua_open_script(self, name, outPath, outSize);
}

/* Engine file system answers (lib_luashared.cpp): the interface's search paths
** carry the mounted packs and every root the mount slots recorded.  Each one
** returns 0 / "no" when no engine file system is bound, so the local walk
** below always stays usable as the fallback. */
extern int opglua_luashared_readfile(const char *name, const char *pathID, char **outData);
extern int opglua_luashared_fileexists(const char *name, const char *pathID);
extern int opglua_luashared_isdirectory(const char *name, const char *pathID);

/* Read a script by name for a path id.  Returns the byte length, or 0. */
int opglua_read_script_id(const char *name, const char *pathID, char **outData)
{
  char full[1024];
  FILE *f;
  char *buf;
  long size;
  if (outData) *outData = NULL;
  if (name == NULL) return 0;
  {
    int n = opglua_luashared_readfile(name, pathID, outData);
    if (n > 0) return n;
  }
  f = glua_open_script_id(NULL, name, pathID, full, sizeof(full));
  if (f == NULL) return 0;
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) { fclose(f); return 0; }
  buf = (char *)malloc((size_t)size + 1);
  if (buf == NULL) { fclose(f); return 0; }
  if (fread(buf, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(buf); return 0; }
  buf[size] = '\0';
  fclose(f);
  if (outData) *outData = buf;
  else free(buf);
  return (int)size;
}

int opglua_script_exists_id(const char *name, const char *pathID)
{
  char full[1024];
  FILE *f;
  if (name == NULL) return 0;
  if (opglua_luashared_fileexists(name, pathID)) return 1;
  f = glua_open_script_id(NULL, name, pathID, full, sizeof(full));
  if (f == NULL) return 0;
  fclose(f);
  return 1;
}

/* Resolve a name for a path id and report whether it is a directory.  The
** reference's existence test also answers "yes" for a directory, which is what
** lets the engine probe "entities/foo" before it asks for a file inside it. */
int opglua_script_isdir_id(const char *name, const char *pathID)
{
  char full[1024];
  FILE *f;
  (void)pathID;
  if (name == NULL) return 0;
  if (opglua_luashared_isdirectory(name, pathID)) return 1;
  f = glua_open_script_id(NULL, name, pathID, full, sizeof(full));
  if (f != NULL) { fclose(f); return 0; }
  /* No file opened: the name may still be a directory.  Try the same roots the
  ** reader walks, without the file test. */
  {
    const char *content = glua_content_root();
    int i;
    char cand[1024];
    if (glua_dir_exists(name)) return 1;
    for (i = 0; i < glua_script_root_count; i++) {
      if (pathID != NULL) {
        int id = glua_pathid_index(pathID, 0);
        if (id >= 0 && glua_script_roots[i].pathID != id) continue;
      }
      snprintf(cand, sizeof(cand), "%s/%s/%s", content, glua_script_roots[i].name, name);
      if (glua_dir_exists(cand)) return 1;
    }
    snprintf(cand, sizeof(cand), "%s/%s", content, name);
    if (glua_dir_exists(cand)) return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------------ */
/* Directory enumeration for ILuaShared::FindScripts.                       */

/* Case-insensitive wildcard match, '*' and '?'. */
static int glua_wildmatch(const char *pat, const char *str)
{
  while (*pat) {
    if (*pat == '*') {
      while (*pat == '*') pat++;
      if (*pat == '\0') return 1;
      while (*str) {
        if (glua_wildmatch(pat, str)) return 1;
        str++;
      }
      return 0;
    }
    if (*str == '\0') return 0;
    if (*pat != '?' && glua_tolower_ascii((unsigned char)*pat) !=
                        glua_tolower_ascii((unsigned char)*str)) return 0;
    pat++;
    str++;
  }
  return *str == '\0';
}

/* Names already reported during one FindScripts call: a name mounted twice
** (content directory and an addon) is handed to the engine only once. */
typedef struct GLuaFindSeen {
  char names[512][128];
  int  count;
} GLuaFindSeen;

static int glua_find_seen(GLuaFindSeen *seen, const char *name)
{
  int i;
  for (i = 0; i < seen->count; i++) {
    if (glua_stricmp(seen->names[i], name) == 0) return 1;
  }
  if (seen->count < 512) {
    snprintf(seen->names[seen->count], sizeof(seen->names[0]), "%s", name);
    seen->count++;
  }
  return 0;
}

/* Report every entry of one directory whose base name matches the pattern. */
static int glua_emit_dir(const char *dir, const char *wild,
                         GLuaFindSeen *seen, void *ctx, glua_find_cb cb)
{
  int total = 0;
  const char *w = (wild != NULL && wild[0] != '\0') ? wild : "*";
#ifdef _WIN32
  char pat[1024];
  WIN32_FIND_DATAA fd;
  HANDLE h;
  snprintf(pat, sizeof(pat), "%s/%s", dir, w);
  h = FindFirstFileA(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    int isFolder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
    if (glua_find_seen(seen, fd.cFileName)) continue;
    cb(ctx, fd.cFileName, isFolder);
    total++;
  } while (FindNextFileA(h, &fd));
  FindClose(h);
#else
  DIR *d = opendir(dir);
  struct dirent *e;
  if (d == NULL) return 0;
  while ((e = readdir(d)) != NULL) {
    char full[1024];
    int isFolder;
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    if (!glua_wildmatch(w, e->d_name)) continue;
    if (glua_find_seen(seen, e->d_name)) continue;
    snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
    isFolder = glua_dir_exists(full);
    cb(ctx, e->d_name, isFolder);
    total++;
  }
  closedir(d);
#endif
  return total;
}

/* Walk every root registered for the path id and report the matching entries
** of <path>.  The pattern splits into a directory part and a wildcard part the
** same way the engine's file system does ("entities/*" -> directory
** "entities", wildcard "*"), and the callback receives the entry name as it
** appears inside that directory. */
int opglua_find_scripts(const char *path, const char *pathID,
                        void *ctx, glua_find_cb cb)
{
  char dir[512];
  const char *slash;
  const char *wild;
  const char *content = glua_content_root();
  GLuaFindSeen seen;
  int id = (pathID != NULL) ? glua_pathid_index(pathID, 0) : -1;
  int total = 0;
  int i;

  if (path == NULL || cb == NULL) return 0;
  memset(&seen, 0, sizeof(seen));

  slash = strrchr(path, '/');
  if (slash == NULL) {
    dir[0] = '\0';
    wild = path;
  } else {
    size_t n = (size_t)(slash - path);
    if (n >= sizeof(dir)) n = sizeof(dir) - 1;
    memcpy(dir, path, n);
    dir[n] = '\0';
    wild = slash + 1;
  }

  for (i = 0; i < glua_script_root_count; i++) {
    char full[1024];
    if (id >= 0 && glua_script_roots[i].pathID != id) continue;
    if (dir[0] != '\0') {
      snprintf(full, sizeof(full), "%s/%s/%s", content, glua_script_roots[i].name, dir);
    } else {
      snprintf(full, sizeof(full), "%s/%s", content, glua_script_roots[i].name);
    }
    total += glua_emit_dir(full, wild, &seen, ctx, cb);
  }

  /* The conventional root covers the stock layout even when the engine did not
  ** mount anything for this id yet. */
  {
    char full[1024];
    if (dir[0] != '\0') snprintf(full, sizeof(full), "%s/lua/%s", content, dir);
    else                snprintf(full, sizeof(full), "%s/lua", content);
    total += glua_emit_dir(full, wild, &seen, ctx, cb);
  }
  glua_log_v("FindScripts: %s (%s) -> %d", path, pathID ? pathID : "(null)", total);
  return total;
}

/* Existence probe for ILuaShared::ScriptExists. */
int opglua_script_exists(const char *name)
{
  return opglua_script_exists_id(name, NULL);
}

extern int opglua_luashared_readscript(const char *path, const char *pathID,
                                        bool isClient, bool isServer,
                                        char **dataOut, char *chunkOut, size_t chunkSize);

/* FindAndRunScript: resolve a script through the shared script cache and run
** the bytes that cache recorded.
**
** The reference resolves through ILuaShared::LoadFile rather than reading the
** disk itself, bumps the per-realm load counters the cache object carries,
** writes the caller's marker into the entry, and runs the recorded bytes using
** the cache's own name as the chunk name.  The order matters: an include made
** by the running script reaches the same cache entry, and the marker is what
** the loader heuristics read.
**
** Two spellings are tried: the name as given, then the path stack prefix plus
** the name.  A name without an extension gets ".lua" appended; a name with any
** other extension is refused before a lookup happens.
*/
static int ci_slot_FindAndRunScript(CLuaInterface *self, const char *fn, bool run,
                                    bool showErrors, const char *marker, bool noReturns)
{ glua_log_v("SLOT:ci_slot_FindAndRunScript");
  char norm[512];
  char pathID[64];
  char candidate[1024];
  char chunk[512];
  char *buf = NULL;
  const char *prefix;
  int n;
  int attempt;
  int ok = 0;
  size_t i;

  if (self == NULL || fn == NULL) return 0;
  glua_log_v("IFACE::FindAndRunScript(%s)", fn);

  snprintf(norm, sizeof(norm), "%s", fn);
  for (i = 0; norm[i]; i++) if (norm[i] == '\\') norm[i] = '/';

  {
    const char *dot = NULL;
    size_t len = strlen(norm);
    for (i = len; i > 0; i--) {
      if (norm[i - 1] == '.') { dot = norm + i - 1; break; }
      if (norm[i - 1] == '/') break;
    }
    if (dot == NULL) {
      if (len + 4 < sizeof(norm)) memcpy(norm + len, ".lua", 5);
    } else if (glua_stricmp(dot, ".lua") != 0) {
      if (showErrors) ci_slot_ErrorNoHalt(self, "Couldn't include file '%s' - Not a .lua file! (%s)\n", fn, norm);
      return 0;
    }
  }

  snprintf(pathID, sizeof(pathID), "%s", self->pathID);
  prefix = ci_slot_GetPath(self);

  for (attempt = 0; attempt < 2 && !ok; attempt++) {
    if (attempt == 0 || prefix == NULL || prefix[0] == '\0') {
      if (attempt == 1) break;
      snprintf(candidate, sizeof(candidate), "%s", norm);
    } else {
      /* The recorded directory already ends in a separator, so joining must
      ** not add another one: a doubled slash changes the file name the script
      ** sees and breaks lookups that compare against a literal path. */
      size_t plen = strlen(prefix);
      int sep = (plen > 0 && prefix[plen - 1] == '/') ? 0 : 1;
      snprintf(candidate, sizeof(candidate), "%s%s%s", prefix, sep ? "/" : "", norm);
    }
    chunk[0] = '\0';
    n = opglua_luashared_readscript(candidate, pathID[0] ? pathID : NULL,
                                    (self->realm == GLUA_REALM_CLIENT),
                                    (self->realm == GLUA_REALM_SERVER),
                                    &buf, chunk, sizeof(chunk));
    if (n > 0 && buf != NULL) ok = 1;
    else if (buf != NULL) { free(buf); buf = NULL; }
  }

  if (!ok) {
    glua_log("IFACE::FindAndRunScript MISS '%s'", norm);
    if (showErrors) ci_slot_ErrorNoHalt(self, "Couldn't include file '%s' - File not found or is empty (%s)\n", fn, candidate);
    return 0;
  }

  glua_log_v("IFACE::FindAndRunScript got '%s' chunk '%s' len=%d", candidate, chunk, n);

  ci_slot_PushPath(self, candidate);
  if (self->pushedFiles < GLUA_PATH_DEPTH) {
    snprintf(self->fileStack[self->pushedFiles], sizeof(self->fileStack[0]), "%s", chunk);
    self->pushedFiles++;
  }
  ok = glua_run_ex(self, chunk, "", buf, run, showErrors, true, noReturns);
  if (self->pushedFiles > 0) self->pushedFiles--;
  ci_slot_PopPath(self);
  free(buf);
  (void)marker;
  return ok;
}

/* The host hands over its own 32-byte string object rather than a C string:
** short values are stored inline and longer ones behind a pointer in the
** first eight bytes, with the character count at offset 16.  Reading the
** object as a C string would copy the pointer itself for long paths, so the
** length field decides which form this is. */
static void glua_copy_host_string(char *dst, size_t dstSize, const void *src)
{
  const unsigned char *p = (const unsigned char *)src;
  const char *text;
  size_t len;
  if (dst == NULL || dstSize == 0) return;
  if (p == NULL) { dst[0] = 0; return; }
  len = *(const size_t *)(p + 16);
  if (len < 0x1000) {
    text = (len < 16) ? (const char *)p : *(const char * const *)p;
    if (text == NULL) { dst[0] = 0; return; }
  } else {
    strncpy(dst, (const char *)p, dstSize - 1);
    dst[dstSize - 1] = 0;
    return;
  }
  if (len > dstSize - 1) len = dstSize - 1;
  memcpy(dst, text, len);
  dst[len] = 0;
}

static void ci_slot_SetPathID(CLuaInterface *self, const char *id)
{ glua_log_v("SLOT:ci_slot_SetPathID");
  glua_copy_host_string(self->pathID, sizeof(self->pathID), id);
}

static const char *ci_slot_GetPathID(CLuaInterface *self) { glua_log_v("SLOT:ci_slot_GetPathID"); return self->pathID; }

static void glua_vmsg(CLuaInterface *self, const char *fmt, va_list ap, int errHalt)
{
  char buf[4096];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  glua_log("LUA_MSG(err=%d): %s", errHalt, buf);
  glua_report(self, buf, errHalt);
}

/* Lua error reporter: assemble the message and the traceback the way the
** reference does and hand the text to the interface that owns the state.  The
** stack shape is left untouched.  Returns 0, which is what the host reads. */
int opglua_lua_error_report(lua_State *L)
{
  char msg[1024];
  char trace[3072];
  CLuaInterface *self;
  if (L == NULL) return 0;

  msg[0] = '\0';
  if (lua_gettop(L) > 0 && !lua_isstring(L, -1)) {
    const char *t = luaL_typename(L, -1);
    snprintf(msg, sizeof(msg), "(%s)", t ? t : "unknown");
  } else if (lua_gettop(L) > 0) {
    snprintf(msg, sizeof(msg), "%s", lua_tostring(L, -1));
  }
  if (msg[0] == '\0') snprintf(msg, sizeof(msg), "Unknown Lua error");
  if (lua_gettop(L) > 0) {
    strncat(msg, "\n", sizeof(msg) - strlen(msg) - 1);
    luaL_traceback(L, L, msg, 1);
    snprintf(trace, sizeof(trace), "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  } else {
    snprintf(trace, sizeof(trace), "%s", msg);
  }

  self = (CLuaInterface *)L->luabase;
  glua_log("AdvancedLuaErrorReporter: %s", trace);
  glua_log_flush();
  if (self != NULL && self->state == L) {
    glua_report(self, trace, 0);
  }
  return 0;
}

static void ci_slot_ErrorNoHalt(CLuaInterface *self, const char *fmt, ...)
{ glua_log_v("SLOT:ci_slot_ErrorNoHalt");
  va_list ap;
  va_start(ap, fmt);
  glua_vmsg(self, fmt, ap, 1);
  va_end(ap);
}

static void ci_slot_Msg(CLuaInterface *self, const char *fmt, ...)
{ glua_log_v("SLOT:ci_slot_Msg");
  va_list ap;
  va_start(ap, fmt);
  glua_vmsg(self, fmt, ap, 0);
  va_end(ap);
}

/* PushPath: the reference stores the DIRECTORY derived from the path it is
** given (it runs the name through the file system to canonicalize, then strips
** the trailing file component).  GetPath returns that directory; includes
** resolve against it.  We keep a small stack. */
static void glua_dirname_of(const char *path, char *out, size_t outSize)
{
  size_t n = strlen(path);
  size_t i;
  if (outSize == 0) return;
  out[0] = '\0';
  for (i = n; i > 0; i--) {
    if (path[i-1] == '/' || path[i-1] == '\\') {
      if (i == n) { /* path already ends in a separator: keep as-is */ }
      if (i < outSize) { memcpy(out, path, i); out[i] = '\0'; }
      return;
    }
  }
  /* no separator: the directory is the current one */
  snprintf(out, outSize, "./");
}

static void ci_slot_PushPath(CLuaInterface *self, const char *path)
{ glua_log_v("SLOT:ci_slot_PushPath(%s)", path?path:"(null)");
  if (self->pushedPaths < GLUA_PATH_DEPTH) {
    char norm[260];
    size_t i;
    snprintf(norm, sizeof(norm), "%s", path ? path : "");
    for (i = 0; norm[i]; i++) if (norm[i] == '\\') norm[i] = '/';
    glua_dirname_of(norm, self->pathStack[self->pushedPaths], sizeof(self->pathStack[0]));
    self->pushedPaths++;
  }
}

static void ci_slot_PopPath(CLuaInterface *self)
{ glua_log_v("SLOT:ci_slot_PopPath");
  if (self->pushedPaths <= 0) {
    ci_slot_ErrorNoHalt(self, "CLuaInterface::PopPath - Overpopped!\n");
    self->pushedPaths--;
    return;
  }
  self->pushedPaths--;
}

static const char *ci_slot_GetPath(CLuaInterface *self)
{ glua_log_v("SLOT:ci_slot_GetPath");
  return self->pushedPaths > 0 ? self->pathStack[self->pushedPaths - 1] : NULL;
}

/* GetColor: the reference reads a Color object (a table with r/g/b/a) and
** packs it as a little-endian RGBA int; a non-table yields 0xFFFFFFFF. */
/* GetColor: reads a Color table into the packed int the reference produces,
** whose byte order is (low to high) alpha, blue, green, red.  A value that is
** not a table answers -1, i.e. all four bytes set, which is what the reference
** writes into the out parameter. */
/* The host passes the destination in the second argument and the stack
** position in the third, and expects the four channel bytes written into
** that buffer with the buffer handed back.  A value that is not a table
** yields an opaque white. */
static void *ci_slot_GetColor(CLuaInterface *self, void *out, int index)
{ glua_log_v("SLOT:ci_slot_GetColor");
  lua_State *L = self->state;
  unsigned char *p = (unsigned char *)out;
  unsigned char r = 0xff, g = 0xff, b = 0xff, a = 0xff;
  if (p == NULL) return out;
  if (L == NULL || glua_gettype(L, index) != GT_TABLE) {
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
    return out;
  }
  lua_getfield(L, index, "r");
  if (lua_isnumber(L, -1)) r = (unsigned char)lua_tonumber(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, index, "g");
  if (lua_isnumber(L, -1)) g = (unsigned char)lua_tonumber(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, index, "b");
  if (lua_isnumber(L, -1)) b = (unsigned char)lua_tonumber(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, index, "a");
  if (lua_isnumber(L, -1)) a = (unsigned char)lua_tonumber(L, -1);
  lua_pop(L, 1);
  p[0] = r; p[1] = g; p[2] = b; p[3] = a;
  return out;
}
/* PushColor: build a Color table and give it the Color metatable.  The packed
** byte order the reference uses is alpha, blue, green, red from the lowest
** byte, and the fields are written in that order.
*/
static void *ci_slot_PushColor(CLuaInterface *self, GLuaColor color)
{ glua_log_v("SLOT:ci_slot_PushColor");
  lua_State *L = self->state;
  GLuaObject *colorMeta;
  if (L == NULL) return NULL;
  lua_createtable(L, 0, 4);
  lua_pushnumber(L, (double)color.a); lua_setfield(L, -2, "a");
  lua_pushnumber(L, (double)color.b); lua_setfield(L, -2, "b");
  lua_pushnumber(L, (double)color.g); lua_setfield(L, -2, "g");
  lua_pushnumber(L, (double)color.r); lua_setfield(L, -2, "r");
  colorMeta = ci_slot_GetMetaTableObjectName(self, "Color", -1);
  if (colorMeta != NULL) {
    gobj_push_obj(self, colorMeta);
    lua_setmetatable(L, -2);
  }
  return NULL;
}

static int ci_slot_GetStack(CLuaInterface *self, int level, lua_Debug *dbg)
{ glua_log_v("SLOT:ci_slot_GetStack"); return lua_getstack(self->state, level, dbg); }
static int ci_slot_GetInfo(CLuaInterface *self, const char *what, lua_Debug *dbg)
{ glua_log_v("SLOT:ci_slot_GetInfo"); return lua_getinfo(self->state, what, dbg); }
static const char *ci_slot_GetLocal(CLuaInterface *self, lua_Debug *dbg, int n)
{ glua_log_v("SLOT:ci_slot_GetLocal"); return lua_getlocal(self->state, dbg, n); }
static const char *ci_slot_GetUpvalue(CLuaInterface *self, int funcIndex, int n)
{ glua_log_v("SLOT:ci_slot_GetUpvalue"); return lua_getupvalue(self->state, funcIndex, n); }

/* GetDataString: the out pointer is cleared first, so a caller that gets 0
** back cannot read a stale pointer.  The length is truncated to 32 bits, which
** is what the interface ships.
*/
static size_t ci_slot_GetDataString(CLuaInterface *self, int index, const char **str)
{ glua_log_v("SLOT:ci_slot_GetDataString");
  size_t len = 0;
  const char *s;
  if (str) *str = NULL;
  if (self == NULL || self->state == NULL) return 0;
  s = lua_tolstring(self->state, index, &len);
  if (s == NULL) return 0;
  if (str) *str = s;
  return (size_t)((unsigned int)len & 0xffffffffu);
}

static void ci_slot_ErrorFromLua(CLuaInterface *self, const char *fmt, ...)
{ glua_log_v("SLOT:ci_slot_ErrorFromLua");
  va_list ap;
  va_start(ap, fmt);
  glua_vmsg(self, fmt, ap, 1);
  va_end(ap);
}

static const char *ci_slot_GetCurrentLocation(CLuaInterface *self)
{ glua_log_v("SLOT:ci_slot_GetCurrentLocation");
  static char buf[512];
  lua_State *L = self->state;
  lua_Debug ar;
  int level;
  snprintf(buf, sizeof(buf), "<nowhere>");
  for (level = 0; level < 2; level++) {
    if (lua_getstack(L, level, &ar) != 0) {
      lua_getinfo(L, "Sl", &ar);
      if (ar.currentline > 0) {
        snprintf(buf, sizeof(buf), "%s (line %d)", ar.short_src, ar.currentline);
        break;
      }
    }
  }
  return buf;
}

static void ci_slot_MsgColour(CLuaInterface *self, const GLuaColor *col, const char *fmt, ...)
{ glua_log_v("SLOT:ci_slot_MsgColour");
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  glua_report_colour(self, buf, col);
}

/* Implemented in lib_luashared.cpp: fill a caller std::string& out param. */
extern void opglua_stdstring_clear(void *dest);
extern void opglua_stdstring_assign(void *dest, const char *data, unsigned int len);

/* GetCurrentFile: walk the Lua call stack for the first frame whose source
** begins with '@' and hand back the source without that marker.  A stack with
** no such frame leaves the string empty, which is what the reference does.
** Frames that come from C (no source line) are skipped.
*/
static void ci_slot_GetCurrentFile(CLuaInterface *self, void *outStr)
{ glua_log_v("SLOT:ci_slot_GetCurrentFile");
  lua_State *L;
  lua_Debug ar;
  int level;
  if (outStr == NULL) return;
  opglua_stdstring_clear(outStr);
  if (self == NULL || self->state == NULL) return;
  L = self->state;
  for (level = 1; level <= 9; level++) {
    if (lua_getstack(L, level, &ar) == 0) return;
    lua_getinfo(L, "Sl", &ar);
    if (ar.currentline < 1) continue;
    if (ar.source == NULL || ar.source[0] != '@') continue;
    opglua_stdstring_assign(outStr, ar.source + 1,
                            (unsigned int)strlen(ar.source + 1));
    return;
  }
}

/* The Source std::string layout, used for the strings the engine passes by
** reference (Bootil::Buffer contents, GetCurrentFile, CompileString input).
** A 16-byte inline buffer, then size and capacity; a longer value keeps its
** bytes on the engine heap.
*/
typedef struct GmodStdString {
  union { char buf[16]; char *ptr; } bx;
  size_t size;
  size_t res;
} GmodStdString;

/* Appends through the engine's own allocator so the caller can destroy the
** buffer it owns. */
extern int opglua_stdstring_append(void *dest, const char *data, size_t len);

static const char *gmodstd_data(const GmodStdString *s)
{
  if (s == NULL) return "";
  return (s->res >= 16 && s->bx.ptr != NULL) ? s->bx.ptr : s->bx.buf;
}

/* CompileString: compile a chunk and dump its bytecode into the caller's
** buffer.  The buffer belongs to the engine (Bootil::Buffer), and its string
** member uses the Source std::string layout, so the dump appends through that
** layout instead of assuming a C++ type from another module.
*/
static int glua_dump_append(const void *data, size_t size, void *ud)
{
  GmodStdString *dest = (GmodStdString *)ud;
  if (dest == NULL || data == NULL) return 0;
  return opglua_stdstring_append(dest, (const char *)data, size) ? 0 : 1;
}

static int ci_slot_CompileString(CLuaInterface *self, void *dumper, const void *str)
{ glua_log_v("SLOT:ci_slot_CompileString");
  lua_State *L;
  const GmodStdString *src;
  int rc;
  if (self == NULL || self->state == NULL || str == NULL) return 0;
  L = self->state;
  src = (const GmodStdString *)str;
  rc = luaL_loadbufferx(L, gmodstd_data(src), src->size, "", "t");
  if (rc != 0) {
    lua_pop(L, 1);
    return 0;
  }
  rc = lua_dump(L, (lua_Writer)glua_dump_append, dumper);
  lua_pop(L, 1);
  return rc == 0;
}

/* Protected call with the error handler this interface installed at boot: the
** handler reference is pushed under the function so a runtime error is turned
** into a formatted message before it unwinds, then removed again.  Returns
** true on success; a failure leaves the message on the stack for the caller to
** report and discard. */
static int ci_slot_CallFunctionProtected(CLuaInterface *self, int args, int rets, bool printErr)
{ glua_log_v("SLOT:ci_slot_CallFunctionProtected");
  (void)glua_onMainThread();   /* Report the calling thread and continue the call. */
  lua_State *L = self->state;
  int index;
  int r;
  if (L == NULL || args < 0) return 0;
  index = lua_gettop(L) - args;
  if (self->errorFuncRef != 0) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->errorFuncRef);
    lua_insert(L, index);
  }
  r = lua_pcall(L, args, rets, self->errorFuncRef != 0 ? index : 0);
  if (self->errorFuncRef != 0) lua_remove(L, index);
  if (r != 0) {
    if (printErr) {
      const char *msg = lua_tostring(L, -1);
      /* The reference names the case where the error value has no text. */
      if (msg == NULL) msg = "CallFunctionProtected was passed a nil as the error message!";
      ci_slot_ErrorNoHalt(self, "%s", msg);
    }
    return 0;
  }
  return 1;
}

/* Binary module naming.  The realm prefix follows IsClient (the reference:
** gmcl_ when the realm is Client, gmsv_ otherwise -- the menu realm uses the
** sv prefix too).  The suffix and extension are compile-time properties of
** this build, exactly as the reference hardcodes them. */
static const char *glua_module_prefix(CLuaInterface *self)
{ return self->realm == GLUA_REALM_CLIENT ? "gmcl_" : "gmsv_"; }

static const char *glua_module_suffix(void)
{
#if defined(_WIN64)
  return "win64";
#elif defined(_WIN32)
  return "win32";
#elif defined(__ANDROID__)
  return "android64";
#elif defined(__aarch64__) || defined(__x86_64__)
  return "linux64";
#elif defined(__APPLE__)
  return "osx64";
#else
  return "linux";
#endif
}

static const char *glua_module_ext(void)
{
#ifdef _WIN32
  return ".dll";
#else
  return ".so";
#endif
}

/* Game directory used to resolve module paths.  The reference resolves via
** the engine file system; a standalone host sets this registry value (or the
** OPGLUA_GAMEDIR environment variable). */
static const char *glua_game_dir(lua_State *L)
{
  const char *dir = NULL;
  static char fallback[512];
  lua_getfield(L, LUA_REGISTRYINDEX, "OPGLUA_GAMEDIR");
  if (lua_type(L, -1) == GT_STRING) dir = lua_tostring(L, -1);
  lua_pop(L, 1);
  if (dir == NULL || dir[0] == '\0') {
    dir = getenv("OPGLUA_GAMEDIR");
  }
  if (dir == NULL || dir[0] == '\0') {
    /* No host override: derive <install>/garrysmod from this DLL's location. */
    const char *root = glua_game_root();
    if (root[0] != '\0') {
      snprintf(fallback, sizeof(fallback), "%s/garrysmod", root);
      dir = fallback;
    }
  }
  return dir ? dir : "";
}

/* Load a binary module file and call its gmod13_open (mirrors the reference
** path builder in the Require slot).  Returns 1 on success. */
static int glua_load_binary_module(CLuaInterface *self, const char *fullpath, const char *entry)
{
  lua_State *L = self->state;
  int base = lua_gettop(L);
  int result = GMOD_LoadBinaryModule(L, fullpath);
  (void)entry;
  if (result != 1) {
    const char *message = lua_tostring(L, -1);
    return luaL_error(L, "%s", message ? message : "Couldn't load binary module");
  }
  lua_settop(L, base);
  return 1;
}

static int glua_file_exists(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (f == NULL) return 0;
  fclose(f);
  return 1;
}

/* Require: try the Lua module file, then fall back to the binary module, the
** same order the reference uses inside this slot. */
/* Require(name): load a Lua or binary module exactly once per realm.
**
** The reference keeps a `_MODULES` table in the globals; an entry that is not
** nil means the module already ran and Require returns immediately.  Otherwise
** it probes `lua/includes/modules/<name>.lua` with the file system, falling
** back to the binary module in `lua/bin/<gmcl_|gmsv_><name>_<arch>.<ext>`,
** and finally to a loader module by the same name.  A name that reaches none
** of those paths raises "Module not found!".
**
** The entry is stored as a boolean, which is what the nil test above reads.
*/
static void ci_slot_Require(CLuaInterface *self, const char *name)
{ glua_log_v("SLOT:ci_slot_Require");
  lua_State *L;
  char path[1024];
  const char *dir;
  int found;

  if (self == NULL || name == NULL || name[0] == '\0') return;
  L = self->state;
  if (L == NULL) return;
  glua_log("IFACE::Require(%s)", name);

  /* _MODULES table, created once. */
  lua_getglobal(L, "_MODULES");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_createtable(L, 0, 0);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "_MODULES");
  }

  /* Already loaded?  An entry that is anything but nil short-circuits. */
  lua_getfield(L, -1, name);
  if (!lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return;
  }
  lua_pop(L, 1);
  lua_pop(L, 1);

  dir = glua_game_dir(L);
  found = 0;

  /* 1. the Lua module file */
  snprintf(path, sizeof(path), "includes/modules/%s.lua", name);
  if (opglua_script_exists_id(path, self->pathID[0] ? self->pathID : NULL)) {
    found = ci_slot_FindAndRunScript(self, path, true, true, "!MODULE", true);
    if (found) glua_log("IFACE::Require(%s) ran the Lua module", name);
  }

  /* 2. the binary module */
  if (!found) {
    snprintf(path, sizeof(path), "%s/lua/bin/%s%s_%s%s", dir,
             glua_module_prefix(self), name, glua_module_suffix(), glua_module_ext());
    if (glua_file_exists(path)) {
      found = glua_load_binary_module(self, path, name) ? 1 : 0;
      if (found) glua_log("IFACE::Require(%s) loaded the binary module", name);
    }
  }

  /* 3. a loader module by the same name */
  if (!found) {
    found = ci_slot_RunLuaModule(self, name);
    if (found) glua_log("IFACE::Require(%s) ran the loader module", name);
  }

  if (!found) {
    /* The reference raises this rather than returning quietly: a script that
    ** requires a missing module must fail at the call site. */
    luaL_error(L, "Module not found! (%s)", name);
    return;
  }

  lua_getglobal(L, "_MODULES");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, name);
  lua_pop(L, 1);
}

static const char *ci_slot_GetActualTypeName(CLuaInterface *self, int type)
{ glua_log_v("SLOT:ci_slot_GetActualTypeName"); return lua_typename(self->state, type); }

static void ci_slot_PreCreateTable(CLuaInterface *self, int arr, int narr)
{ glua_log_v("SLOT:ci_slot_PreCreateTable"); lua_createtable(self->state, arr, narr); }

static void ci_slot_PushPooledString(CLuaInterface *self, int index)
{
  lua_State *L = self->state;
  const char *str = (index >= 0 && index < GLUA_POOLED_COUNT) ? glua_pooled_strings[index] : NULL;
  glua_log_v("SLOT:ci_slot_PushPooledString(%d -> %s)", index, str ? str : "(null)");
  if (str != NULL)
    lua_pushstring(L, str);
  else
    lua_pushnil(L);
}

static const char *ci_slot_GetPooledString(CLuaInterface *self, int index)
{
  (void)self;
  /* Out of range answers NULL, not an empty string: callers test the pointer,
  ** and an empty name is a valid pooled entry in the table. */
  if (index >= 0 && index < GLUA_POOLED_COUNT) return glua_pooled_strings[index];
  return NULL;
}

static int ci_slot_AddThreadedCall(CLuaInterface *self, void *call)
{
  GLuaThreadedNode *node = (GLuaThreadedNode *)malloc(sizeof(*node));
  if (node == NULL) return luaL_error(self->state, "Unable to allocate a pending Lua call");
  node->call = call;
  node->next = NULL;
  if (self->threadedTail) self->threadedTail->next = node;
  else self->threadedHead = node;
  self->threadedTail = node;
  return ++self->threadedCount;
}

/* Appends the call stack to whatever the caller already wrote into the
** buffer; it never clears it, because the reason the stack is wanted is
** usually already there. */
static void ci_slot_AppendStackTrace(CLuaInterface *self, char *buf, unsigned int size)
{ glua_log_v("SLOT:ci_slot_AppendStackTrace");
  lua_State *L = self->state;
  lua_Debug ar;
  int level = 0;
  if (buf == NULL || size == 0) return;
  if (L == NULL) { strncat(buf, "   Lua State = NULL\n\n", size - strlen(buf) - 1); return; }
  if (lua_getstack(L, 0, &ar) == 0) {
    strncat(buf, "\t*Not in Lua call OR Lua has panicked*\n", size - strlen(buf) - 1);
    return;
  }
  while (level < 17 && lua_getstack(L, level, &ar) != 0) {
    char line[512];
    size_t used;
    lua_getinfo(L, "Slnu", &ar);
    snprintf(line, sizeof(line), "%d. %s - %s:%d\n",
             level, ar.name ? ar.name : "?", ar.short_src, ar.currentline);
    used = strlen(buf);
    if (used + strlen(line) + 1 >= size) break;
    strncat(buf, line, size - used - 1);
    level++;
  }
}

/* Implemented in lib_convar.cpp: build the engine ConVar/ConCommand so the
** returned pointer has the real layout the engine patches and self-registers
** with ICVar, exactly as the reference does. */
extern void *glua_convars_create_convar(const char *name, const char *def,
                                        const char *help, int flags);
extern void *glua_convars_create_concommand(const char *name, const char *help,
                                            int flags, void *cb, void *completion);

static unsigned glua_console_realm_flags(CLuaInterface *self, unsigned flags)
{
  flags &= 0xf3fffff3u;
  if (self->realm == GLUA_REALM_SERVER) flags |= 0x08000004u;
  if (self->realm == GLUA_REALM_CLIENT) flags |= 0x14000008u;
  if (self->realm == GLUA_REALM_MENU) flags &= ~128u;
  return flags;
}

static void *ci_slot_CreateConVar(CLuaInterface *self, const char *name, const char *def,
                                  const char *help, int flags)
{ glua_log_v("SLOT:ci_slot_CreateConVar(name=%s def=%s help=%s flags=%d/%#x)", name?name:"(null)", def?def:"(null)", help?help:"(null)", flags, flags);
  return glua_convars_create_convar(name, def, help,
                                    (int)glua_console_realm_flags(self, (unsigned)flags));
}

static void *ci_slot_CreateConCommand(CLuaInterface *self, const char *name, const char *help,
                                      int flags, void *cb, void *completion)
{ glua_log_v("SLOT:ci_slot_CreateConCommand(name=%s flags=%d)", name?name:"(null)", flags);
  unsigned realm_flags = glua_console_realm_flags(self, (unsigned)flags);
  if (self->realm == GLUA_REALM_SERVER) realm_flags |= 0x40000000u;
  if (name && (!strcmp(name, "gmod_undo") || !strcmp(name, "gm_spawn")))
    glua_log("console command realm=%d name=%s flags=%08x", (int)self->realm, name, realm_flags);
  return glua_convars_create_concommand(name, help, (int)realm_flags, cb, completion);
}

static const char *ci_slot_CheckStringOpt(CLuaInterface *self, int idx, const char *def)
{ glua_log_v("SLOT:ci_slot_CheckStringOpt"); return luaL_optlstring(self->state, idx, def, NULL); }

static double ci_slot_CheckNumberOpt(CLuaInterface *self, int idx, double def)
{ glua_log_v("SLOT:ci_slot_CheckNumberOpt"); return luaL_optnumber(self->state, idx, def); }

static int ci_slot_RegisterMetaTable(CLuaInterface *self, const char *name, GLuaObject *tbl)
{ glua_log_v("SLOT:ci_slot_RegisterMetaTable(name=%s)", name?name:"(null)");
  lua_State *L = self->state;
  int id;
  lua_getfield(L, LUA_REGISTRYINDEX, name);
  if (lua_type(L, -1) == GT_TABLE) {    /* already registered: preserve its id */
    lua_getfield(L, -1, "MetaID");
    id = (int)lua_tonumber(L, -1);
    lua_pop(L, 2);                      /* pop MetaID and old table */
  } else {
    lua_pop(L, 1);
    id = self->metaIDCounter++;
  }
  if (tbl != NULL) gobj_push_obj(self, tbl); else lua_newtable(L);
  lua_pushnumber(L, (double)id);
  lua_setfield(L, -2, "MetaID");
  lua_pushstring(L, name);
  lua_setfield(L, -2, "MetaName");
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, name);
  if (id >= 0 && id < 255) {
    if (self->metaTables[id] == NULL) self->metaTables[id] = gobj_create(self);
    if (self->metaTables[id] != NULL) gobj_setstack_any(self, self->metaTables[id], -1);
  }
  /* Leave registered metatable on stack (-1) per GMod contract */
  return id;
}

static void ci_slot_dtor(CLuaInterface *self)
{ glua_log_v("SLOT:ci_slot_dtor");
  if (self == NULL) return;
  if (self->state != NULL) {
    lua_close(self->state);
    self->state = NULL;
  }
}

/* ------------------------------------------------------------------------ */
/* CreateInterface: the reference interface factory.  The engine does
** CreateInterface("LUASHARED003") to get the ILuaShared object and
** CreateInterface("LUACONVARS001") for ILuaConVars.  Unknown names return
** NULL with the standard return code, like the reference list walk. */

/* Realm slots.  The reference keeps one interface pointer per realm in an
** array at a fixed place in the shared object, and both the create slot and
** the get slot address that same array; callers that read the array through
** the object rather than through the table must find the same answers, so the
** member order is fixed here. */
typedef struct CLuaShared {
  const void *vtable;                    /* +0x00 */
  void *factory;                         /* +0x08 engine interface factory */
  bool dedicated;                        /* +0x10 */
  void *filesystem;                      /* +0x18 VFileSystem022, or NULL */
  unsigned char reserved[0x78 - 0x20];   /* +0x20 fixed array placement */
  CLuaInterface *ifaces[3];              /* +0x78 client, server, menu */
  unsigned char reserved2[0xb0 - 0x90]; /* +0x90 gap up to the hook slot */
  void *findHook;                        /* +0xb0 resolution hook */
} CLuaShared;

/* Forward views: the shared object reaches the ConVar side when it tears the
** last realm down, and the factory answers are process-wide singletons. */
typedef struct CLuaConVars CLuaConVars;
static CLuaShared *glua_shared_for_convars = NULL;
static CLuaConVars *glua_convars_singleton = NULL;

typedef char glua_iface_array_offset_check[
  (offsetof(CLuaShared, ifaces) == 0x78) ? 1 : -1];

static void cs_dtor(CLuaShared *s) { (void)s; }

/* Init(interfaceFactory, magicBool, context, pGet): the engine hands over its
** CreateInterface.  The reference asks it for the ConVar system and for the
** file system, keeps both, and only then lets any interface be created -- the
** file system is what every script lookup goes through afterwards.  A missing
** file system is not fatal: the module keeps its own directory walk. */
extern void opglua_convars_set_cvar(void *cvar);
extern void opglua_convars_register(void);
extern void opglua_mem_bind(void);
extern void opglua_luashared_setfilesystem(void *fs);
extern void opglua_luashared_setcvar(void *cv);
extern void opglua_luashared_setcontext(void *ctx);

static void cs_Init(CLuaShared *s, void *f, bool b, void *ctx, void *get)
{ glua_log("ILuaShared::Init(factory=%p ctx=%p)", f, ctx); s->factory = f; s->dedicated = b; (void)get;
  opglua_mem_bind();
  opglua_luashared_setcontext(ctx);
  s->filesystem = NULL;
  if (f != NULL) {
    void *(*factory)(const char *, int *) = (void *(*)(const char *, int *))f;
    /* Pull VEngineCvar007 like the reference so the ConVar module's g_pCVar is
    ** bound to the engine's ICVar.  factory(name, int* out). */
    void *cvar = factory("VEngineCvar007", NULL);
    void *fs = factory("VFileSystem022", NULL);
    glua_log("ILuaShared::Init cvar=%p filesystem=%p", cvar, fs);
    opglua_convars_set_cvar(cvar);
    opglua_convars_register();
    opglua_luashared_setcvar(cvar);
    s->filesystem = fs;
    opglua_luashared_setfilesystem(fs);
  } else {
    opglua_luashared_setfilesystem(NULL);
    opglua_luashared_setcvar(NULL);
  }
  glua_boot_marker("ILuaShared::Init");
}

static void cs_Shutdown(CLuaShared *s)
{ glua_log("ILuaShared::Shutdown");
  int i;
  for (i = 0; i < 3; i++) {
    if (s->ifaces[i]) { ci_slot_Shutdown(s->ifaces[i]); free(s->ifaces[i]); s->ifaces[i] = NULL; }
  }
  opglua_luashared_setfilesystem(NULL);
  s->filesystem = NULL;
  glua_log_flush();
}
/* DumpStats: report the script-cache counters.  The reference prints this
** block with these labels, so anything reading the console keeps working. */
static void cs_DumpStats(CLuaShared *s)
{ CLuaInterface *out;
  int files = 0, hits = 0, failed = 0, ok = 0, disk = 0;
  glua_log("ILuaShared::DumpStats");
  opglua_luashared_cache_stats(&files, &hits, &failed, &ok, &disk);
  /* Only an interface that went through Init carries a host callback, so a
  ** realm that exists but has no state yet must not be used as a message
  ** sink.  The figures always go to the log. */
  out = s->ifaces[GLUA_REALM_CLIENT];
  if (out != NULL && out->state == NULL) out = NULL;
  if (out == NULL) { out = s->ifaces[GLUA_REALM_SERVER]; if (out != NULL && out->state == NULL) out = NULL; }
  if (out == NULL) { out = s->ifaces[GLUA_REALM_MENU];   if (out != NULL && out->state == NULL) out = NULL; }
  glua_log("Lua File Stats: files=%d hits=%d failed=%d ok=%d disk=%d",
           files, hits, failed, ok, disk);
  if (out == NULL) return;
  ci_slot_Msg(out, "Lua File Stats ------\n");
  ci_slot_Msg(out, "Files In Cache: %i\n", files);
  ci_slot_Msg(out, "m_iCacheHits: %i\n", hits);
  ci_slot_Msg(out, "m_iFailedLoads: %i\n", failed);
  ci_slot_Msg(out, "m_iSuccessfulLoads: %i\n", ok);
  ci_slot_Msg(out, "m_iDiskLoads: %i\n", disk);
}

static CLuaInterface *cs_CreateLuaInterface(CLuaShared *s, unsigned char realm, bool unknown)
{
  CLuaInterface *iface;
  (void)unknown;
  glua_log("CreateLuaInterface(realm=%d)", (int)realm);
  iface = (CLuaInterface *)calloc(1, sizeof(CLuaInterface));
  if (iface == NULL) return NULL;
  iface->vtable = (const void *)glua_interface_vtable;
  iface->realm = realm;
  /* Allocate only.  The engine's follow-up Init(gameCallback) creates the
  ** state and is what hands the interface back through the callback. */
  if (realm < 3) s->ifaces[realm] = iface;
  glua_boot_marker("CreateLuaInterface");
  glua_log_flush();
  return iface;
}

static void cs_CloseLuaInterface(CLuaShared *s, CLuaInterface *iface)
{ glua_log("ILuaShared::CloseLuaInterface(%p)",(void*)iface);
  int i;
  int clientLeft, serverLeft;
  if (iface == NULL) return;
  for (i = 0; i < 3; i++) if (s->ifaces[i] == iface) s->ifaces[i] = NULL;
  ci_slot_Shutdown(iface);
  free(iface);
  /* When neither game realm is left, the reference tears down the managed
  ** cvars the ConVar module created for scripts.  Leaving them would keep
  ** stale ConVar objects registered with the engine across a map change. */
  clientLeft = (s->ifaces[GLUA_REALM_CLIENT] != NULL);
  serverLeft = (s->ifaces[GLUA_REALM_SERVER] != NULL);
  if (!clientLeft && !serverLeft && glua_convars_singleton != NULL) {
    void **vt = *(void ***)glua_convars_singleton;
    if (vt != NULL && vt[4] != NULL) {
      glua_log("ILuaShared::CloseLuaInterface -> DestroyManaged");
      ((void (*)(void *))vt[4])((void *)glua_convars_singleton);
    }
  }
}

static CLuaInterface *cs_GetLuaInterface(CLuaShared *s, unsigned char realm)
{
  CLuaInterface *r;
  glua_log_v("ILuaShared::GetLuaInterface(realm=%d)", (int)realm);
  r = (realm < 3) ? s->ifaces[realm] : NULL;
  glua_log_v("ILuaShared::GetLuaInterface -> %p", (void *)r);
  return r;
}

/* ILuaShared tail (slots 7..18).  File reads, the script cache and the
** script enumeration live in lib_luashared.cpp; the mounting slots below keep
** the local root bookkeeping and hand the same roots to the engine's file
** system when one is present. */

extern void opglua_luashared_unmount(const char *pathID);

static void *cs_LoadFile(CLuaShared *s, const void *path, const void *pathId,
                         bool fromDatatable, bool fromFile)
{ (void)s;
  void *r = opglua_luashared_loadfile(path, pathId, fromDatatable, fromFile);
  glua_log_v("ILuaShared::LoadFile -> %p", r);
  return r;
}

/* MountLua(pathID): the engine's per-path-id root set.  The reference mounts
** "lua" for every id and additionally "gamemodes" for every id except the
** menu's, both through the same add-one-root slot, so the local bookkeeping
** and the engine's search paths stay in step.  The menu comparison is exact:
** the engine always spells that id the same way. */
static void cs_MountLuaAdd(CLuaShared *s, const char *file, const char *pathID);

static void cs_MountLua(CLuaShared *s, const char *pathID)
{ (void)s;
  glua_log("ILuaShared::MountLua(%s)", pathID ? pathID : "(null)");
  if (pathID == NULL) return;
  cs_MountLuaAdd(s, "lua", pathID);
  if (strcmp(pathID, "LuaMenu") != 0) cs_MountLuaAdd(s, "gamemodes", pathID);
}

/* MountLuaAdd(file, pathID): one explicit root.  The reference resolves the
** directory, refuses it when it is not a directory, and adds it to the path
** id's search set; the engine side of that happens in lib_luashared.cpp and
** the local walk records the same name either way, so a path id that the file
** system does not know still resolves here. */
static void cs_MountLuaAdd(CLuaShared *s, const char *file, const char *pathID)
{ (void)s;
  glua_log("ILuaShared::MountLuaAdd(%s, %s)", file ? file : "(null)", pathID ? pathID : "(null)");
  if (file == NULL || file[0] == '\0' || pathID == NULL) return;
  glua_script_root_add(file, pathID);
  opglua_luashared_mount_add(file, pathID);
}

/* UnMountLua(pathID): drop the id's roots on both sides. */
static void cs_UnMountLua(CLuaShared *s, const char *pathID)
{ (void)s;
  glua_log("ILuaShared::UnMountLua(%s)", pathID ? pathID : "(null)");
  if (pathID == NULL) return;
  glua_script_root_remove(pathID);
  opglua_luashared_unmount(pathID);
}
static void cs_SetFileContents(CLuaShared *s, const char *a, const char *b) { (void)s; (void)a; (void)b; }
static void cs_SetLuaFindHook(CLuaShared *s, void *hook)
{
  if (s == NULL) return;
  s->findHook = hook;
  glua_log("ILuaShared::SetLuaFindHook hook=%p vtable=%p", hook,
           hook ? *(void ***)hook : NULL);
}

void glua_luashared_trace(const char *event, const char *a,
                                     const char *b, int x, int y)
{
  glua_log_v("LuaSharedTrace event=%s a=%s b=%s x=%d y=%d",
           event ? event : "", a ? a : "", b ? b : "", x, y);
}

/* FindScripts(path, pathID, out): enumerate the matching entries of `path`
** over every root mounted for `pathID` and append them to the caller's
** result vector.  The engine drives this for its script folders, so an empty
** answer here is precisely what leaves scripted entities and weapons
** unregistered. */
static void cs_FindScripts(CLuaShared *s, const void *path, const void *pathID, void *out)
{
  int hookResult = opglua_luashared_findhook(s ? s->findHook : NULL, path, pathID, out);
  (void)hookResult;
  if (hookResult != 0) return;
  opglua_luashared_findscripts(path, pathID, out);
}
/* The aggregate needs the find hook and the per-realm stack walkers to be
** trustworthy first; until then answer with an empty block rather than risk
** walking a state that is being torn down. */
static const char *cs_GetStackTraces(CLuaShared *s) { (void)s; return ""; }
static void cs_InvalidateCache(CLuaShared *s, const void *unknown)
{ (void)s; opglua_luashared_invalidatecache(unknown); }
static void cs_EmptyCache(CLuaShared *s) { (void)s; opglua_luashared_emptycache(); }
static bool cs_ScriptExists(CLuaShared *s, const void *file, const void *path, bool unknown)
{ (void)s;
  bool r = opglua_luashared_scriptexists(file, path, unknown);
  glua_log_v("ILuaShared::ScriptExists -> %d", (int)r);
  return r;
}

/* GetCache(fileName) -> LuaFile* from the module cache.  A pure lookup: the
** loader fills the table, and a name that was never loaded is a miss. */
static void *cs_GetCache(CLuaShared *s, const void *fileName)
{ (void)s;
  void *r = opglua_luashared_getcache(fileName);
  glua_log_v("ILuaShared::GetCache -> %p", r);
  return r;
}

static const glua_vfn glua_shared_vtable[19] = {
  /*  0 */ (glua_vfn)cs_dtor,
  /*  1 */ (glua_vfn)cs_Init,
  /*  2 */ (glua_vfn)cs_Shutdown,
  /*  3 */ (glua_vfn)cs_DumpStats,
  /*  4 */ (glua_vfn)cs_CreateLuaInterface,
  /*  5 */ (glua_vfn)cs_CloseLuaInterface,
  /*  6 */ (glua_vfn)cs_GetLuaInterface,
  /*  7 */ (glua_vfn)cs_LoadFile,
  /*  8 */ (glua_vfn)cs_GetCache,
  /*  9 */ (glua_vfn)cs_MountLua,
  /* 10 */ (glua_vfn)cs_MountLuaAdd,
  /* 11 */ (glua_vfn)cs_UnMountLua,
  /* 12 */ (glua_vfn)cs_SetFileContents,
  /* 13 */ (glua_vfn)cs_SetLuaFindHook,
  /* 14 */ (glua_vfn)cs_FindScripts,
  /* 15 */ (glua_vfn)cs_GetStackTraces,
  /* 16 */ (glua_vfn)cs_InvalidateCache,
  /* 17 */ (glua_vfn)cs_EmptyCache,
  /* 18 */ (glua_vfn)cs_ScriptExists
};

/* ILuaConVars: the engine asks for this too.  Both creation slots build a real
** ConVar/ConCommand through the bridge, which self-registers with the engine's
** cvar system -- the same objects the interface slots hand out. */
typedef struct CLuaConVars {
  const void *vtable;
} CLuaConVars;



static void cv_dtor(CLuaConVars *c) { glua_log("CV:cv_dtor"); (void)c; }
static void cv_Init(CLuaConVars *c) { glua_log("CV:cv_Init"); (void)c; }
static void *cv_CreateConVar(CLuaConVars *c, const char *name, const char *def,
                             const char *help, int flags)
{ glua_log("CV:cv_CreateConVar(%s)", name ? name : "(null)"); (void)c;
  return glua_convars_create_convar(name, def, help, flags); }
static void *cv_CreateConCommand(CLuaConVars *c, const char *name, const char *help,
                                 int flags, void *cb, void *completion)
{ glua_log("CV:cv_CreateConCommand(%s)", name ? name : "(null)"); (void)c;
  return glua_convars_create_concommand(name, help, flags, cb, completion); }
extern void glua_convars_destroy_managed(void);

/* DestroyManaged: take every ConVar/ConCommand this module created back out of
** the engine's registry.  Called when the last game realm closes. */
static void cv_DestroyManaged(CLuaConVars *c)
{ glua_log("CV:cv_DestroyManaged");
  (void)c;
  glua_convars_destroy_managed();
}
/* The ConVar module keeps a small key/value cache the engine uses to remember
** the values scripts asked for before a ConVar exists.  SaveManaged writes it
** back on shutdown, so entries have to survive ClearCache only until then.
*/
typedef struct CvCacheEntry {
  char *key;
  char *value;
  struct CvCacheEntry *next;
} CvCacheEntry;

static CvCacheEntry *g_cv_cache = NULL;

static void cv_cache_free(CvCacheEntry *e)
{
  while (e != NULL) {
    CvCacheEntry *next = e->next;
    free(e->key);
    free(e->value);
    free(e);
    e = next;
  }
}

static void cv_Cache(CLuaConVars *c, const char *key, const char *value)
{ CvCacheEntry *e;
  glua_log("CV:cv_Cache(%s=%s)", key ? key : "(null)", value ? value : "(null)");
  (void)c;
  if (key == NULL) return;
  for (e = g_cv_cache; e != NULL; e = e->next) {
    if (strcmp(e->key, key) == 0) {
      char *copy = value ? _strdup(value) : _strdup("");
      if (copy != NULL) { free(e->value); e->value = copy; }
      return;
    }
  }
  e = (CvCacheEntry *)calloc(1, sizeof(*e));
  if (e == NULL) return;
  e->key = _strdup(key);
  e->value = _strdup(value ? value : "");
  if (e->key == NULL || e->value == NULL) { cv_cache_free(e); return; }
  e->next = g_cv_cache;
  g_cv_cache = e;
}

static void cv_ClearCache(CLuaConVars *c)
{ glua_log("CV:cv_ClearCache");
  (void)c;
  cv_cache_free(g_cv_cache);
  g_cv_cache = NULL;
}

/* SaveManaged: the reference writes the cached values into cfg/client.vdf and
** cfg/server.vdf.  Writing those files belongs to the host's own config
** machinery here, so the values are handed back through the log sink instead;
** the cache itself is dropped afterwards either way. */
static void cv_SaveManaged(CLuaConVars *c)
{ CvCacheEntry *e;
  (void)c;
  glua_log("CV:cv_SaveManaged");
  for (e = g_cv_cache; e != NULL; e = e->next) {
    glua_log("CV:  %s = %s", e->key ? e->key : "(null)", e->value ? e->value : "");
  }
  cv_cache_free(g_cv_cache);
  g_cv_cache = NULL;
}

static const glua_vfn glua_convars_vtable[8] = {
  (glua_vfn)cv_dtor,
  (glua_vfn)cv_Init,
  (glua_vfn)cv_CreateConVar,
  (glua_vfn)cv_CreateConCommand,
  (glua_vfn)cv_DestroyManaged,
  (glua_vfn)cv_Cache,
  (glua_vfn)cv_ClearCache,
  (glua_vfn)cv_SaveManaged
};

static int ns_equals(const char *a, const char *b) { return glua_stricmp(a, b) == 0; }

LUALIB_API void *CreateInterface(const char *name, int *pReturnCode)
{
  /* Both interfaces are singletons: the engine caches the first answer it gets
  ** and hands that pointer to every module, so a second factory call must
  ** return the same object or the modules would each drive their own
  ** realm table. */
  static CLuaShared shared_singleton_holder;
  static CLuaConVars convars_singleton_holder;
  glua_log("CreateInterface('%s')", name ? name : "(null)");
  glua_boot_marker("CreateInterface");
  if (name == NULL) { if (pReturnCode) *pReturnCode = 1; return NULL; }
  if (ns_equals(name, "LUASHARED003") || ns_equals(name, "LUASHARED")) {
    shared_singleton_holder.vtable = (const void *)glua_shared_vtable;
    glua_shared_for_convars = &shared_singleton_holder;
    if (pReturnCode) *pReturnCode = 0;
    return &shared_singleton_holder;
  }
  if (ns_equals(name, "LUACONVARS001") || ns_equals(name, "LUACONVARS")) {
    convars_singleton_holder.vtable = (const void *)glua_convars_vtable;
    glua_convars_singleton = &convars_singleton_holder;
    if (pReturnCode) *pReturnCode = 0;
    return &convars_singleton_holder;
  }
  /* Unknown interface: the reference walks its list and returns NULL. */
  if (pReturnCode) *pReturnCode = 1;
  return NULL;
}

/* ------------------------------------------------------------------------ */
/* A tiny self-test entry so the host chain can be exercised without an     */
/* external harness: opglua_selftest() builds an interface, loads a module  */
/* and reports whether the module's global marker is visible.               */

LUALIB_API int opglua_selftest(const char *modulePath)
{
  CLuaShared shared;
  CLuaInterface *iface;
  lua_State *L;
  int rc;

  memset(&shared, 0, sizeof(shared));
  shared.vtable = (const void *)glua_shared_vtable;
  iface = cs_CreateLuaInterface(&shared, GLUA_REALM_SERVER, true);
  if (iface == NULL) { fputs("selftest: interface creation failed\n", stderr); return 1; }
  L = iface->state;
  if (L == NULL || L->luabase != iface) {
    fputs("selftest: luabase back pointer not set\n", stderr);
    return 2;
  }

  rc = GMOD_LoadBinaryModule(L, modulePath);
  printf("selftest: GMOD_LoadBinaryModule(%s) = %d\n", modulePath, rc);

  lua_getglobal(L, "opglua_test_marker");
  if (lua_type(L, -1) == GT_STRING) {
    printf("selftest: marker = '%s'\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    cs_CloseLuaInterface(&shared, iface);
    return rc == 1 ? 0 : 3;
  }
  printf("selftest: marker missing (type %d)\n", lua_type(L, -1));
  lua_pop(L, 1);
  cs_CloseLuaInterface(&shared, iface);
  return 4;
}
