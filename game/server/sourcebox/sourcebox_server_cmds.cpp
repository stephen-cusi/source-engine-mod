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
	if (engine->PrecacheModel(args[1], 0) == 0) {
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
		forward = (tr.endpos + tr.plane.normal*16);
		model->Teleport(&forward, NULL, NULL);
	}

	model->Activate();
}

ConCommand sb_modelspawn("sb_modelspawn", SB_ModelSpawn);


CON_COMMAND(ent_setpos, "Set an entity's position, angles, and velocity. Leave argument blank with \"\" or <> to ignore\nUSAGE: ent_setpos <ENTITY> <X> <Y> <Z> <PITCH> <YAW> <ROLL> <VEL X> <VEL Y> <VEL Z>")
{
	if (args.ArgC() <= 2)
	{
		Msg("USAGE: ent_setpos <ENTITY> <X> <Y> <Z> <PITCH> <YAW> <ROLL> <VEL X> <VEL Y> <VEL Z>\n");
		return;
	}
	CBasePlayer* pPlayer = UTIL_GetCommandClient();
	CBaseEntity* target = gEntList.FindEntityByName(NULL, args[1], pPlayer);
	if (target == NULL)
	{
		target = UTIL_EntityByIndex(atoi(args[1]));
		if (target == NULL)
			return;
	}
	Vector pos = target->GetAbsOrigin();
	QAngle ang = target->GetAbsAngles();
	Vector vel = target->GetAbsVelocity();
	pos[0] = atof(args[2]);
	if (args.ArgC() < 3)
		goto tel;
	pos[1] = atof(args[3]);
	if (args.ArgC() < 4)
		goto tel;
	pos[2] = atof(args[4]);
	if (args.ArgC() < 5)
		goto tel;
	ang[0] = atof(args[5]);
	if (args.ArgC() < 6)
		goto tel;
	ang[1] = atof(args[6]);
	if (args.ArgC() < 7)
		goto tel;
	ang[2] = atof(args[7]);
	if (args.ArgC() < 8)
		goto tel;
	vel[0] = atof(args[8]);
	if (args.ArgC() < 9)
		goto tel;
	vel[1] = atof(args[9]);
	if (args.ArgC() < 10)
		goto tel;
	vel[2] = atof(args[10]);
tel:
	target->Teleport(&pos, &ang, &vel);
}



CON_COMMAND(ent_probe, "Probe a keyvalue from an entity\nUSAGE: ent_probe <ENTITY> <KEYVALUE>")
{
	if (args.ArgC() <= 2)
	{
		Msg("USAGE: ent_probe <ENTITY> <KEYVALUE>\n");
		return;
	}
	
	CBasePlayer* pPlayer = UTIL_GetCommandClient();
	CBaseEntity* target = gEntList.FindEntityByName(NULL, args[1], pPlayer);
	if (target == NULL)
	{
		target = UTIL_EntityByIndex(atoi(args[1]));
		if (target == NULL)
			return;
	}
	char value[256];
	target->GetKeyValue(args[2], value, 256);
	Msg("%s\n", value);
}


CON_COMMAND_F(geteyeang, "Get player angles\nUSAGE: getang <OPTIONAL PLAYER INDEX>", FCVAR_CHEAT)
{
	CBasePlayer* player = NULL;
	if (args.ArgC() < 2)
	{
		player = UTIL_GetCommandClient();
		if (!player)
		{
			return;
		}
	}
	else
	{
		player = UTIL_PlayerByIndex(atoi(args[1]));
		if (!player)
		{
			return;
		}
	}
	QAngle angles = player->EyeAngles();
	Msg("%f %f %f\n", angles[0], angles[1], angles[2]);
}
CON_COMMAND_F(getpos, "Get player position\nUSAGE: getpos <OPTIONAL PLAYER INDEX>", FCVAR_CHEAT)
{
	CBasePlayer* player = NULL;
	if (args.ArgC() < 2)
	{
		player = UTIL_GetCommandClient();
		if (!player)
		{
			return;
		}
	}
	else
	{
		player = UTIL_PlayerByIndex(atoi(args[1]));
		if (!player)
		{
			return;
		}
	}
	Vector pos = player->GetAbsOrigin();
	Msg("%f %f %f\n", pos[0], pos[1], pos[2]);
}
CON_COMMAND_F(geteyepos, "Get player eye position\nUSAGE: geteyepos <OPTIONAL PLAYER INDEX>", FCVAR_CHEAT)
{
	CBasePlayer* player = NULL;
	if (args.ArgC() < 2)
	{
		player = UTIL_GetCommandClient();
		if (!player)
		{
			return;
		}
	}
	else
	{
		player = UTIL_PlayerByIndex(atoi(args[1]));
		if (!player)
		{
			return;
		}
	}
	Vector pos = player->EyePosition();
	Msg("%f %f %f\n", pos[0], pos[1], pos[2]);
}
CON_COMMAND_F(geteyevectors, "Get player eye vectors\nUSAGE: geteyevectors <OPTIONAL PLAYER INDEX>", FCVAR_CHEAT)
{
	CBasePlayer* player = NULL;
	if (args.ArgC() < 2)
	{
		player = UTIL_GetCommandClient();
		if (!player)
		{
			return;
		}
	}
	else
	{
		player = UTIL_PlayerByIndex(atoi(args[1]));
		if (!player)
		{
			return;
		}
	}
	Vector fwd;
	Vector right;
	Vector up;
	player->EyeVectors(&fwd, &right, &up);
	Msg("%f %f %f;%f %f %f;%f %f %f\n", fwd[0], fwd[1], fwd[2], right[0], right[1], right[2], up[0], up[1], up[2]);
}

CON_COMMAND_F(getvel, "Get player position\nUSAGE: getpos <OPTIONAL PLAYER INDEX>", FCVAR_CHEAT)
{
	CBasePlayer* player = NULL;
	if (args.ArgC() < 2)
	{
		player = UTIL_GetCommandClient();
		if (!player)
		{
			return;
		}
	}
	else
	{
		player = UTIL_PlayerByIndex(atoi(args[1]));
		if (!player)
		{
			return;
		}
	}
	Vector vel = player->GetAbsVelocity();
	Msg("%f %f %f\n", vel[0], vel[1], vel[2]);
}

CON_COMMAND_F(getang, "Get player position\nUSAGE: getpos <OPTIONAL PLAYER INDEX>", FCVAR_CHEAT)
{
	CBasePlayer* player = NULL;
	if (args.ArgC() < 2)
	{
		player = UTIL_GetCommandClient();
		if (!player)
		{
			return;
		}
	}
	else
	{
		player = UTIL_PlayerByIndex(atoi(args[1]));
		if (!player)
		{
			return;
		}
	}
	QAngle ang = player->GetAbsAngles();
	Msg("%f %f %f\n", ang[0], ang[1], ang[2]);
}
