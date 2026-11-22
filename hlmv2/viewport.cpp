//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: Implementation of the Qt OpenGL studio viewport. See viewport.h
//          for the rendering route notes (route B2: own GL renderer, no
//          materialsystem attachment).
//
//=============================================================================

#include "viewport.h"

#include "studiodata.h"

#include <QColor>
#include <QMouseEvent>
#include <QOpenGLVertexArrayObject>
#include <QWheelEvent>
#include <QtGlobal>

#include <cmath>

namespace
{
	const char kMeshVertexShader[] = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uMvp;
out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;
void main()
{
	gl_Position = uMvp * vec4(aPos, 1.0);
	vNormal = aNormal;
	vUV = aUV;
	vWorldPos = aPos;
}
)";

	// Two-sided diffuse with a key/fill pair and a subtle rim. The old shader
	// used max(lambert, back-face lambert), which lit every face equally and
	// made untextured models read as one flat silhouette.
	const char kMeshFragmentShader[] = R"(
#version 330 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;
uniform vec3 uColor;
uniform vec3 uLightDir;
uniform vec3 uLightDir2;
uniform vec3 uEyePos;
uniform sampler2D uTex;
uniform bool uUseTexture;
uniform vec3 uTint;
uniform bool uUseTint;
out vec4 fragColor;
void main()
{
	vec3 n = normalize(vNormal + vec3(1e-8));
	if (!gl_FrontFacing)
		n = -n;
	vec4 texel = uUseTexture ? texture(uTex, vUV) : vec4(uColor, 1.0);
	vec3 base = texel.rgb;
	if (uUseTint)
	{
		// Approximate the engine's blendtintbybasealpha: the base texture's
		// alpha channel is the tint mask (gloves stay, skin takes the player
		// colour), and the tint replaces the masked pixels.
		base = mix(base, uTint, clamp(texel.a, 0.0, 1.0));
	}
	float key = max(dot(n, normalize(uLightDir)), 0.0);
	float fill = max(dot(n, normalize(uLightDir2)), 0.0);
	vec3 lit = base * (0.34 + 0.70 * key + 0.20 * fill);
	vec3 viewDir = normalize(uEyePos - vWorldPos);
	float rim = pow(1.0 - max(dot(n, viewDir), 0.0), 3.0) * 0.16;
	lit += base * rim;
	// VTF alpha is often an envmap/specular mask, not opacity (HL2 uses
	// $basealphaenvmapmask). Treat the model as opaque unless a proper
	// translucent material pass is added.
	fragColor = vec4(lit, 1.0);
}
)";

	const char kLineVertexShader[] = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main()
{
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

	const char kLineFragmentShader[] = R"(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main()
{
	fragColor = uColor;
}
)";

	// Purpose: Deterministic palette color per material slot so meshes remain
	//          distinguishable until real texture support lands.
	QVector3D PaletteColor( int nIndex )
	{
		float hue = fmod( nIndex * 0.618033989f, 1.0f );
		QColor color;
		color.setHslF( hue, 0.55f, 0.62f );
		return QVector3D( color.redF(), color.greenF(), color.blueF() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Construction/destruction
//-----------------------------------------------------------------------------
CModelViewport::CModelViewport( QWidget *parent )
	: QOpenGLWidget( parent )
{
	// GL 3.3 core is the lowest common denominator across Win/Linux/mac.
	QSurfaceFormat format;
	format.setVersion( 3, 3 );
	format.setProfile( QSurfaceFormat::CoreProfile );
	format.setDepthBufferSize( 24 );
	format.setSamples( 4 );
	setFormat( format );

	setMouseTracking( true );
	setFocusPolicy( Qt::StrongFocus );
}

CModelViewport::~CModelViewport()
{
	makeCurrent();
	for ( GpuMesh_t *pMesh : m_lstGpuMeshes )
	{
		if ( pMesh->bBuilt )
		{
			pMesh->vao.destroy();
			pMesh->vbo.destroy();
		}
		delete pMesh;
	}
	m_lstGpuMeshes.clear();
	qDeleteAll( m_lstTextures );
	m_lstTextures.clear();
	qDeleteAll( m_lstHandsTextures );
	m_lstHandsTextures.clear();
	m_GridVao.destroy();
	m_GridVbo.destroy();
	m_LineVao.destroy();
	m_LineVbo.destroy();
	doneCurrent();
}

//-----------------------------------------------------------------------------
// Purpose: Attaches or clears the parsed model.
//-----------------------------------------------------------------------------
void CModelViewport::SetModel( CStudioModelData *pModel )
{
	m_pModel = pModel;
	m_bFramedModel = false;

	// The GL context only exists after the first expose; defer the buffer
	// upload to paintGL when it does not (e.g. model loaded before show()).
	if ( m_bGlReady )
	{
		makeCurrent();
		RebuildMeshBuffers();
		doneCurrent();
	}
	else
	{
		m_bMeshDirty = true;
	}

	if ( pModel )
		FrameModel();

	update();
}

//-----------------------------------------------------------------------------
// Purpose: Attaches/clears the optional "hands" model. Buffers are rebuilt on
//          the next paint so this is safe before the GL context exists.
//-----------------------------------------------------------------------------
void CModelViewport::SetHandsModel( CStudioModelData *pHands )
{
	m_pHandsModel = pHands;
	if ( !pHands )
		m_HandsSkinning.clear();
	m_bMeshDirty = true;
	if ( m_pModel )
		FrameModel();
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Material images for the attached hands model.
//-----------------------------------------------------------------------------
void CModelViewport::SetHandsMaterialImages( const QVector<QImage> &images )
{
	m_lstHandsImages = images;
	m_bHandsTexturesDirty = true;
	if ( m_bGlReady )
	{
		makeCurrent();
		BuildTextures( 1 );
		doneCurrent();
	}
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Camera presets. The original viewer had a dedicated "Viewmodel
//          Mode" (origin camera, 54 degree FOV); ours keeps the orbit camera
//          but offers the standard orthographic-ish directions as well.
//-----------------------------------------------------------------------------
void CModelViewport::SetCameraPreset( int nPreset )
{
	switch ( nPreset )
	{
	case CamFront:     m_flCamYaw = 90.0f;  m_flCamPitch = 0.0f;   break;
	case CamBack:      m_flCamYaw = 270.0f; m_flCamPitch = 0.0f;   break;
	case CamLeft:      m_flCamYaw = 180.0f; m_flCamPitch = 0.0f;   break;
	case CamRight:     m_flCamYaw = 0.0f;   m_flCamPitch = 0.0f;   break;
	case CamTop:       m_flCamYaw = 45.0f;  m_flCamPitch = 88.0f;  break;
	case CamBottom:    m_flCamYaw = 45.0f;  m_flCamPitch = -88.0f; break;
	case CamViewmodel: m_flCamYaw = 270.0f; m_flCamPitch = 2.0f;   break;
	case CamPerspective:
	default:           m_flCamYaw = 45.0f;  m_flCamPitch = 20.0f;  break;
	}

	m_bFramedModel = false;
	if ( m_pModel )
		FrameModel();
	if ( nPreset == CamViewmodel )
		m_flCamDistance = qMax( 24.0f, m_flCamDistance * 0.55f );
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Records which material slots accept an engine colour.
//-----------------------------------------------------------------------------
void CModelViewport::SetTintableSlots( const QVector<bool> &tintableSlots )
{
	m_lstTintable = tintableSlots;
	update();
}

void CModelViewport::SetHandsTintableSlots( const QVector<bool> &tintableSlots )
{
	m_lstHandsTintable = tintableSlots;
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Rebuilds mesh buffers for in-place model changes (keeps camera).
//-----------------------------------------------------------------------------
void CModelViewport::RefreshModel()
{
	if ( m_bGlReady )
	{
		makeCurrent();
		RebuildMeshBuffers();
		doneCurrent();
	}
	else
	{
		m_bMeshDirty = true;
	}
	update();
}

//-----------------------------------------------------------------------------
// Purpose: GL context setup.
//-----------------------------------------------------------------------------
void CModelViewport::initializeGL()
{
	initializeOpenGLFunctions();

	QString sError;
	if ( !BuildPrograms( sError ) )
	{
		m_sGlError = sError;
		m_bGlReady = false;
		return;
	}

	// Ground grid: lines on the XY plane (Z up), 16 units apart like the
	// Hammer grid.
	QVector<float> grid;
	const int nExtent = 512;
	const int nStep = 16;
	for ( int i = -nExtent; i <= nExtent; i += nStep )
	{
		grid << (float)i << -nExtent << 0.0f << (float)i << nExtent << 0.0f; // Y lines
		grid << -nExtent << (float)i << 0.0f << nExtent << (float)i << 0.0f; // X lines
	}
	m_nGridVertexCount = grid.count() / 3;

	m_GridVao.create();
	m_GridVao.bind();
	m_GridVbo.create();
	m_GridVbo.bind();
	m_GridVbo.allocate( grid.constData(), grid.count() * (int)sizeof( float ) );
	glEnable( GL_VERTEX_PROGRAM_POINT_SIZE );
	glDisableVertexAttribArray( 1 );
	glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), (const void *)0 );
	glEnableVertexAttribArray( 0 );
	m_GridVbo.release();
	m_GridVao.release();

	m_LineVao.create();
	m_LineVbo.create();

	glEnable( GL_DEPTH_TEST );
	glEnable( GL_MULTISAMPLE );
	glClearColor( 0.22f, 0.24f, 0.27f, 1.0f );

	m_bGlReady = true;
}

//-----------------------------------------------------------------------------
// Purpose: Compiles the two tiny programs used by the viewport.
//-----------------------------------------------------------------------------
bool CModelViewport::BuildPrograms( QString &sError )
{
	if ( !m_MeshProgram.addShaderFromSourceCode( QOpenGLShader::Vertex, kMeshVertexShader ) ||
		!m_MeshProgram.addShaderFromSourceCode( QOpenGLShader::Fragment, kMeshFragmentShader ) ||
		!m_MeshProgram.link() )
	{
		sError = QStringLiteral( "mesh program: %1" ).arg( m_MeshProgram.log() );
		return false;
	}
	m_nMeshMvpLoc = m_MeshProgram.uniformLocation( "uMvp" );
	m_nMeshColorLoc = m_MeshProgram.uniformLocation( "uColor" );
	m_nMeshLightLoc = m_MeshProgram.uniformLocation( "uLightDir" );
	m_nMeshLight2Loc = m_MeshProgram.uniformLocation( "uLightDir2" );
	m_nMeshEyeLoc = m_MeshProgram.uniformLocation( "uEyePos" );
	m_nMeshTexLoc = m_MeshProgram.uniformLocation( "uTex" );
	m_nMeshUseTexLoc = m_MeshProgram.uniformLocation( "uUseTexture" );
	m_nMeshTintLoc = m_MeshProgram.uniformLocation( "uTint" );
	m_nMeshUseTintLoc = m_MeshProgram.uniformLocation( "uUseTint" );

	if ( !m_LineProgram.addShaderFromSourceCode( QOpenGLShader::Vertex, kLineVertexShader ) ||
		!m_LineProgram.addShaderFromSourceCode( QOpenGLShader::Fragment, kLineFragmentShader ) ||
		!m_LineProgram.link() )
	{
		sError = QStringLiteral( "line program: %1" ).arg( m_LineProgram.log() );
		return false;
	}
	m_nLineMvpLoc = m_LineProgram.uniformLocation( "uMvp" );
	m_nLineColorLoc = m_LineProgram.uniformLocation( "uColor" );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Attaches decoded VTF images by studio texture slot.
//-----------------------------------------------------------------------------
void CModelViewport::SetMaterialImages( const QVector<QImage> &images )
{
	m_lstMaterialImages = images;
	m_bTexturesDirty = true;
	if ( m_bGlReady )
	{
		makeCurrent();
		BuildTextures( 0 );
		doneCurrent();
	}
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Uploads the cached images into GL textures (GL context required).
//          nBatch 0 = main model, 1 = attached hands.
//-----------------------------------------------------------------------------
void CModelViewport::BuildTextures( int nBatch )
{
	QVector<QImage> &images = ( nBatch == 1 ) ? m_lstHandsImages : m_lstMaterialImages;
	QVector<QOpenGLTexture *> &textures = ( nBatch == 1 ) ? m_lstHandsTextures : m_lstTextures;
	bool &bDirty = ( nBatch == 1 ) ? m_bHandsTexturesDirty : m_bTexturesDirty;

	if ( !m_bGlReady || !bDirty )
		return;

	qDeleteAll( textures );
	textures.clear();
	textures.resize( images.count() );
	for ( int i = 0; i < images.count(); ++i )
	{
		const QImage &img = images[ i ];
		if ( img.isNull() )
			continue;
		QOpenGLTexture *pTex = new QOpenGLTexture( QOpenGLTexture::Target2D );
		pTex->setWrapMode( QOpenGLTexture::Repeat );
		pTex->setMinificationFilter( QOpenGLTexture::LinearMipMapLinear );
		pTex->setMagnificationFilter( QOpenGLTexture::Linear );
		pTex->setData( img.convertToFormat( QImage::Format_RGBA8888 ),
			QOpenGLTexture::GenerateMipMaps );
		if ( !pTex->isCreated() )
		{
			delete pTex;
			continue;
		}
		textures[ i ] = pTex;
	}
	bDirty = false;
}

//-----------------------------------------------------------------------------
// Purpose: Rebuilds per-mesh VAOs/VBOs from the current draw list.
//-----------------------------------------------------------------------------
void CModelViewport::RebuildMeshBuffers()
{
	for ( GpuMesh_t *pMesh : m_lstGpuMeshes )
	{
		if ( pMesh->bBuilt )
		{
			pMesh->vao.destroy();
			pMesh->vbo.destroy();
		}
		delete pMesh;
	}
	m_lstGpuMeshes.clear();
	m_lstSkinned.clear();

	m_bMeshDirty = false;
	BuildTextures( 0 );
	BuildTextures( 1 );

	if ( !m_pModel || !m_bGlReady )
		return;

	// Two batches share one flat mesh list: main model (0) and optional
	// bone-merged hands (1). Keeping the list flat lets the drawing loop stay
	// a single pass while each mesh still knows which model/texture set it
	// belongs to.
	for ( int nBatch = 0; nBatch < 2; ++nBatch )
	{
		CStudioModelData *pModel = ( nBatch == 0 ) ? m_pModel : m_pHandsModel;
		if ( !pModel )
			continue;

		const QVector<MeshDrawData_t> &drawList = pModel->GetDrawList();
		for ( int iDraw = 0; iDraw < drawList.count(); ++iDraw )
		{
			const MeshDrawData_t &draw = drawList[ iDraw ];

			GpuMesh_t *pMesh = new GpuMesh_t();
			m_lstGpuMeshes.append( pMesh );
			m_lstSkinned.resize( m_lstGpuMeshes.count() );
			GpuMesh_t &mesh = *pMesh;

			const int nTextureIndex = pModel->GetMeshMaterial( draw.nMaterialRef );
			mesh.nTextureIndex = nTextureIndex;
			mesh.nBatch = nBatch;
			mesh.nDrawIndex = iDraw;
			mesh.vecColor = PaletteColor(
				( nTextureIndex < 0 ? iDraw : nTextureIndex ) + nBatch * 7 );
			mesh.bTintable = false;
			{
				const QVector<bool> &tintable = ( nBatch == 1 )
					? m_lstHandsTintable : m_lstTintable;
				if ( nTextureIndex >= 0 && nTextureIndex < tintable.count() )
					mesh.bTintable = tintable[ nTextureIndex ];
			}

			// Flat, non-indexed corner stream: position/normal/uv per corner.
			mesh.nVertexCount = draw.indices.count();
			mesh.vao.create();
			mesh.vao.bind();
			mesh.vbo.create();
			mesh.vbo.bind();
			mesh.vbo.allocate( draw.indices.count() * 8 * (int)sizeof( float ) );
			mesh.vbo.release();
			mesh.vao.release();
			mesh.bBuilt = true;
		}
	}

	UpdateSkinnedVertices();
}

//-----------------------------------------------------------------------------
// Purpose: CPU skinning: transforms every referenced vertex through its bone
//          matrices into world space, in index order so the renderer can
//          stream position/normal pairs.
//
//          Uses the animated matrices when the sequence provides data,
//          otherwise the setup (bind pose) matrices.
//-----------------------------------------------------------------------------
void CModelViewport::UpdateSkinnedVertices()
{
	if ( !m_pModel || m_lstSkinned.count() != m_lstGpuMeshes.count() )
		return;

	// Hands are driven by the main model's animated bones, matched by name.
	// Recompute per frame so playback stays in sync (the original viewer's
	// "animate weapons" behaviour).
	if ( m_pHandsModel )
		m_HandsSkinning = m_pHandsModel->BuildMergedSkinningMatrices( *m_pModel );

	for ( int iMesh = 0; iMesh < m_lstGpuMeshes.count(); ++iMesh )
	{
		GpuMesh_t *pGpu = m_lstGpuMeshes[ iMesh ];
		if ( !pGpu || !pGpu->bBuilt )
			continue;

		CStudioModelData *pModel = ( pGpu->nBatch == 0 ) ? m_pModel : m_pHandsModel;
		if ( !pModel )
			continue;
		const QVector<MeshDrawData_t> &drawList = pModel->GetDrawList();
		if ( pGpu->nDrawIndex < 0 || pGpu->nDrawIndex >= drawList.count() )
			continue;
		const MeshDrawData_t &draw = drawList[ pGpu->nDrawIndex ];

		int nVertexCount = 0;
		const VvdVertex_t *pVertices = pModel->GetVvdVertices( &nVertexCount );
		if ( !pVertices )
			continue;

		// Pose-space vertices: use the bone's change from its bind pose.
		const QVector<QMatrix4x4> &bones = ( pGpu->nBatch == 0 )
			? pModel->GetSkinningMatrices() : m_HandsSkinning;

		SkinnedMesh_t &skin = m_lstSkinned[ iMesh ];

		skin.positions.resize( draw.indices.count() * 3 );
		skin.normals.resize( draw.indices.count() * 3 );
		skin.uvs.resize( draw.indices.count() * 2 );

		float *pPos = skin.positions.data();
		float *pNrm = skin.normals.data();
		float *pUV = skin.uvs.data();

		for ( int i = 0; i < draw.indices.count(); ++i )
		{
			uint32_t localIndex = draw.indices[ i ];
			int nGlobal = draw.nVertexBase + (int)localIndex;
			if ( nGlobal < 0 || nGlobal >= nVertexCount )
			{
				pPos[ 0 ] = pPos[ 1 ] = pPos[ 2 ] = 0.0f;
				pNrm[ 0 ] = 0.0f; pNrm[ 1 ] = 0.0f; pNrm[ 2 ] = 1.0f;
				pUV[ 0 ] = pUV[ 1 ] = 0.0f;
				pPos += 3; pNrm += 3; pUV += 2;
				continue;
			}

			const VvdVertex_t &vertex = pVertices[ nGlobal ];
			const mstudioboneweight_t &weights = vertex.m_BoneWeights;

			// Skin the position through the (up to 3) weighted bones.
			QVector4D skinned( 0.0f, 0.0f, 0.0f, 0.0f );
			QVector4D normalSkinned( 0.0f, 0.0f, 0.0f, 0.0f );
			QVector4D bindPos( vertex.m_vecPosition.x, vertex.m_vecPosition.y, vertex.m_vecPosition.z, 1.0f );
			QVector4D bindNrm( vertex.m_vecNormal.x, vertex.m_vecNormal.y, vertex.m_vecNormal.z, 0.0f );

			int nBones = (int)weights.numbones;
			if ( nBones <= 0 || nBones > 3 )
				nBones = 1;
			float flWeightSum = 0.0f;
			for ( int w = 0; w < nBones; ++w )
			{
				int iBone = weights.bone[ w ];
				float flWeight = weights.weight[ w ];
				if ( iBone < 0 || iBone >= bones.count() )
					continue;
				const QMatrix4x4 &bone = bones[ iBone ];
				skinned += ( bone * bindPos ) * flWeight;
				normalSkinned += ( bone * bindNrm ) * flWeight;
				flWeightSum += flWeight;
			}
			if ( flWeightSum <= 0.0f )
			{
				// No valid bone contribution: keep the bind pose (vvd vertices
				// are already in model space for static/unweighted meshes).
				skinned = bindPos;
				normalSkinned = bindNrm;
			}
			else
			{
				// Normalize so partially weighted / clipped vertices do not
				// collapse towards the origin (w < 1 used to shrink them).
				skinned /= flWeightSum;
				normalSkinned /= flWeightSum;
			}

			pPos[ 0 ] = skinned.x(); pPos[ 1 ] = skinned.y(); pPos[ 2 ] = skinned.z();
			pNrm[ 0 ] = normalSkinned.x(); pNrm[ 1 ] = normalSkinned.y(); pNrm[ 2 ] = normalSkinned.z();
			pUV[ 0 ] = vertex.m_vecTexCoord.x; pUV[ 1 ] = vertex.m_vecTexCoord.y;
			pPos += 3; pNrm += 3; pUV += 2;
		}

		skin.bValid = true;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Centers and distances the camera on the model bounds.
//-----------------------------------------------------------------------------
void CModelViewport::FrameModel()
{
	if ( !m_pModel )
		return;

	int nVertexCount = 0;
	const VvdVertex_t *pVertices = m_pModel->GetVvdVertices( &nVertexCount );
	if ( !pVertices || nVertexCount <= 0 )
		return;

	// Bounds over the drawn meshes only.
	QVector3D vecMin( 1e9f, 1e9f, 1e9f );
	QVector3D vecMax( -1e9f, -1e9f, -1e9f );
	const QVector<QMatrix4x4> &bones = m_pModel->GetSkinningMatrices();
	for ( const MeshDrawData_t &draw : m_pModel->GetDrawList() )
	{
		for ( int i = 0; i < draw.nVertexCount && draw.nVertexBase >= 0; ++i )
		{
			int nGlobal = draw.nVertexBase + i;
			if ( nGlobal < 0 || nGlobal >= nVertexCount )
				continue;
			const VvdVertex_t &vertex = pVertices[ nGlobal ];
			// Respect the bone transform so posed models frame correctly.
			QVector4D p( vertex.m_vecPosition.x, vertex.m_vecPosition.y, vertex.m_vecPosition.z, 1.0f );
			int iBone = vertex.m_BoneWeights.bone[ 0 ];
			QVector4D w = ( iBone >= 0 && iBone < bones.count() ) ? ( bones[ iBone ] * p ) : p;
			vecMin.setX( qMin( vecMin.x(), w.x() ) );
			vecMin.setY( qMin( vecMin.y(), w.y() ) );
			vecMin.setZ( qMin( vecMin.z(), w.z() ) );
			vecMax.setX( qMax( vecMax.x(), w.x() ) );
			vecMax.setY( qMax( vecMax.y(), w.y() ) );
			vecMax.setZ( qMax( vecMax.z(), w.z() ) );
		}
	}

	if ( vecMin.x() > vecMax.x() )
		return; // empty draw list

	m_vecTarget = ( vecMin + vecMax ) * 0.5f;
	QVector3D extent = vecMax - vecMin;
	float flRadius = extent.length() * 0.5f;
	if ( m_pHandsModel )
		flRadius = qMax( flRadius, m_pHandsModel->GetBindBoundsExtent() * 0.55f );
	m_flCamDistance = qMax( flRadius * 2.6f, 32.0f );
	m_bFramedModel = true;
}

//-----------------------------------------------------------------------------
// Purpose: Restores the default orbit camera and re-frames the model.
//-----------------------------------------------------------------------------
void CModelViewport::ResetCamera()
{
	m_flCamYaw = 45.0f;
	m_flCamPitch = 20.0f;
	m_bFramedModel = false;
	if ( m_pModel )
		FrameModel();
	update();
}

//-----------------------------------------------------------------------------
// Purpose: One-time camera framing on first resize with a model loaded.
//-----------------------------------------------------------------------------
void CModelViewport::resizeGL( int w, int h )
{
	Q_UNUSED( w );
	Q_UNUSED( h );
	if ( m_pModel && !m_bFramedModel )
		FrameModel();
}

//-----------------------------------------------------------------------------
// Purpose: Renders the scene.
//-----------------------------------------------------------------------------
void CModelViewport::paintGL()
{
	glClearColor( 0.22f, 0.24f, 0.27f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	if ( !m_bGlReady )
		return;

	// Buffers requested before the context existed (model loaded pre-show)
	// are uploaded on the first real paint.
	if ( m_pModel && m_bMeshDirty )
		RebuildMeshBuffers();
	if ( m_bHandsTexturesDirty )
		BuildTextures( 1 );

	// View/projection (Z-up world, camera orbits the target).
	QMatrix4x4 proj;
	proj.perspective( 54.0f, (float)width() / (float)height(), 1.0f, 16384.0f );

	float flYawRad = qDegreesToRadians( m_flCamYaw );
	float flPitchRad = qDegreesToRadians( m_flCamPitch );
	QVector3D eye(
		m_vecTarget.x() + m_flCamDistance * cos( flPitchRad ) * cos( flYawRad ),
		m_vecTarget.y() + m_flCamDistance * cos( flPitchRad ) * sin( flYawRad ),
		m_vecTarget.z() + m_flCamDistance * sin( flPitchRad ) );
	QMatrix4x4 view;
	view.lookAt( eye, m_vecTarget, QVector3D( 0.0f, 0.0f, 1.0f ) );

	QMatrix4x4 mvp = proj * view;

	if ( m_pModel )
		UpdateSkinnedVertices();

	DrawGrid();
	DrawModel();
	if ( m_pModel )
	{
		DrawBones();
		DrawAttachments();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Ground grid.
//-----------------------------------------------------------------------------
void CModelViewport::DrawGrid()
{
	if ( !m_bShowGround )
		return;

	m_LineProgram.bind();
	QMatrix4x4 proj, view;
	proj.perspective( 54.0f, (float)width() / (float)height(), 1.0f, 16384.0f );
	float flYawRad = qDegreesToRadians( m_flCamYaw );
	float flPitchRad = qDegreesToRadians( m_flCamPitch );
	QVector3D eye(
		m_vecTarget.x() + m_flCamDistance * cos( flPitchRad ) * cos( flYawRad ),
		m_vecTarget.y() + m_flCamDistance * cos( flPitchRad ) * sin( flYawRad ),
		m_vecTarget.z() + m_flCamDistance * sin( flPitchRad ) );
	view.lookAt( eye, m_vecTarget, QVector3D( 0.0f, 0.0f, 1.0f ) );
	m_LineProgram.setUniformValue( m_nLineMvpLoc, proj * view );
	m_LineProgram.setUniformValue( m_nLineColorLoc, QVector4D( 0.36f, 0.39f, 0.44f, 1.0f ) );

	m_GridVao.bind();
	glDrawArrays( GL_LINES, 0, m_nGridVertexCount );
	m_GridVao.release();
	m_LineProgram.release();
}

//-----------------------------------------------------------------------------
// Purpose: Model triangles or wireframe.
//-----------------------------------------------------------------------------
void CModelViewport::DrawModel()
{
	if ( !m_pModel )
		return;

	QMatrix4x4 proj, view;
	proj.perspective( 54.0f, (float)width() / (float)height(), 1.0f, 16384.0f );
	float flYawRad = qDegreesToRadians( m_flCamYaw );
	float flPitchRad = qDegreesToRadians( m_flCamPitch );
	QVector3D eye(
		m_vecTarget.x() + m_flCamDistance * cos( flPitchRad ) * cos( flYawRad ),
		m_vecTarget.y() + m_flCamDistance * cos( flPitchRad ) * sin( flYawRad ),
		m_vecTarget.z() + m_flCamDistance * sin( flPitchRad ) );
	view.lookAt( eye, m_vecTarget, QVector3D( 0.0f, 0.0f, 1.0f ) );
	QMatrix4x4 mvp = proj * view;

	if ( m_bWireframe )
	{
		// Wireframe-only mode: reuse the triangle index stream in GL_LINES
		// adjacency via a geometry-free trick: draw each triangle's edges.
		// (Drawing GL_TRIANGLES with a wireframe polygon mode is not
		// available in core GL, so edges come from an expanded index pass.)
		m_LineProgram.bind();
		m_LineProgram.setUniformValue( m_nLineMvpLoc, mvp );
		m_LineProgram.setUniformValue( m_nLineColorLoc, QVector4D( 0.85f, 0.88f, 0.55f, 1.0f ) );

		for ( int i = 0; i < m_lstSkinned.count(); ++i )
		{
			const SkinnedMesh_t &skin = m_lstSkinned[ i ];
			if ( !skin.bValid )
				continue;

			// Build edge lines from consecutive triangle corners.
			const QVector<float> &p = skin.positions;
			QVector<float> lines;
			const int nCorners = p.count() / 3;
			lines.reserve( nCorners * 6 );
			for ( int t = 0; t + 2 < nCorners; t += 3 )
			{
				for ( int e = 0; e < 3; ++e )
				{
					int a = t + e;
					int b = t + ( e + 1 ) % 3;
					lines << p[ a * 3 ] << p[ a * 3 + 1 ] << p[ a * 3 + 2 ];
					lines << p[ b * 3 ] << p[ b * 3 + 1 ] << p[ b * 3 + 2 ];
				}
			}

			if ( lines.isEmpty() )
				continue;

			m_LineVao.bind();
			m_LineVbo.bind();
			m_LineVbo.allocate( lines.constData(), lines.count() * (int)sizeof( float ) );
			glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), (const void *)0 );
			glEnableVertexAttribArray( 0 );
			glDrawArrays( GL_LINES, 0, lines.count() / 3 );
			m_LineVbo.release();
			m_LineVao.release();
		}
		m_LineProgram.release();
		return;
	}

	m_MeshProgram.bind();
	m_MeshProgram.setUniformValue( m_nMeshMvpLoc, mvp );
	m_MeshProgram.setUniformValue( m_nMeshLightLoc, QVector3D( 0.4f, 0.35f, 0.85f ).normalized() );
	m_MeshProgram.setUniformValue( m_nMeshLight2Loc, QVector3D( -0.6f, 0.5f, 0.2f ).normalized() );
	m_MeshProgram.setUniformValue( m_nMeshEyeLoc, eye );
	m_MeshProgram.setUniformValue( m_nMeshTexLoc, 0 );

	for ( int i = 0; i < m_lstGpuMeshes.count() && i < m_lstSkinned.count(); ++i )
	{
		GpuMesh_t *pMesh = m_lstGpuMeshes[ i ];
		if ( !pMesh )
			continue;
		GpuMesh_t &mesh = *pMesh;
		const SkinnedMesh_t &skin = m_lstSkinned[ i ];
		if ( !mesh.bBuilt || !skin.bValid )
			continue;

		const int nCorners = mesh.nVertexCount;
		if ( nCorners <= 0 || skin.uvs.count() != nCorners * 2 )
			continue;

		// Successful VTF decode -> texture; otherwise keep the deterministic
		// palette color so models with missing content still render. Hands
		// have their own texture set.
		const QVector<QOpenGLTexture *> &textures =
			( mesh.nBatch == 1 ) ? m_lstHandsTextures : m_lstTextures;
		QOpenGLTexture *pTex = ( m_bTexturesEnabled && mesh.nTextureIndex >= 0 &&
			mesh.nTextureIndex < textures.count() )
			? textures[ mesh.nTextureIndex ] : nullptr;
		m_MeshProgram.setUniformValue( m_nMeshColorLoc, mesh.vecColor );
		m_MeshProgram.setUniformValue( m_nMeshUseTexLoc, pTex != nullptr );
		const bool bUseTint = mesh.bTintable;
		m_MeshProgram.setUniformValue( m_nMeshUseTintLoc, bUseTint );
		if ( bUseTint )
			m_MeshProgram.setUniformValue( m_nMeshTintLoc, m_vecTint );
		if ( pTex )
			pTex->bind();

		mesh.vao.bind();
		mesh.vbo.bind();
		// Stream all three streams into the preallocated interleaved-by-section
		// VBO: [positions | normals | uvs]. The old code only wrote positions,
		// leaving the normal section uninitialized -> every face shaded the
		// same dark color.
		const int nPosBytes = nCorners * 3 * (int)sizeof( float );
		const int nNrmBytes = nPosBytes;
		const int nUvBytes = nCorners * 2 * (int)sizeof( float );
		mesh.vbo.write( 0, skin.positions.constData(), nPosBytes );
		mesh.vbo.write( nPosBytes, skin.normals.constData(), nNrmBytes );
		mesh.vbo.write( nPosBytes + nNrmBytes, skin.uvs.constData(), nUvBytes );

		glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), (const void *)0 );
		glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), (const void *)(size_t)nPosBytes );
		glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof( float ), (const void *)(size_t)( nPosBytes + nNrmBytes ) );
		glEnableVertexAttribArray( 0 );
		glEnableVertexAttribArray( 1 );
		glEnableVertexAttribArray( 2 );
		glDrawArrays( GL_TRIANGLES, 0, nCorners );
		mesh.vbo.release();
		mesh.vao.release();
		if ( pTex )
			pTex->release();
	}
	m_MeshProgram.release();
}

//-----------------------------------------------------------------------------
// Purpose: Generic dynamic line batch.
//-----------------------------------------------------------------------------
void CModelViewport::DrawLines( const QVector<float> &verts, const QVector3D &color )
{
	if ( verts.isEmpty() )
		return;

	QMatrix4x4 proj, view;
	proj.perspective( 54.0f, (float)width() / (float)height(), 1.0f, 16384.0f );
	float flYawRad = qDegreesToRadians( m_flCamYaw );
	float flPitchRad = qDegreesToRadians( m_flCamPitch );
	QVector3D eye(
		m_vecTarget.x() + m_flCamDistance * cos( flPitchRad ) * cos( flYawRad ),
		m_vecTarget.y() + m_flCamDistance * cos( flPitchRad ) * sin( flYawRad ),
		m_vecTarget.z() + m_flCamDistance * sin( flPitchRad ) );
	view.lookAt( eye, m_vecTarget, QVector3D( 0.0f, 0.0f, 1.0f ) );

	m_LineProgram.bind();
	m_LineProgram.setUniformValue( m_nLineMvpLoc, proj * view );
	m_LineProgram.setUniformValue( m_nLineColorLoc, QVector4D( color, 1.0f ) );

	m_LineVao.bind();
	m_LineVbo.bind();
	m_LineVbo.allocate( verts.constData(), verts.count() * (int)sizeof( float ) );
	glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), (const void *)0 );
	glEnableVertexAttribArray( 0 );
	glDrawArrays( GL_LINES, 0, verts.count() / 3 );
	m_LineVbo.release();
	m_LineVao.release();
	m_LineProgram.release();
}

//-----------------------------------------------------------------------------
// Purpose: Bone skeleton lines + joint dots (Z-up world space).
//-----------------------------------------------------------------------------
void CModelViewport::DrawBones()
{
	if ( !m_bShowBones || !m_pModel )
		return;

	const QVector<QMatrix4x4> &bones = m_pModel->GetAnimatedBoneMatrices();
	const int nBones = m_pModel->GetBoneCount();
	if ( nBones <= 0 || bones.count() < nBones )
		return;

	QVector<float> lines;
	for ( int i = 0; i < nBones; ++i )
	{
		QVector4D childPos = bones[ i ].column( 3 );
		int iParent = -1;
		m_pModel->GetBoneName( i, &iParent );
		if ( iParent < 0 || iParent >= nBones )
			continue;
		QVector4D parentPos = bones[ iParent ].column( 3 );
		lines << childPos.x() << childPos.y() << childPos.z();
		lines << parentPos.x() << parentPos.y() << parentPos.z();
	}

	DrawLines( lines, QVector3D( 0.3f, 0.9f, 0.5f ) );
}

//-----------------------------------------------------------------------------
// Purpose: Attachment crosses in world space.
//-----------------------------------------------------------------------------
void CModelViewport::DrawAttachments()
{
	if ( !m_bShowAttachments || !m_pModel )
		return;

	const QVector<QMatrix4x4> &bones = m_pModel->GetAnimatedBoneMatrices();
	const int nAttachments = m_pModel->GetAttachmentCount();
	if ( nAttachments <= 0 )
		return;

	QVector<float> lines;
	for ( int i = 0; i < nAttachments; ++i )
	{
		CStudioModelData::Vec3 origin;
		if ( !m_pModel->GetAttachmentWorldTransform( i, bones, origin ) )
			continue;
		const float s = 3.0f;
		lines << origin.x - s << origin.y << origin.z << origin.x + s << origin.y << origin.z;
		lines << origin.x << origin.y - s << origin.z << origin.x << origin.y + s << origin.z;
		lines << origin.x << origin.y << origin.z - s << origin.x << origin.y << origin.z + s;
	}

	DrawLines( lines, QVector3D( 1.0f, 0.55f, 0.2f ) );
}

//-----------------------------------------------------------------------------
// Purpose: Orbit camera input.
//-----------------------------------------------------------------------------
void CModelViewport::mousePressEvent( QMouseEvent *event )
{
	m_ptLastMouse = event->pos();
	if ( event->button() == Qt::LeftButton )
		m_bRotating = true;
	else if ( event->button() == Qt::RightButton || event->button() == Qt::MiddleButton )
		m_bPanning = true;
}

void CModelViewport::mouseReleaseEvent( QMouseEvent *event )
{
	if ( event->button() == Qt::LeftButton )
		m_bRotating = false;
	if ( event->button() == Qt::RightButton || event->button() == Qt::MiddleButton )
		m_bPanning = false;
}

void CModelViewport::mouseMoveEvent( QMouseEvent *event )
{
	QPoint delta = event->pos() - m_ptLastMouse;
	m_ptLastMouse = event->pos();

	if ( m_bRotating )
	{
		m_flCamYaw += delta.x() * 0.4f;
		m_flCamPitch += delta.y() * 0.4f;
		m_flCamPitch = qBound( -89.0f, m_flCamPitch, 89.0f );
		update();
	}
	else if ( m_bPanning )
	{
		// Pan in the camera plane; scale by distance so it feels linear.
		float flScale = m_flCamDistance * 0.0015f;
		float flYawRad = qDegreesToRadians( m_flCamYaw );
		QVector3D right( sin( flYawRad ), -cos( flYawRad ), 0.0f );
		QVector3D up( cos( flYawRad ), sin( flYawRad ), 0.0f );
		m_vecTarget -= right * delta.x() * flScale;
		m_vecTarget -= up * delta.y() * flScale * cos( qDegreesToRadians( m_flCamPitch ) );
		m_vecTarget.setZ( m_vecTarget.z() + delta.y() * flScale * sin( qDegreesToRadians( m_flCamPitch ) ) );
		update();
	}
}

void CModelViewport::wheelEvent( QWheelEvent *event )
{
	float flStep = event->angleDelta().y() / 120.0f;
	m_flCamDistance *= pow( 0.85f, flStep );
	m_flCamDistance = qBound( 4.0f, m_flCamDistance, 8192.0f );
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Self-test helper: ensures a context exists, renders a frame and
//          reports the outcome (used headless with the offscreen platform).
//-----------------------------------------------------------------------------
bool CModelViewport::RenderOneFrameForSelfTest()
{
	// grabFramebuffer() drives the whole documented pipeline: it creates the
	// context and FBO (transient when the widget was never exposed), runs
	// initializeGL/paintGL and reads the result back. makeCurrent() alone is
	// a no-op until the widget has been exposed, which is why the context is
	// validated afterwards.
	const QImage frame = grabFramebuffer();
	const QOpenGLContext *pContext = context();
	return ( !frame.isNull() && pContext != nullptr && pContext->isValid() );
}

//-----------------------------------------------------------------------------
// Purpose: Returns the current framebuffer as an image.
//-----------------------------------------------------------------------------
QImage CModelViewport::GrabFrame()
{
	return grabFramebuffer();
}

//-----------------------------------------------------------------------------
// Purpose: Reports what the CPU skinner actually produced versus where the
//          camera is looking. A mismatch here means FrameModel() framed the
//          wrong bounds (e.g. its simplified single-bone transform differs
//          from the weighted skinning used for drawing).
//-----------------------------------------------------------------------------
QString CModelViewport::GetRenderDiagnostics() const
{
	QVector3D vMin( 1e9f, 1e9f, 1e9f );
	QVector3D vMax( -1e9f, -1e9f, -1e9f );
	int nCorners = 0;
	for ( const SkinnedMesh_t &skin : m_lstSkinned )
	{
		if ( !skin.bValid )
			continue;
		for ( int i = 0; i + 2 < skin.positions.count(); i += 3 )
		{
			QVector3D p( skin.positions[ i ], skin.positions[ i + 1 ], skin.positions[ i + 2 ] );
			vMin.setX( qMin( vMin.x(), p.x() ) );
			vMin.setY( qMin( vMin.y(), p.y() ) );
			vMin.setZ( qMin( vMin.z(), p.z() ) );
			vMax.setX( qMax( vMax.x(), p.x() ) );
			vMax.setY( qMax( vMax.y(), p.y() ) );
			vMax.setZ( qMax( vMax.z(), p.z() ) );
			++nCorners;
		}
	}

	return QStringLiteral( "qt-gl 3.3 core; corners=%1 bbox=(%2,%3,%4)-(%5,%6,%7) target=(%8,%9,%10) dist=%11 meshes=%12/%13" )
		.arg( nCorners )
		.arg( vMin.x(), 0, 'f', 1 ).arg( vMin.y(), 0, 'f', 1 ).arg( vMin.z(), 0, 'f', 1 )
		.arg( vMax.x(), 0, 'f', 1 ).arg( vMax.y(), 0, 'f', 1 ).arg( vMax.z(), 0, 'f', 1 )
		.arg( m_vecTarget.x(), 0, 'f', 1 ).arg( m_vecTarget.y(), 0, 'f', 1 ).arg( m_vecTarget.z(), 0, 'f', 1 )
		.arg( m_flCamDistance, 0, 'f', 1 )
		.arg( m_lstTextures.count() ).arg( m_lstGpuMeshes.count() );
}
