//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB GMod compat (2026-10-03): the client side of the physics
// gun's beam entity - a direct translation of the reference C_PhysBeam draw
// path.
//
//   idle (m_bIsOn false) : three pulsing claw sprites at the holder's
//                          view-model attachment 1; no beam, no end dot
//   holding              : a quadratic-Bezier ribbon (17 points) from the
//                          muzzle to the transformed m_HoldPos, drawn in
//                          three passes (widths 2.0 / rand(2,5) / rand(2,6),
//                          black endpoints, texture scroll scaled by the span
//                          length); claw sprites (2, or 7 on a bone target)
//                          marching along the aim direction; an end dot of
//                          2/7 size-rand sprites at the hold point
//   colour               : the holder player's weapon colour everywhere
//
// The anchor is ALWAYS the holder's view-model attachment 1 (first and third
// person alike - the entity draws in the normal world pass, so there is no
// view-model projection problem to work around).
//
//=============================================================================//

#include "cbase.h"
#include "beamdraw.h"
#include "c_baseplayer.h"
#include "c_basecombatweapon.h"
#include "iviewrender.h"
#include "luamanager.h"		// HL2SB: the DrawPhysgunBeam Lua hook dispatch
#include "luasrclib.h"
#include "lbaseplayer_shared.h"	// HL2SB: lua_pushplayer
#include "lbaseentity_shared.h"	// HL2SB: lua_pushentity / lua_pushvector
#include "mathlib/lvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar physgun_drawbeams;
extern ConVar physgun_maxrange;
extern ConVar hl2sb_physgun_beamdebug;	// HL2SB (2026-10-03): defined in weapon_physgun.cpp
Color HL2SB_GetWeaponColor( int iUserID );

class C_PhysBeam : public C_BaseEntity
{
	DECLARE_CLASS( C_PhysBeam, C_BaseEntity );

public:
	DECLARE_CLIENTCLASS();

	C_PhysBeam( void );

	virtual bool	ShouldDraw( void ) { return true; }
	virtual bool	IsTransparent( void ) { return true; }
	virtual int		DrawModel( int flags );

	// The entity has NO model - without this the leaf system's render bounds
	// are an empty box, the beam lands in no leaves, and DrawModel never runs
	// (the whole visual - and the GM:DrawPhysgunBeam Lua hook the halo feed
	// hangs on - silently never fires).  A box covering the maximum beam
	// reach around the holder (the entity is parented to the player) keeps
	// the beam in every leaf the player is in.
	virtual void	GetRenderBounds( Vector& mins, Vector& maxs )
	{
		mins.Init( -4096.0f, -4096.0f, -4096.0f );
		maxs.Init( 4096.0f, 4096.0f, 4096.0f );
	}

private:
	void DrawRibbon( const Vector &start, const Vector &control, const Vector &end, float width, const Vector &color, float scroll );
	void DrawClawSprites( const Vector &pos, const Vector &fwd, bool bBoneTarget, byte r, byte g, byte b );
	void DrawEndDot( const Vector &pos, byte r, byte g, byte b, bool bBoneTarget );

private:
	EHANDLE			m_hPlayer;
	EHANDLE			m_hTargetEnt;
	Vector			m_HoldPos;
	bool			m_bIsOn;
	int				m_iPhysBone;

	CMaterialReference	m_hGlow1;	// sprites/physg_glow1 - the end dot
	CMaterialReference	m_hGlow2;	// sprites/physg_glow2 - the claw sprites
	IMaterial			*m_pBeamMaterial;	// sprites/physbeam
	IMaterial			*m_pBeamMaterialA;	// sprites/physbeama - bone holds
};

IMPLEMENT_CLIENTCLASS_DT( C_PhysBeam, DT_PhysBeam, CPhysBeam )
	RecvPropEHandle( RECVINFO( m_hPlayer ) ),
	RecvPropEHandle( RECVINFO( m_hTargetEnt ) ),
	RecvPropVector( RECVINFO( m_HoldPos ) ),
	RecvPropInt( RECVINFO( m_bIsOn ) ),
	RecvPropInt( RECVINFO( m_iPhysBone ) ),
END_RECV_TABLE()

C_PhysBeam::C_PhysBeam( void )
{
	m_hPlayer = NULL;
	m_hTargetEnt = NULL;
	m_HoldPos.Init();
	m_bIsOn = false;
	m_iPhysBone = 0;

	m_hGlow1.Init( "sprites/physg_glow1", TEXTURE_GROUP_CLIENT_EFFECTS );
	m_hGlow2.Init( "sprites/physg_glow2", TEXTURE_GROUP_CLIENT_EFFECTS );
	m_pBeamMaterial = materials->FindMaterial( "sprites/physbeam", TEXTURE_GROUP_CLIENT_EFFECTS );
	m_pBeamMaterialA = materials->FindMaterial( "sprites/physbeama", TEXTURE_GROUP_CLIENT_EFFECTS );
}

