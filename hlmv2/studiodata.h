//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: Standalone studio model (.mdl/.vvd/.vtx/.phy) parser.
//
//          The viewer does NOT link the engine's mdlcache/materialsystem
//          stack. Instead it reads the model files with Qt file I/O and
//          reinterprets the raw bytes through the engine's own studio.h
//          structures. That is the same thing CMDLCache::UnserializeMDL() does
//          (a plain memcpy of the on-disk image into a studiohdr_t*), so the
//          structures in public/studio.h are known to be disk-layout
//          compatible in this repository. All accessors used here are the
//          self-relative inline ones (pBone(), pLocalSeqdesc(), ...) which do
//          not require runtime fixups.
//
//          mdllib was evaluated as the loader and rejected: public/mdllib/
//          mdllib.h only exposes LOD buffer *stripping* (StripModelBuffers /
//          IMdlStripInfo); it has no model inspection API at all, so there is
//          nothing to reuse for a viewer.
//
//          Data owned by this class:
//            - the raw .mdl buffer (studiohdr_t points into it)
//            - the raw .vvd buffer (mstudiovertex_t array, LOD0 fixups applied)
//            - the raw .vtx buffer (strip groups -> triangle index lists)
//            - the raw .phy buffer (header + solid key-value text summary)
//            - bind-pose / animated bone matrices (QMatrix4x4)
//
//=============================================================================

#ifndef HLMV2_STUDIODATA_H
#define HLMV2_STUDIODATA_H

#include <QByteArray>
#include <QMatrix4x4>
#include <QVector3D>
#include <QString>
#include <QStringList>
#include <QVector>

#include "studio.h"

class matrix3x4_t;

//-----------------------------------------------------------------------------
// Purpose: Disk-layout POD copy of mstudiovertex_t (48 bytes). The engine
//          struct forbids copying, which breaks QVector::resize; this twin is
//          used for the parsed vvd vertex array instead.
//-----------------------------------------------------------------------------
struct VvdVertex_t
{
	mstudioboneweight_t m_BoneWeights;
	Vector m_vecPosition;
	Vector m_vecNormal;
	Vector2D m_vecTexCoord;
};
static_assert( sizeof( VvdVertex_t ) == 48, "vvd vertex must stay 48 bytes" );

//-----------------------------------------------------------------------------
// Purpose: One drawable mesh: bodygroup state, skin ref, global vvd vertex
//          range and the triangle corner list from the .vtx strip data.
//-----------------------------------------------------------------------------
struct MeshDrawData_t
{
	int nBodypart;              // bodypart (bodygroup) this mesh belongs to
	int nModel;                 // sub-model (bodygroup variant) index
	int nMaterialRef;           // skinref index (resolve through GetMeshMaterial())
	int nVertexBase;            // global vvd vertex index of first mesh vertex
	int nVertexCount;
	QVector<uint32_t> indices;  // triangle corner list (3 per tri), mesh-local
	                            // vertex ids (0..nVertexCount-1)
};

//-----------------------------------------------------------------------------
// Purpose: A parsed texture/material entry from the mdl texture table.
//-----------------------------------------------------------------------------
struct TextureInfo_t
{
	QString name;               // e.g. "wood_crate001a" (.vtf extension stripped)
	int nFlags;
};

//-----------------------------------------------------------------------------
// Purpose: Physics (.phy) summary.
//-----------------------------------------------------------------------------
struct PhysicsSummary_t
{
	bool bLoaded = false;
	int nHeaderSize = 0;
	int nSolidCount = 0;
	uint32_t nChecksum = 0;     // must match studiohdr_t.checksum
	QStringList solidText;      // lines of the trailing "solid { ... }" key-value block
	float flMass = 0.0f;
	QString surfaceprop;
};

//-----------------------------------------------------------------------------
// Purpose: A parsed studio model + all peer files needed for inspection.
//-----------------------------------------------------------------------------
class CStudioModelData
{
public:
	CStudioModelData();
	~CStudioModelData();

	// Loads model.mdl and its peers (.vvd, .vtx, .phy) that exist next to it.
	// Returns false and fills sError on failure.
	bool LoadFromFile( const QString &mdlPath, QString &sError );

	bool IsValid() const { return m_pHdr != nullptr; }
	const QString &GetFileName() const { return m_sFileName; }
	const QString &GetModelName() const { return m_sModelName; }
	QString GetError() const { return m_sError; }
	QString GetMissingPeers() const { return m_sMissingPeers; }

