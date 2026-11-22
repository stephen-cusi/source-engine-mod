#pragma once

#include "mapmodel.h"

#include <QHash>
#include <QMap>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <QMatrix4x4>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <cmath>

class QOpenGLWidget;

inline bool HammerPreviewValidUvScale( const SideLite &side )
{
	return std::isfinite( side.uscale ) && std::isfinite( side.vscale ) &&
		side.uscale != 0.0f && side.vscale != 0.0f;
}

class CMaterialPreviewRenderer : protected QOpenGLFunctions
{
public:
	CMaterialPreviewRenderer();
	~CMaterialPreviewRenderer();

	void SetGameDirectory( const QString &gameDir );
	QStringList SearchPaths() const { return m_searchPaths; }
	void SetSearchPaths( const QStringList &paths ) { m_searchPaths = paths; m_textureCacheDirty = true; }
	bool Initialize();
	void Release();
	void Render( const MapDocLite *map, const QSet<int> &selectedIds,
		const double eye[3], const double forward[3], const double up[3],
		int width, int height, bool textured, const QMap<int, QSet<int>> &selectedFaces = {} );
	bool ProbeMaterial( const QString &materialName, QString *detail = nullptr );
	// Decode the base texture without an OpenGL context, for browser previews.
	bool LoadMaterialImage( const QString &materialName, QImage &image );
	bool ResolveBaseTexture( const QString &materialName, QString &textureName, QString *detail = nullptr ) const;
	int CachedTextureCount() const { return m_textures.size(); }
	bool HasLoadedTexture( const QString &name ) const
	{
		const auto it = m_textures.constFind( name.toLower() );
		return it != m_textures.cend() && !it->missing && it->id != 0;
	}

private:
	struct Vertex { QVector3D position; QVector2D uv; };
	struct MaterialProjection { double decalScale = 1.0; bool modulate = false; };
	struct TextureEntry {
		GLuint id = 0;
		int width = 1, height = 1;
		bool missing = false;
		MaterialProjection projection;
	};

	bool EnsureProgram();
	bool ResolveBaseTexture( const QString &materialName, QString &textureName, QString *detail, MaterialProjection *projection ) const;
	bool LoadMaterialImage( const QString &materialName, QImage &image, MaterialProjection *projection );
	GLuint TextureForMaterial( const QString &materialName );
	void DrawSolid( const SolidLite &solid, const QMatrix4x4 &mvp, bool textured, bool selected, const QSet<int> &selectedFaces );
	void DrawProjectedMaterials( const MapDocLite &map );
	void DrawOverlays( const MapDocLite &map );
	void DrawProjectedTriangles( const QVector<Vertex> &vertices, const TextureEntry &texture, bool clampUv );

	QOpenGLShaderProgram m_program;
	QHash<QString, TextureEntry> m_textures;
	QStringList m_searchPaths;
	QString m_gameDirectory;
	bool m_initialized = false;
	bool m_textureCacheDirty = false;
};
