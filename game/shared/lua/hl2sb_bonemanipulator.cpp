#include "cbase.h"
#include "hl2sb_bonemanipulator.h"
#include "hl2sb_transmit_parent.h"
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CBoneManipulate C_BoneManipulate
#endif

// GMod retains a full pose on a child controller, so snapshots also carry
// changes made before a client joins or before the target enters its PVS.
class CBoneManipulate : public CBaseEntity
{
public:
    DECLARE_CLASS(CBoneManipulate, CBaseEntity);
    DECLARE_NETWORKCLASS();
#ifndef CLIENT_DLL
    DECLARE_DATADESC();
#endif
    CBoneManipulate();
    ~CBoneManipulate();
#ifdef CLIENT_DLL
    void OnDataChanged(DataUpdateType_t type);
#else
    int UpdateTransmitState() { return SetTransmitState(FL_EDICT_PVSCHECK); }
    Vector m_localPositions[MAXSTUDIOBONES];
    QAngle m_localAngles[MAXSTUDIOBONES];
    Vector m_localScales[MAXSTUDIOBONES];
#endif
    CNetworkHandle(CBaseEntity, m_hTarget);
    CNetworkArray(Vector, m_positions, MAXSTUDIOBONES);
    CNetworkArray(QAngle, m_angles, MAXSTUDIOBONES);
    CNetworkArray(Vector, m_scales, MAXSTUDIOBONES);
    CNetworkArray(int, m_jiggle, MAXSTUDIOBONES);
};

IMPLEMENT_NETWORKCLASS_ALIASED(BoneManipulate, DT_BoneManipulate)
BEGIN_NETWORK_TABLE(CBoneManipulate, DT_BoneManipulate)
#ifdef CLIENT_DLL
    RecvPropEHandle(RECVINFO(m_hTarget)),
    RecvPropArray3(RECVINFO_ARRAY(m_positions), RecvPropVector(RECVINFO(m_positions[0]))),
    RecvPropArray3(RECVINFO_ARRAY(m_angles), RecvPropVector(RECVINFO(m_angles[0]))),
    RecvPropArray3(RECVINFO_ARRAY(m_scales), RecvPropVector(RECVINFO(m_scales[0]))),
    RecvPropArray3(RECVINFO_ARRAY(m_jiggle), RecvPropInt(RECVINFO(m_jiggle[0]))),
#else
    SendPropEHandle(SENDINFO(m_hTarget)),
    SendPropArray3(SENDINFO_ARRAY3(m_positions), SendPropVector(SENDINFO_ARRAY(m_positions), 0, SPROP_NOSCALE)),
    SendPropArray3(SENDINFO_ARRAY3(m_angles), SendPropVector(SENDINFO_ARRAY(m_angles), 0, SPROP_NOSCALE)),
    SendPropArray3(SENDINFO_ARRAY3(m_scales), SendPropVector(SENDINFO_ARRAY(m_scales), 0, SPROP_NOSCALE)),
    SendPropArray3(SENDINFO_ARRAY3(m_jiggle), SendPropInt(SENDINFO_ARRAY(m_jiggle), 8, SPROP_UNSIGNED)),
#endif
END_NETWORK_TABLE()
#ifndef CLIENT_DLL
LINK_ENTITY_TO_CLASS(hl2sb_bone_manipulator, CBoneManipulate);
BEGIN_DATADESC(CBoneManipulate)
    DEFINE_FIELD(m_hTarget, FIELD_EHANDLE),
    DEFINE_ARRAY(m_positions, FIELD_VECTOR, MAXSTUDIOBONES),
    DEFINE_ARRAY(m_angles, FIELD_VECTOR, MAXSTUDIOBONES),
    DEFINE_ARRAY(m_scales, FIELD_VECTOR, MAXSTUDIOBONES),
    DEFINE_ARRAY(m_jiggle, FIELD_INTEGER, MAXSTUDIOBONES),
    DEFINE_ARRAY(m_localPositions, FIELD_VECTOR, MAXSTUDIOBONES),
    DEFINE_ARRAY(m_localAngles, FIELD_VECTOR, MAXSTUDIOBONES),
    DEFINE_ARRAY(m_localScales, FIELD_VECTOR, MAXSTUDIOBONES),