	// --- Header summary ---
	int GetVersion() const { return m_pHdr ? m_pHdr->version : 0; }
	int GetChecksum() const { return m_pHdr ? m_pHdr->checksum : 0; }
	int GetBoneCount() const { return m_pHdr ? m_pHdr->numbones : 0; }
	// Includes the sequences inherited from $includemodel animation hosts
	// (e.g. c_arms.mdl -> c_arms_animations.mdl in HL2/GMod).
	int GetSequenceCount() const
	{
		return m_pHdr ? m_pHdr->numlocalseq +
			( m_pIncludeHdr ? m_pIncludeHdr->numlocalseq : 0 ) : 0;
	}
	int GetIncludeSequenceCount() const { return m_pIncludeHdr ? m_pIncludeHdr->numlocalseq : 0; }
	int GetTextureCount() const { return m_pHdr ? m_pHdr->numtextures : 0; }
	int GetBodypartCount() const { return m_pHdr ? m_pHdr->numbodyparts : 0; }
	int GetAttachmentCount() const { return m_pHdr ? m_pHdr->numlocalattachments : 0; }
	int GetSkinFamilies() const { return m_pHdr ? m_pHdr->numskinfamilies : 0; }
	int GetSkinRefs() const { return m_pHdr ? m_pHdr->numskinref : 0; }
	int GetPoseParameterCount() const { return m_pHdr ? m_pHdr->numlocalposeparameters : 0; }
	int GetHitboxSetCount() const { return m_pHdr ? m_pHdr->numhitboxsets : 0; }
	int GetIKChainCount() const { return m_pHdr ? m_pHdr->numikchains : 0; }
	int GetFlexDescCount() const { return m_pHdr ? m_pHdr->numflexdesc : 0; }
	int GetVertexCount() const { return m_nVvdVertexCount; }
	int GetTriangleCount() const { return m_nTriangleCount; }
	int GetMeshCount() const { return m_nMeshCount; }

	// --- Lists used by the UI panels ---
	const QVector<TextureInfo_t> &GetTextures() const { return m_lstTextures; }
	const QStringList &GetCdTextures() const { return m_lstCdTextures; }

	QString GetSequenceLabel( int iSeq, QString *activity = nullptr, float *fps = nullptr,
		int *numFrames = nullptr, bool *looping = nullptr, int *numBlends = nullptr ) const;
	QString GetBoneName( int iBone, int *parent = nullptr ) const;
	QString GetAttachmentName( int iAttach, int *bone = nullptr ) const;
	QString GetBodypartName( int iBodypart, int *numModels = nullptr ) const;
	QString GetModelName2( int iBodypart, int iModel ) const; // sub-model (bodygroup variant)
	QString GetPoseParameterName( int iPose, float *start = nullptr, float *end = nullptr, float *loop = nullptr ) const;

	// World-space attachment origin for the given bone matrices.
	struct Vec3 { float x, y, z; };
	bool GetAttachmentWorldTransform( int iAttach, const QVector<QMatrix4x4> &boneToWorld, Vec3 &outOrigin ) const;

	// --- Bodygroup/skin state ---
	void SetBodygroupSelection( int iBodypart, int iModel );
	void SetSkin( int nSkin );
	int GetBodygroupSelection( int iBodypart ) const { return m_lstBodygroupSelection.value( iBodypart, 0 ); }
	int GetSkin() const { return m_nSkin; }

	// Resolves a mesh's skinref index through the active skin family to a
	// global texture index.
	int GetMeshMaterial( int nMeshMaterialRef ) const;

	// Freezes the model in its bind pose (clears sequence playback). Used by
	// the viewer's "Bind pose" display option and by diagnostics.
	void SetBindPoseOnly();
	bool IsBindPoseOnly() const { return m_nCurrentSequence < 0; }

	// Looks up a bone index by name (case-insensitive), for bone merging.
	int FindBoneIndexByName( const QString &name ) const;

	// Bind-pose vertex bounds, used to align merged hands with the weapon.
	QVector3D GetBindBoundsCenter() const;
	float GetBindBoundsExtent() const;
	int GetDominantBoneIndex() const { return m_nDominantBone; }

	// Builds skinning transforms for this model when it is attached as a
	// "hands" model to owner: matching bone names reuse the owner's animated
	// transforms, so the hands follow the weapon's sequence. This mirrors the
	// original viewer's merged-model bone cache.
	QVector<QMatrix4x4> BuildMergedSkinningMatrices( CStudioModelData &owner );

	// --- Geometry for the viewport ---
	// Draw list honouring the current bodygroup selections.
	const QVector<MeshDrawData_t> &GetDrawList() const { return m_lstDrawList; }
	// Fat vertex array (mstudiovertex_t: position/normal/uv/boneweights).
	const VvdVertex_t *GetVvdVertices( int *pCount ) const;

	// --- Bones / animation ---
	const QVector<QMatrix4x4> &GetSetupBoneMatrices() const { return m_SetupBoneToWorld; }

	bool SetCurrentSequence( int iSeq );
	int GetCurrentSequence() const { return m_nCurrentSequence; }
	// Advances animation by dt seconds; marks bone matrices dirty.
	void AdvanceAnimation( float dt );
	// Control Panel's "Force To Frame" slider drives the cycle directly.
	void SetCycle( float flCycle );
	void SetPaused( bool bPaused ) { m_bPaused = bPaused; }
	bool GetPaused() const { return m_bPaused; }
	void SetPlaybackSpeed( float flSpeed ) { m_flPlaybackSpeed = flSpeed; }
	float GetPlaybackSpeed() const { return m_flPlaybackSpeed; }
	float GetCurrentCycle() const { return m_flCycle; }
	QString GetCurrentSequenceLabel() const;

