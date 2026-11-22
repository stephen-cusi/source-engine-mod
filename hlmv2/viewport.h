//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: Central 3D viewport (QOpenGLWidget).
//
//          Render route B2 ("own Qt OpenGL"): the engine materialsystem/
//          shaderapidx9 stack is deliberately NOT attached here (that route
//          needs the engine's D3D9 module plus a filesystem bootstrap and is
//          Windows-only; see the design report). Instead the parsed studio
//          geometry is rendered directly with a small GL 3.3 core renderer:
//
//            - CPU-skinned triangles (bind pose or animated bone matrices),
//              two-sided diffuse lighting with a rim term
//            - material textures resolved from loose files / VPK via
//              CMaterialLoader (VMT -> VTF), decoded without any engine module
//            - a deterministic palette color per material slot when a texture
//              is missing or unsupported
//            - optional wireframe overlay / wireframe-only mode
//            - bone skeleton lines and joint dots
//            - attachment crosses in world space
//            - ground grid (Source convention: Z is up)
//            - orbit camera: left-drag rotate, wheel zoom, right-drag pan
//
//          Everything above is Qt + OpenGL 3.3 core only: no materialsystem,
//          no shaderapidx9, no filesystem bootstrap, so the same code builds
//          on win32, linux and darwin.
//
//=============================================================================

#ifndef HLMV2_VIEWPORT_H
#define HLMV2_VIEWPORT_H

#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>
#include <QWidget>

class CStudioModelData;

//-----------------------------------------------------------------------------
// Purpose: Model inspection viewport with orbit camera and studio renderer.
//-----------------------------------------------------------------------------
class CModelViewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
	Q_OBJECT

public:
	explicit CModelViewport( QWidget *parent = nullptr );
	~CModelViewport() override;

	// Attaches (or clears) the parsed model; keeps camera framing sensible.
	void SetModel( CStudioModelData *pModel );

	// Rebuilds the GPU buffers after in-place model state changes (bodygroup
	// variants, skin family) without moving the camera.
	void RefreshModel();

	// Attaches decoded material images, indexed by the studio texture slot.
	// Safe to call before the GL context exists; upload is deferred to paint.
	void SetMaterialImages( const QVector<QImage> &images );

	// Attaches/clears a "hands" model. Its meshes are drawn together with the
	// main model, skinned with the main model's animated bones (matched by
	// name) - the standalone equivalent of the original viewer's merged
	// models / "Load Weapon".
	void SetHandsModel( CStudioModelData *pHands );
	void SetHandsMaterialImages( const QVector<QImage> &images );
	bool HasHands() const { return m_pHandsModel != nullptr; }

	// Camera presets, mirroring the original viewer's viewmodel/orthogonal-ish
	// viewing aids while keeping the orbit camera.
	enum CameraPreset_t
	{
		CamPerspective = 0,
		CamFront,
		CamBack,
		CamLeft,
		CamRight,
		CamTop,
		CamBottom,
		CamViewmodel
	};
	void SetCameraPreset( int nPreset );

	// Centers and distances the camera on the current model bounds.
	void FrameModel();
	// Restores the default orbit angles and re-frames.
	void ResetCamera();

	// Display modes.
	void SetWireframe( bool bWireframe ) { m_bWireframe = bWireframe; update(); }
	void SetShowBones( bool bShow ) { m_bShowBones = bShow; update(); }
	void SetShowAttachments( bool bShow ) { m_bShowAttachments = bShow; update(); }
	void SetShowGround( bool bShow ) { m_bShowGround = bShow; update(); }
	void SetTexturesEnabled( bool bEnabled ) { m_bTexturesEnabled = bEnabled; update(); }

	// Marks which texture slots are engine-coloured ("$color" or a proxy
	// writing it) and sets the tint applied to them. This is how HL2/GMod
	// colour player arms and weapons.
	void SetTintableSlots( const QVector<bool> &tintableSlots );
	void SetHandsTintableSlots( const QVector<bool> &tintableSlots );
	void SetTintColor( const QVector3D &color ) { m_vecTint = color; update(); }

	// Self-test helper: drives a full render through grabFramebuffer() (works
	// headless, without ever exposing the widget) and reports whether the
	// pipeline produced a frame.
	bool RenderOneFrameForSelfTest();

	// Grabs the rendered frame (used by File > Export Screenshot and by
	// --selftest). Works without an exposed widget via a transient FBO.
	QImage GrabFrame();

	// Diagnostics: skinned vertex bounds, camera target/distance and mesh
	// count. Used by --selftest to catch framing vs geometry mismatches.
	QString GetRenderDiagnostics() const;

	// Camera access for the status bar readout.
	float GetCameraDistance() const { return m_flCamDistance; }
	float GetCameraYaw() const { return m_flCamYaw; }
	float GetCameraPitch() const { return m_flCamPitch; }

protected:
	void initializeGL() override;
	void resizeGL( int w, int h ) override;
	void paintGL() override;

	void mousePressEvent( QMouseEvent *event ) override;
	void mouseReleaseEvent( QMouseEvent *event ) override;
	void mouseMoveEvent( QMouseEvent *event ) override;
	void wheelEvent( QWheelEvent *event ) override;

