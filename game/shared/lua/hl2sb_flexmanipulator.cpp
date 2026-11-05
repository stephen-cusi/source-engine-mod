#include "cbase.h"
#include "hl2sb_flexmanipulator.h"
#include "hl2sb_transmit_parent.h"
#ifdef CLIENT_DLL
#include "c_baseflex.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CHL2SBFlexManipulator C_HL2SBFlexManipulator
#endif

// A separate controller also supports animating entities without CBaseFlex.
class CHL2SBFlexManipulator : public CBaseEntity
{
public:
    DECLARE_CLASS(CHL2SBFlexManipulator, CBaseEntity);
    DECLARE_NETWORKCLASS();
#ifndef CLIENT_DLL
    DECLARE_DATADESC();
#endif
    CHL2SBFlexManipulator();
    ~CHL2SBFlexManipulator();
#ifdef CLIENT_DLL
    void OnDataChanged(DataUpdateType_t type);
#else
    int UpdateTransmitState() { return SetTransmitState(FL_EDICT_PVSCHECK); }
#endif
    CNetworkHandle(CBaseEntity, m_hTarget);
    CNetworkVar(float, m_flScale);
    CNetworkArray(float, m_flWeights, MAXSTUDIOFLEXCTRL);
};

IMPLEMENT_NETWORKCLASS_ALIASED(HL2SBFlexManipulator, DT_HL2SBFlexManipulator)
BEGIN_NETWORK_TABLE(CHL2SBFlexManipulator, DT_HL2SBFlexManipulator)
#ifdef CLIENT_DLL
    RecvPropEHandle(RECVINFO(m_hTarget)),
    RecvPropFloat(RECVINFO(m_flScale)),
    RecvPropArray3(RECVINFO_ARRAY(m_flWeights), RecvPropFloat(RECVINFO(m_flWeights[0]))),
#else
    SendPropEHandle(SENDINFO(m_hTarget)),
    SendPropFloat(SENDINFO(m_flScale), 0, SPROP_NOSCALE),
    SendPropArray3(SENDINFO_ARRAY3(m_flWeights), SendPropFloat(SENDINFO_ARRAY(m_flWeights), 0, SPROP_NOSCALE)),
#endif
END_NETWORK_TABLE()
#ifndef CLIENT_DLL
LINK_ENTITY_TO_CLASS(hl2sb_flex_manipulator, CHL2SBFlexManipulator);
BEGIN_DATADESC(CHL2SBFlexManipulator)
    DEFINE_FIELD(m_hTarget, FIELD_EHANDLE),
    DEFINE_FIELD(m_flScale, FIELD_FLOAT),
    DEFINE_ARRAY(m_flWeights, FIELD_FLOAT, MAXSTUDIOFLEXCTRL),
END_DATADESC()
#endif

static CUtlVector<CHL2SBFlexManipulator *> s_Controllers;
#ifdef CLIENT_DLL
struct LocalFlexState
{
    EHANDLE entity;
    float scale;
    float weights[MAXSTUDIOFLEXCTRL];
};
static CUtlVector<LocalFlexState> s_LocalFlex;

void CHL2SBFlexManipulator::OnDataChanged(DataUpdateType_t type)
{
    BaseClass::OnDataChanged(type);
    for (int i = s_LocalFlex.Count() - 1; i >= 0; --i)
        if (!s_LocalFlex[i].entity.Get() || s_LocalFlex[i].entity == m_hTarget)
            s_LocalFlex.FastRemove(i);
}

static LocalFlexState *LocalState(CBaseEntity *entity, bool create)
{
    for (int i = s_LocalFlex.Count() - 1; i >= 0; --i)
    {
        if (!s_LocalFlex[i].entity.Get())
            s_LocalFlex.FastRemove(i);
        else if (s_LocalFlex[i].entity == entity)
            return &s_LocalFlex[i];
    }
    if (!create)
        return NULL;
    LocalFlexState state;
    state.entity = entity;
    state.scale = HL2SB_GetFlexScale(entity);
    for (int i = 0; i < MAXSTUDIOFLEXCTRL; ++i)
        state.weights[i] = HL2SB_GetFlexWeight(entity, i);
    return &s_LocalFlex[s_LocalFlex.AddToTail(state)];
}
#endif