//-----------------------------------------------------------------------------
// The ribbon: 17-point quadratic Bezier, texture scroll scaled by the span
// length (texcoord = fmod(scroll,1) - len * t * 0.02), black endpoints.
//-----------------------------------------------------------------------------
void C_PhysBeam::DrawRibbon( const Vector &start, const Vector &control, const Vector &end, float width, const Vector &color, float scroll )
{
	const int subdivisions = 16;
	const float flSpan = start.DistTo( end );

	CMatRenderContextPtr pRenderContext( materials );
	CBeamSegDraw beamDraw;
	beamDraw.Start( pRenderContext, subdivisions + 1, NULL );

	BeamSeg_t seg;
	seg.m_flAlpha = 1.0f;
	seg.m_flWidth = width;

	float u = fmod( scroll, 1.0f );
	float dt = 1.0f / (float)subdivisions;
	float t = 0.0f;
	for ( int i = 0; i <= subdivisions; i++, t += dt )
	{
		float omt = ( 1.0f - t );
		seg.m_vPos = omt * omt * start + 2.0f * t * omt * control + t * t * end;
		seg.m_flTexCoord = u - flSpan * t * 0.02f;
		seg.m_vColor = ( i == 0 || i == subdivisions ) ? vec3_origin : color;
		beamDraw.NextSeg( &seg );
	}

	beamDraw.End();
}

//-----------------------------------------------------------------------------
// The claw sprites: glow2, marching along the forward direction.
//   idle    : 3 sprites, size = sin(time*5 + i*15)*8 + 48, alpha 120..255,
//             drift 5 units per sprite
//   holding : 2 sprites (7 on a bone target), alpha 100..255, drift 2
//-----------------------------------------------------------------------------
void C_PhysBeam::DrawClawSprites( const Vector &pos, const Vector &fwd, bool bBoneTarget, byte r, byte g, byte b )
{
	CMatRenderContextPtr pRenderContext( materials );

	Vector vecPos = pos;

	if ( !m_bIsOn )
	{
		for ( int i = 0; i < 3; i++ )
		{
			float flSize = sinf( gpGlobals->curtime * 5.0f + (float)i * 15.0f ) * 8.0f + 48.0f;
			color32 clr;
			clr.r = r; clr.g = g; clr.b = b;
			clr.a = (byte)random->RandomInt( 120, 255 );
			pRenderContext->Bind( m_hGlow2 );
			DrawSprite( vecPos, flSize, flSize, clr );
			vecPos += fwd * 5.0f;
		}
		return;
	}

	int nCount = bBoneTarget ? 7 : 2;
	for ( int i = 0; i < nCount; i++ )
	{
		float flSize = random->RandomFloat( 4.0f, 48.0f );
		color32 clr;
		clr.r = r; clr.g = g; clr.b = b;
		clr.a = (byte)random->RandomInt( 100, 255 );
		pRenderContext->Bind( m_hGlow2 );
		DrawSprite( vecPos, flSize, flSize, clr );
		vecPos += fwd * 2.0f;
	}
}

//-----------------------------------------------------------------------------
// The end dot: glow1, 2 sprites (7 on a bone target) of size rand(1,16) at
// the hold point.  Only while the beam is lit - idle shows NOTHING there.
//-----------------------------------------------------------------------------
void C_PhysBeam::DrawEndDot( const Vector &pos, byte r, byte g, byte b, bool bBoneTarget )
{
	CMatRenderContextPtr pRenderContext( materials );

	int nCount = bBoneTarget ? 7 : 2;
	for ( int i = 0; i < nCount; i++ )
	{
		float flSize = random->RandomFloat( 1.0f, 16.0f );
		color32 clr;
		clr.r = r; clr.g = g; clr.b = b;
		clr.a = (byte)random->RandomInt( 100, 255 );
		pRenderContext->Bind( m_hGlow1 );
		DrawSprite( pos, flSize, flSize, clr );
	}
}