	// Bone matrices for the current animation frame (rebuilds on demand).
	const QVector<QMatrix4x4> &GetAnimatedBoneMatrices();

	// Skinning matrices = current BoneToWorld * inverse(bind BoneToWorld).
	//
	// VVD vertices are already in pose (model) space, so binding to a bone
	// must apply only the *change* from the bind pose. The engine spells this
	// as BoneToWorld * poseToBone in ComputePoseToWorld(); poseToBone is the
	// inverse of the bind-pose BoneToWorld, but which copy to read depends on
	// whether the model has a mstudiolinearbone_t table (viewmodels do, and
	// their per-bone copy can disagree badly). Computing the inverse from the
	// setup pose is equivalent and works for every model without referencing
	// either table.
	const QVector<QMatrix4x4> &GetSkinningMatrices();

	const PhysicsSummary_t &GetPhysicsSummary() const { return m_PhysSummary; }

	// True when inline animation data could be decoded for the current
	// sequence (external .ani anim blocks are not supported).
	bool HasAnimationData() const { return m_bAnimDataAvailable; }

private:
	void Clear();
	bool ParseHeader( QString &sError );
	bool ParseVvd( const QString &basePath, QString &sError );
	bool ParseVtx( const QString &basePath, QString &sError );
	bool ParsePhy( const QString &basePath, QString &sError );
	bool LoadIncludeModels( const QString &mdlPath );
	const studiohdr_t *SequenceHeader( int iSeq, int *pLocalSeq ) const;
	void BuildDrawList();
	void BuildSetupBoneMatrices();
	void FillMeshMetadata();

	// Resolves a (possibly $includemodel) sequence to the engine CStudioHdr
	// that owns it and the local sequence index inside that header. The
	// engine's IBoneSetup is driven from that header so InitPose /
	// AccumulatePose / CalcAutoplaySequences are the stock implementations,
	// not a re-decoding of the animation streams.
	CStudioHdr *SequenceStudioHdr( int iSeq, int *pLocalSeq ) const;
	float SequenceDuration( int iSeq ) const;

private:
	QByteArray m_bufMdl;
	QByteArray m_bufIncludeMdl;         // $includemodel animation host
	studiohdr_t *m_pIncludeHdr = nullptr;
	QByteArray m_bufVvd;
	QByteArray m_bufVtx;
	QByteArray m_bufPhy;

	studiohdr_t *m_pHdr = nullptr;

	// Engine runtime views over the raw file buffers. The animation/bone
	// pipeline is public/bone_setup.cpp, compiled into this target; hlmv2
	// itself only supplies the bytes and the Qt front end.
	CStudioHdr *m_pStudioHdr = nullptr;
	CStudioHdr *m_pIncludeStudioHdr = nullptr;

	QString m_sFileName;
	QString m_sModelName;
	QString m_sError;
	QString m_sMissingPeers;

	QVector<TextureInfo_t> m_lstTextures;
	QStringList m_lstCdTextures;

	// Vvd data
	int m_nVvdVertexCount = 0;
	const VvdVertex_t *m_pVvdVertices = nullptr;  // points into m_bufVvd (fixups already flattened)
	QVector<VvdVertex_t> m_VvdFixed;              // used only when fixups require flattening

	// Vtx-derived geometry: every bodypart/model/mesh, in file order.
	QVector<MeshDrawData_t> m_lstAllDrawList;
	// Filtered by current bodygroup state.
	QVector<MeshDrawData_t> m_lstDrawList;
	int m_nTriangleCount = 0;
	int m_nMeshCount = 0;
	QVector<int> m_lstBodygroupSelection;
	int m_nSkin = 0;

	// Bones
	QVector<QMatrix4x4> m_SetupBoneToWorld;
	QVector<QMatrix4x4> m_AnimBoneToWorld;
	QVector<QMatrix4x4> m_BindInverse;      // inverse of m_SetupBoneToWorld
	QVector<QMatrix4x4> m_SkinningMatrices;
	float m_flModelExtent = 0.0f;           // bind-pose bbox diagonal, lazy
	int m_nDominantBone = -1;               // bone driving the most vertices
	bool m_bAnimDirty = true;

	// Animation state
	int m_nCurrentSequence = -1;
	float m_flCycle = 0.0f;
	float m_flAnimTime = 0.0f;       // realtime clock for autoplay layers
	bool m_bPaused = false;
	float m_flPlaybackSpeed = 1.0f;
	bool m_bAnimDataAvailable = false;

	PhysicsSummary_t m_PhysSummary;
};

#endif // HLMV2_STUDIODATA_H