CHL2SBFlexManipulator::CHL2SBFlexManipulator()
{
    m_flScale = 1.0f;
    for (int i = 0; i < MAXSTUDIOFLEXCTRL; ++i)
        m_flWeights.Set(i, 0.0f);
    s_Controllers.AddToTail(this);
}

CHL2SBFlexManipulator::~CHL2SBFlexManipulator()
{
    s_Controllers.FindAndRemove(this);
}

static CHL2SBFlexManipulator *Controller(CBaseEntity *entity, bool create = false)
{
    if (!entity || !entity->GetBaseAnimating())
        return NULL;
    for (int i = 0; i < s_Controllers.Count(); ++i)
        if (s_Controllers[i]->m_hTarget.Get() == entity)
            return s_Controllers[i];
#ifndef CLIENT_DLL
    if (create && !entity->IsMarkedForDeletion())
    {
        CHL2SBFlexManipulator *controller = static_cast<CHL2SBFlexManipulator *>(
            CreateEntityByName("hl2sb_flex_manipulator"));
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

bool HL2SB_HasFlexManipulator(CBaseEntity *entity)
{
#ifdef CLIENT_DLL
    if (LocalState(entity, false))
        return true;
#endif
    return Controller(entity) != NULL;
}

float HL2SB_GetFlexWeight(CBaseEntity *entity, int index)
{
    index = clamp(index, 0, MAXSTUDIOFLEXCTRL - 1);
#ifdef CLIENT_DLL
    LocalFlexState *state = LocalState(entity, false);
    if (state)
        return state->weights[index];
#endif
    CHL2SBFlexManipulator *controller = Controller(entity);
    return controller ? controller->m_flWeights[index] : 0.0f;
}

void HL2SB_SetFlexWeight(CBaseEntity *entity, int index, float weight)
{
    CBaseAnimating *anim = entity ? entity->GetBaseAnimating() : NULL;
    if (!anim)
        return;
    index = clamp(index, 0, MAXSTUDIOFLEXCTRL - 1);
#ifdef CLIENT_DLL
    LocalFlexState *state = LocalState(entity, true);
    state->weights[index] = weight;
#else
    CHL2SBFlexManipulator *controller = Controller(entity, true);
    if (controller)
        controller->m_flWeights.Set(index, weight);
#endif
}

float HL2SB_GetFlexScale(CBaseEntity *entity)
{
#ifdef CLIENT_DLL
    LocalFlexState *state = LocalState(entity, false);
    if (state)
        return state->scale;
#endif
    CHL2SBFlexManipulator *controller = Controller(entity);
    return controller ? controller->m_flScale.Get() : 1.0f;
}

void HL2SB_SetFlexScale(CBaseEntity *entity, float scale)
{
    if (!entity || !entity->GetBaseAnimating())
        return;
#ifdef CLIENT_DLL
    LocalState(entity, true)->scale = scale;
#else
    CHL2SBFlexManipulator *controller = Controller(entity, true);
    if (controller)
        controller->m_flScale = scale;
#endif
}

#ifdef CLIENT_DLL
bool HL2SB_ApplyFlexManipulation(CBaseEntity *entity, int count, float *weights, float *delayed)
{
    if (!HL2SB_HasFlexManipulator(entity))
        return false;
    CBaseAnimating *anim = entity->GetBaseAnimating();
    CStudioHdr *hdr = anim ? anim->GetModelPtr() : NULL;
    if (!hdr || count != hdr->numflexdesc() || !weights || hdr->numflexcontrollers() == 0)
        return false;
    C_BaseFlex::LinkToGlobalFlexControllers(hdr);
    float controllers[MAXSTUDIOFLEXDESC] = {};
    for (int i = 0; i < MIN(hdr->numflexcontrollers(), MAXSTUDIOFLEXCTRL); ++i)
    {
        const mstudioflexcontroller_t *flex = hdr->pFlexcontroller((LocalFlexController_t)i);
        if (flex->localToGlobal >= 0 && flex->localToGlobal < ARRAYSIZE(controllers))
            controllers[flex->localToGlobal] = HL2SB_GetFlexWeight(entity, i) * (flex->max - flex->min) + flex->min;
    }
    Q_memset(weights, 0, count * sizeof(float));
    hdr->RunFlexRules(controllers, weights);
    const float scale = HL2SB_GetFlexScale(entity);
    for (int i = 0; i < count; ++i)
        weights[i] *= scale;
    if (delayed)
        Q_memcpy(delayed, weights, count * sizeof(float));
    return true;
}
#endif