//-----------------------------------------------------------------------------
// The draw path.
//
// HL2SB (2026-10-03): the entity draw is the DIAGNOSTIC renderer - it runs
// only while hl2sb_physgun_beamdebug is on, so the shipping visual comes solely
// from the weapon's own DrawModel / ViewModelDrawn path
// (game/shared/hl2sb/weapon_physgun.cpp).  The GM:DrawPhysgunBeam hook also
// lives in the weapon path; this entity stays the state carrier + future
// net-reference and must not double-fire the hook.
//-----------------------------------------------------------------------------
int C_PhysBeam::DrawModel( int flags )
{
	// Shipping renderer is the weapon path; the entity draws in debug only.
	if ( !hl2sb_physgun_beamdebug.GetBool() )
		return 0;

	if ( !physgun_drawbeams.GetBool() )
		return 0;

	C_BasePlayer *pPlayer = ToBasePlayer( m_hPlayer );
	if ( pPlayer == NULL )
		return 0;

	// The beam only glows while its holder actually has the physgun out.
	CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	if ( pWeapon == NULL || !FClassnameIs( pWeapon, "weapon_physgun" ) )
		return 0;

	// Reference gate: the holder's view model must exist and carry a model.
	C_BaseViewModel *pVM = pPlayer->GetViewModel( 0 );
	if ( pVM == NULL || !pVM->GetModel() || !pVM->GetModelName() || pVM->GetModelName()[0] == 0 )
		return 0;

	// The muzzle anchor: attachment 1 on the view model, falling back to the
	// shoot position.
	Vector vecMuzzle;
	QAngle angMuzzle;
	if ( !pVM->GetAttachment( 1, vecMuzzle, angMuzzle ) )
	{
		vecMuzzle = pPlayer->Weapon_ShootPosition();
		angMuzzle = pPlayer->EyeAngles();
	}

	// The ribbon direction is the PLAYER'S AIM - the eye angles (body angles
	// in a vehicle).  Not the muzzle attachment's angles, which sway with the
	// view-model animation.
	Vector vecAimDir;
	if ( pPlayer->GetVehicle() != NULL )
		AngleVectors( pPlayer->GetAbsAngles(), &vecAimDir );
	else
		AngleVectors( pPlayer->EyeAngles(), &vecAimDir );

	// The beam end: the grab point transformed back by the target (the same
	// three-way rule the server stored it with), or the gate-traced scan
	// point when nothing is held.
	C_BaseEntity *pTarget = m_hTargetEnt;
	Vector vecEnd;
	bool bBoneTarget = false;

	if ( pTarget != NULL )
	{
		bBoneTarget = ( m_iPhysBone > 0 );

		if ( pTarget->GetAbsAngles() == vec3_angle )
		{
			vecEnd = pTarget->GetAbsOrigin() + m_HoldPos;
		}
		else if ( pTarget->GetMoveType() == MOVETYPE_VPHYSICS )
		{
			C_BaseAnimating *pAnim = static_cast< C_BaseAnimating * >( pTarget );
			CStudioHdr *pHdr = pAnim ? pAnim->GetModelPtr() : NULL;
			if ( pHdr != NULL && pHdr->IsValid() && m_iPhysBone >= 0 && m_iPhysBone < pHdr->numbones() )
			{
				VectorTransform( m_HoldPos, pAnim->GetBone( m_iPhysBone ), vecEnd );
			}
			else
			{
				pTarget->EntityToWorldSpace( m_HoldPos, &vecEnd );
			}
		}
		else
		{
			pTarget->EntityToWorldSpace( m_HoldPos, &vecEnd );
		}
	}
	else
	{
		Vector vecScan = pPlayer->Weapon_ShootPosition() + vecAimDir * ( physgun_maxrange.GetFloat() * 0.5f );
		trace_t tr;
		UTIL_TraceLine( pPlayer->Weapon_ShootPosition(), vecScan,
			MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr );
		vecEnd = tr.endpos;
	}

	// HL2SB (2026-10-03): the GM:DrawPhysgunBeam hook fires from the WEAPON
	// draw path only (weapon_physgun.cpp).  Firing it here as well would
	// double-feed the halo chain while both paths run.

	// Every sprite and the ribbon carry the HOLDER's weapon colour.
	Color clrW = HL2SB_GetWeaponColor( pPlayer->GetUserID() );
	byte r = clrW.r(), g = clrW.g(), b = clrW.b();

	// Idle: claw sprites only.
	DrawClawSprites( vecMuzzle, vecAimDir, bBoneTarget, r, g, b );

	if ( !m_bIsOn )
		return 1;

	// Ribbon: the Bezier control rides the aim direction at half the span.
	// A bone hold swaps the ribbon material and scroll direction.
	Vector vecControl = vecMuzzle + vecAimDir * ( vecEnd.DistTo( vecMuzzle ) * 0.5f );

	CMatRenderContextPtr pRenderContext( materials );
	IMaterial *pRibbon = bBoneTarget ? m_pBeamMaterialA : m_pBeamMaterial;
	pRenderContext->Bind( pRibbon );
	Vector vecRibbonColor( r / 255.0f, g / 255.0f, b / 255.0f );

	float flScroll = gpGlobals->curtime * ( bBoneTarget ? 2.5625f : -2.0f );
	DrawRibbon( vecMuzzle, vecControl, vecEnd, 2.0f, vecRibbonColor, flScroll );
	DrawRibbon( vecMuzzle, vecControl, vecEnd, random->RandomFloat( 2.0f, 5.0f ), vecRibbonColor, flScroll );
	DrawRibbon( vecMuzzle, vecControl, vecEnd, random->RandomFloat( 2.0f, 6.0f ), vecRibbonColor, flScroll );

	// End dot at the hold point.
	DrawEndDot( vecEnd, r, g, b, bBoneTarget );

	return 1;
}
