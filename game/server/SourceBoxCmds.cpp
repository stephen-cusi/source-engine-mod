#include "cbase.h"
#include <datacache/imdlcache.h>
#include "baseanimating.h"

void SB_ModelSpawn(const CCommand& args)
{
	MDLCACHE_CRITICAL_SECTION();

	CBasePlayer* pPlayer = UTIL_GetCommandClient();
	if (!pPlayer)
	{
		return;
	}
	if (engine->PrecacheModel(args[1], 0) == -1) {
		return;
	}
	int modelindex = modelinfo->GetModelIndex(args[1]);
	vcollide_t* collide = modelinfo->GetVCollide(modelindex);
	if (!collide) {
		return;
	}
	CBaseAnimating* model;
	if (collide->solidCount > 1) {
		model = dynamic_cast<CBaseAnimating*>(CreateEntityByName("prop_ragdoll"));
	}
	else if (collide->solidCount == 1) {
		model = dynamic_cast<CBaseAnimating*>(CreateEntityByName("prop_physics"));
	}
	else {
		return;
	}
	model->Precache();
	model->SetModel(args[1]);
	DispatchSpawn(model);

	// Now attempt to drop into the world
	trace_t tr;
	Vector forward;
	pPlayer->EyeVectors(&forward);
	UTIL_TraceLine(pPlayer->EyePosition(),
		pPlayer->EyePosition() + forward * MAX_TRACE_LENGTH, MASK_SOLID,
		pPlayer, COLLISION_GROUP_NONE, &tr);
	if (tr.fraction != 1.0)
	{
		model->Teleport(&(tr.endpos + tr.plane.normal*16), NULL, NULL);
	}

	model->Activate();
}

ConCommand sb_modelspawn("sb_modelspawn", SB_ModelSpawn);