END_DATADESC()
#endif

static CUtlVector<CBoneManipulate *> s_BoneControllers;

CBoneManipulate::CBoneManipulate()
{
    for (int i = 0; i < MAXSTUDIOBONES; ++i)
    {
        m_positions.Set(i, vec3_origin);
        m_angles.Set(i, vec3_angle);
        m_scales.Set(i, Vector(1, 1, 1));
        m_jiggle.Set(i, 0);
#ifndef CLIENT_DLL
        m_localPositions[i] = vec3_origin;
        m_localAngles[i] = vec3_angle;
        m_localScales[i].Init(1, 1, 1);
#endif
    }
    s_BoneControllers.AddToTail(this);
}

CBoneManipulate::~CBoneManipulate()
{
    s_BoneControllers.FindAndRemove(this);
#ifdef CLIENT_DLL
    CBaseAnimating *anim = m_hTarget.Get() ? m_hTarget->GetBaseAnimating() : NULL;
    if (anim) anim->InvalidateBoneCache();
#endif
}

static CBoneManipulate *BoneController(CBaseEntity *entity, bool create = false)
{
    if (!entity || !entity->GetBaseAnimating()) return NULL;
    for (int i = 0; i < s_BoneControllers.Count(); ++i)
        if (s_BoneControllers[i]->m_hTarget.Get() == entity)
            return s_BoneControllers[i];
#ifndef CLIENT_DLL
    if (create && !entity->IsMarkedForDeletion())
    {
        CBoneManipulate *controller = static_cast<CBoneManipulate *>(CreateEntityByName("hl2sb_bone_manipulator"));
        if (controller)
        {
            controller->m_hTarget = entity;
            controller->SetParent(entity);
            controller->SetLocalOrigin(vec3_origin);
            controller->SetSolid(SOLID_NONE);
            controller->SetMoveType(MOVETYPE_NONE);
            controller->AddEffects(EF_NODRAW);
            HL2SB_SetTransmitWithParent(controller, true);
            DispatchSpawn(controller);
        }
        return controller;
    }
#endif
    return NULL;
}

#ifdef CLIENT_DLL
struct LocalBoneManip
{
    EHANDLE entity;
    int bone;
    Vector position;
    QAngle angles;
    Vector scale;
    int jiggle;
};
static CUtlVector<LocalBoneManip> s_LocalBoneManips;

static LocalBoneManip *LocalBone(CBaseEntity *entity, int bone, bool create = false)
{
    for (int i = s_LocalBoneManips.Count() - 1; i >= 0; --i)
    {
        if (!s_LocalBoneManips[i].entity.Get())
            s_LocalBoneManips.FastRemove(i);
        else if (s_LocalBoneManips[i].entity.Get() == entity && s_LocalBoneManips[i].bone == bone)
            return &s_LocalBoneManips[i];
    }
    if (!create || !entity) return NULL;
    LocalBoneManip state;
    state.entity = entity;
    state.bone = bone;
    state.position = vec3_origin;
    state.angles = vec3_angle;
    state.scale.Init(1, 1, 1);
    state.jiggle = 0;
    return &s_LocalBoneManips[s_LocalBoneManips.AddToTail(state)];
}

void CBoneManipulate::OnDataChanged(DataUpdateType_t type)
{
    BaseClass::OnDataChanged(type);
    // The first authoritative snapshot replaces client-only defaults. Later
    // snapshots write only dirty elements and preserve other local overrides.
    for (int i = s_LocalBoneManips.Count() - 1; i >= 0; --i)
        if (!s_LocalBoneManips[i].entity.Get() || s_LocalBoneManips[i].entity == m_hTarget)
            s_LocalBoneManips.FastRemove(i);
    CBaseAnimating *anim = m_hTarget.Get() ? m_hTarget->GetBaseAnimating() : NULL;
    if (anim) anim->InvalidateBoneCache();
}
#endif