private:
	// GL object setup (called once from initializeGL).
	bool BuildPrograms( QString &sError );
	void BuildTextures( int nBatch );

	// Rebuilds the mesh GPU buffers after model/bodygroup/skin changes.
	void RebuildMeshBuffers();

	// Recomputes the CPU-skinned vertex cache for the current bone matrices.
	void UpdateSkinnedVertices();

	// Draw helpers.
	void DrawGrid();
	void DrawModel();
	void DrawLines( const QVector<float> &verts, const QVector3D &color );
	void DrawBones();
	void DrawAttachments();

private:
	CStudioModelData *m_pModel = nullptr;
	CStudioModelData *m_pHandsModel = nullptr;
	QVector<QMatrix4x4> m_HandsSkinning;

	// Camera (orbit around target; Source space: Z up).
	QVector3D m_vecTarget{ 0.0f, 0.0f, 36.0f };
	float m_flCamDistance = 128.0f;
	float m_flCamYaw = 45.0f;
	float m_flCamPitch = 20.0f;
	bool m_bFramedModel = false;

	// Mouse navigation state.
	QPoint m_ptLastMouse;
	bool m_bRotating = false;
	bool m_bPanning = false;

	// Display flags. Debug overlays are off by default so a freshly opened
	// model reads as the actual asset; they remain available from View.
	bool m_bWireframe = false;
	bool m_bShowBones = false;
	bool m_bShowAttachments = false;
	bool m_bShowGround = true;
	bool m_bTexturesEnabled = true;

	// Engine-driven colour ($color / material proxy) support.
	QVector<bool> m_lstTintable;
	QVector<bool> m_lstHandsTintable;
	QVector3D m_vecTint{ 1.0f, 1.0f, 1.0f };

	// Mesh shader.
	QOpenGLShaderProgram m_MeshProgram;
	int m_nMeshMvpLoc = -1;
	int m_nMeshColorLoc = -1;
	int m_nMeshLightLoc = -1;
	int m_nMeshLight2Loc = -1;
	int m_nMeshEyeLoc = -1;
	int m_nMeshTexLoc = -1;
	int m_nMeshUseTexLoc = -1;
	int m_nMeshTintLoc = -1;
	int m_nMeshUseTintLoc = -1;

	// Line shader (grid, bones, attachments, wireframe).
	QOpenGLShaderProgram m_LineProgram;
	int m_nLineMvpLoc = -1;
	int m_nLineColorLoc = -1;

	// Per-mesh GPU state; rebuilt on model/bodygroup change. Heap-allocated
	// because QOpenGLBuffer/VAO are not copyable (QVector needs copies).
	struct GpuMesh_t
	{
		QOpenGLVertexArrayObject vao;
		QOpenGLBuffer vbo{ QOpenGLBuffer::VertexBuffer };
		int nVertexCount = 0;      // corners in the expanded, skinned stream
		int nTextureIndex = -1;    // studio texture slot for this mesh
		int nBatch = 0;            // 0 = main model, 1 = attached hands
		int nDrawIndex = 0;        // index inside that batch's draw list
		bool bTintable = false;    // material colour is engine driven
		QVector3D vecColor;
		bool bBuilt = false;
	};
	QVector<GpuMesh_t *> m_lstGpuMeshes;

	// Decoded VTF images (one per studio texture slot) and their GL objects.
	// Indexed by texture slot; a null texture means "use the palette color".
	QVector<QImage> m_lstMaterialImages;
	QVector<QOpenGLTexture *> m_lstTextures;
	bool m_bTexturesDirty = false;

	// Separate texture set for the attached hands model.
	QVector<QImage> m_lstHandsImages;
	QVector<QOpenGLTexture *> m_lstHandsTextures;
	bool m_bHandsTexturesDirty = false;

	// CPU-skinned vertex cache (world space) per draw mesh, in draw order.
	struct SkinnedMesh_t
	{
		QVector<float> positions;   // xyz per corner (world space)
		QVector<float> normals;     // xyz per corner
		QVector<float> uvs;         // uv per corner
		bool bValid = false;
	};
	QVector<SkinnedMesh_t> m_lstSkinned;

	// Static grid line data.
	QOpenGLVertexArrayObject m_GridVao;
	QOpenGLBuffer m_GridVbo{ QOpenGLBuffer::VertexBuffer };
	int m_nGridVertexCount = 0;

	// Dynamic line buffer shared by bones/attachments (updated per frame).
	QOpenGLVertexArrayObject m_LineVao;
	QOpenGLBuffer m_LineVbo{ QOpenGLBuffer::VertexBuffer };

	bool m_bGlReady = false;
	QString m_sGlError;

	// Set when mesh buffers were requested before the GL context existed; the
	// next paintGL builds them (QOpenGLWidget only creates its context on the
	// first expose, so SetModel() before show() must defer the upload).
	bool m_bMeshDirty = false;
};

#endif // HLMV2_VIEWPORT_H
