#include "cbase.h"
#include "hl2sb_motioncontroller.h"
#include "physics.h"
#include "basescripted.h"
#include "luamanager.h"
#include "lbaseentity_shared.h"
#include "lvphysics_interface.h"
#include "mathlib/lvector.h"
#include "igamesystem.h"
#include <map>

#include "tier0/memdbgon.h"

namespace
{
class LuaMotionController : public IMotionEvent
{
public:
	EHANDLE owner;
	IPhysicsMotionController *controller;
	LuaMotionController(CBaseEntity *entity) : owner(entity), controller(NULL) {}
	~LuaMotionController() { if (controller && physenv) physenv->DestroyMotionController(controller); }
	simresult_e Simulate(IPhysicsMotionController *, IPhysicsObject *object, float delta, Vector &linear, AngularImpulse &angular)
	{
		linear.Init();
		angular.Init();
		CBaseEntity *entity = owner.Get();
		if (!L || !entity || entity->m_nTableReference < 0) return SIM_NOTHING;
		if (!lua_checkstack(L, 32)) return SIM_NOTHING;
		int top = lua_gettop(L);
		lua_getref(L, entity->m_nTableReference);
		if (!lua_istable(L, -1)) { lua_settop(L, top); return SIM_NOTHING; }
		luasrc_PushScriptField(L, -1, "PhysicsSimulate");
		if (!lua_isfunction(L, -1)) { lua_settop(L, top); return SIM_NOTHING; }
		CBaseEntity::PushLuaInstanceSafe(L, entity);
		lua_pushphysicsobject(L, object);
		lua_pushnumber(L, delta);
		int mode = SIM_NOTHING;
		if (luasrc_pcall(L, 3, 3, 0) == 0 && lua_isnumber(L, -1))
		{
			mode = (int)lua_tointeger(L, -1);
			if (mode > SIM_NOTHING && mode <= SIM_GLOBAL_FORCE &&
				luaL_testudata(L, -3, "Vector") && luaL_testudata(L, -2, "Vector"))
			{
				angular = lua_tovector(L, -3);
				linear = lua_tovector(L, -2);
				if (!angular.IsValid() || !linear.IsValid()) mode = SIM_NOTHING;
			}
			else mode = SIM_NOTHING;
		}
		lua_settop(L, top);
		if (mode == SIM_NOTHING) { linear.Init(); angular.Init(); }
		return (simresult_e)mode;
	}
};

std::map<CBaseEntity *, LuaMotionController *> s_Controllers;
class LuaMotionSystem : public CAutoGameSystem
{
public:
	void LevelShutdownPreEntity()
	{
		for (std::map<CBaseEntity *, LuaMotionController *>::iterator it = s_Controllers.begin(); it != s_Controllers.end(); ++it) delete it->second;
		s_Controllers.clear();
	}
} s_LuaMotionSystem;
}

void HL2SB_StopMotionController(CBaseEntity *entity)
{
	std::map<CBaseEntity *, LuaMotionController *>::iterator found = s_Controllers.find(entity);
	if (found == s_Controllers.end()) return;
	delete found->second;
	s_Controllers.erase(found);
}

void HL2SB_StartMotionController(CBaseEntity *entity)
{
	if (!physenv || !dynamic_cast<CBaseScripted *>(entity)) return;
	HL2SB_StopMotionController(entity);
	LuaMotionController *motion = new LuaMotionController(entity);
	motion->controller = physenv->CreateMotionController(motion);
	if (!motion->controller) { delete motion; return; }
	s_Controllers[entity] = motion;
	if (entity->VPhysicsGetObject()) motion->controller->AttachObject(entity->VPhysicsGetObject(), true);
}

void HL2SB_EditMotionController(CBaseEntity *entity, IPhysicsObject *object, bool attach)
{
	std::map<CBaseEntity *, LuaMotionController *>::iterator found = s_Controllers.find(entity);
	if (found == s_Controllers.end() || !object) return;
	if (attach) found->second->controller->AttachObject(object, true);
	else found->second->controller->DetachObject(object);
}