bool HL2SB_HasBoneManipulations(CBaseEntity *entity)
{
    if (BoneController(entity)) return true;
#ifdef CLIENT_DLL
    for (int i = 0; i < s_LocalBoneManips.Count(); ++i)
        if (s_LocalBoneManips[i].entity.Get() == entity && entity) return true;
#endif
    return false;
}

int HL2SB_GetBoneJiggle(CBaseEntity *entity, int bone)
{
    if (bone < 0 || bone >= MAXSTUDIOBONES) return 0;
    CBoneManipulate *controller = BoneController(entity);
    if (controller) return controller->m_jiggle[bone];
#ifdef CLIENT_DLL
    LocalBoneManip *local = LocalBone(entity, bone);
    if (local) return local->jiggle;
#endif
    return 0;
}

void HL2SB_SetBoneJiggle(CBaseEntity *entity, int bone, int value)
{
    CBaseAnimating *anim = entity ? entity->GetBaseAnimating() : NULL;
    if (!anim || bone < 0 || bone >= MAXSTUDIOBONES) return;
    value &= 255;
    const bool changed = HL2SB_GetBoneJiggle(entity, bone) != value;
    CBoneManipulate *controller = BoneController(entity, true);
    if (controller) controller->m_jiggle.Set(bone, value);
#ifdef CLIENT_DLL
    else LocalBone(entity, bone, true)->jiggle = value;
#endif
    if (changed) anim->InvalidateBoneCache();
}

Vector HL2SB_GetBonePositionOffset(CBaseEntity *entity, int bone)
{
    if (bone < 0 || bone >= MAXSTUDIOBONES) return vec3_origin;
    CBoneManipulate *controller = BoneController(entity);
#ifdef CLIENT_DLL
    if (controller) return controller->m_positions[bone];
    LocalBoneManip *local = LocalBone(entity, bone);
    return local ? local->position : vec3_origin;
#else
    return controller ? controller->m_localPositions[bone] : vec3_origin;
#endif
}

QAngle HL2SB_GetBoneAngleOffset(CBaseEntity *entity, int bone)
{
    if (bone < 0 || bone >= MAXSTUDIOBONES) return vec3_angle;
    CBoneManipulate *controller = BoneController(entity);
#ifdef CLIENT_DLL
    if (controller) return controller->m_angles[bone];
    LocalBoneManip *local = LocalBone(entity, bone);
    return local ? local->angles : vec3_angle;
#else
    return controller ? controller->m_localAngles[bone] : vec3_angle;
#endif
}

Vector HL2SB_GetBoneScale(CBaseEntity *entity, int bone)
{
    if (bone < 0 || bone >= MAXSTUDIOBONES) return Vector(1, 1, 1);
    CBoneManipulate *controller = BoneController(entity);
#ifdef CLIENT_DLL
    if (controller) return controller->m_scales[bone];
    LocalBoneManip *local = LocalBone(entity, bone);
    return local ? local->scale : Vector(1, 1, 1);
#else
    return controller ? controller->m_localScales[bone] : Vector(1, 1, 1);
#endif
}

void HL2SB_SetBonePositionOffset(CBaseEntity *entity, int bone, const Vector &value, bool network)
{
    CBaseAnimating *anim = entity ? entity->GetBaseAnimating() : NULL;
    if (!anim || bone < 0 || bone >= MAXSTUDIOBONES || !value.IsValid()) return;
    const bool changed = HL2SB_GetBonePositionOffset(entity, bone) != value;
    CBoneManipulate *controller = BoneController(entity, true);
#ifdef CLIENT_DLL
    if (!changed) { if (!controller) LocalBone(entity, bone, true); return; }
    if (controller) controller->m_positions.Set(bone, value);
    else LocalBone(entity, bone, true)->position = value;
#else
    if (!controller) return;
    if (network) controller->m_positions.Set(bone, value);
    controller->m_localPositions[bone] = value;
#endif
    if (changed) anim->InvalidateBoneCache();
}

void HL2SB_SetBoneAngleOffset(CBaseEntity *entity, int bone, const QAngle &value, bool network)
{
    CBaseAnimating *anim = entity ? entity->GetBaseAnimating() : NULL;
    if (!anim || bone < 0 || bone >= MAXSTUDIOBONES || !value.IsValid()) return;
    const bool changed = HL2SB_GetBoneAngleOffset(entity, bone) != value;
    CBoneManipulate *controller = BoneController(entity, true);
#ifdef CLIENT_DLL
    if (!changed) { if (!controller) LocalBone(entity, bone, true); return; }
    if (controller) controller->m_angles.Set(bone, value);
    else LocalBone(entity, bone, true)->angles = value;
#else
    if (!controller) return;
    if (network) controller->m_angles.Set(bone, value);
    controller->m_localAngles[bone] = value;
#endif
    if (changed) anim->InvalidateBoneCache();
}

void HL2SB_SetBoneScale(CBaseEntity *entity, int bone, const Vector &value, bool network)
{
    CBaseAnimating *anim = entity ? entity->GetBaseAnimating() : NULL;
    if (!anim || bone < 0 || bone >= MAXSTUDIOBONES || !value.IsValid()) return;
    const bool changed = HL2SB_GetBoneScale(entity, bone) != value;
    CBoneManipulate *controller = BoneController(entity, true);
#ifdef CLIENT_DLL
    if (!changed) { if (!controller) LocalBone(entity, bone, true); return; }
    if (controller) controller->m_scales.Set(bone, value);
    else LocalBone(entity, bone, true)->scale = value;
#else
    if (!controller) return;
    if (network) controller->m_scales.Set(bone, value);
    controller->m_localScales[bone] = value;
#endif
    if (changed) anim->InvalidateBoneCache();
}

#ifdef CLIENT_DLL
static void ApplyPoseOffset(int bone, const Vector &offset, const QAngle &angles, Vector *pos, Quaternion *q)
{
    pos[bone] += offset;
    if (angles == vec3_angle) return;
    Quaternion rotation, result;
    AngleQuaternion(angles, rotation);
    QuaternionMult(q[bone], rotation, result);
    q[bone] = result;
}

void HL2SB_ApplyBonePoseManipulations(C_BaseAnimating *entity, CStudioHdr *hdr, Vector *pos, Quaternion *q)
{
    if (!entity || !hdr) return;
    CBoneManipulate *controller = BoneController(entity);
    const int count = MIN(hdr->numbones(), MAXSTUDIOBONES);
    if (controller)
    {
        for (int i = 0; i < count; ++i)
            ApplyPoseOffset(i, controller->m_positions[i], controller->m_angles[i], pos, q);
        return;
    }
    for (int i = s_LocalBoneManips.Count() - 1; i >= 0; --i)
    {
        const LocalBoneManip &state = s_LocalBoneManips[i];
        if (!state.entity.Get()) s_LocalBoneManips.FastRemove(i);
        else if (state.entity.Get() == entity && state.bone >= 0 && state.bone < count)
            ApplyPoseOffset(state.bone, state.position, state.angles, pos, q);
    }
}

static void ApplyScale(C_BaseAnimating *entity, int index, const Vector &scale)
{
    if (scale == Vector(1, 1, 1)) return;
    matrix3x4_t &bone = entity->GetBoneForWrite(index);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            bone[row][column] *= scale[column];
}

void HL2SB_ApplyBoneScaleManipulations(C_BaseAnimating *entity, CStudioHdr *hdr, int boneMask)
{
    if (!entity || !hdr) return;
    CBoneManipulate *controller = BoneController(entity);
    const int count = MIN(hdr->numbones(), MAXSTUDIOBONES);
    if (controller)
    {
        for (int i = 0; i < count; ++i)
            if (hdr->boneFlags(i) & boneMask)
                ApplyScale(entity, i, controller->m_scales[i]);
        return;
    }
    for (int i = 0; i < s_LocalBoneManips.Count(); ++i)
    {
        const LocalBoneManip &state = s_LocalBoneManips[i];
        if (state.entity.Get() == entity && state.bone >= 0 && state.bone < count && (hdr->boneFlags(state.bone) & boneMask))
            ApplyScale(entity, state.bone, state.scale);
    }
}
#endif
