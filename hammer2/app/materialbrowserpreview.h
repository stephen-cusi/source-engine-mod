#pragma once

#include "materialpreviewrenderer.h"

#include <QCoreApplication>
#include <QLabel>
#include <QPixmap>
#include <QThread>
#include <QTimer>
#include <memory>

// The asset index can contain thousands of materials. Decode only the latest
// selected item on a single worker, rather than blocking the UI on every click.
class CMaterialBrowserPreview : public QObject
{
public:
	CMaterialBrowserPreview( QLabel *image, QLabel *status, QObject *parent )
		: QObject( parent ), m_image( image ), m_status( status )
	{
		m_timer.setSingleShot( true );
		m_timer.setInterval( 150 );
		connect( &m_timer, &QTimer::timeout, this, [this] { Start(); } );
	}

	void Request( const QString &gameDir, const QString &material )
	{
		if ( gameDir != m_gameDir )
		{
			m_paths.clear();
			if ( !gameDir.isEmpty() )
			{
				// Build mount paths on the UI thread; the worker only decodes assets.
				CMaterialPreviewRenderer resolver;
				resolver.SetGameDirectory( gameDir );
				m_paths = resolver.SearchPaths();
			}
		}
		m_gameDir = gameDir;
		m_material = material;
		++m_generation;
		m_image->clear();
		if ( gameDir.isEmpty() || material.isEmpty() )
		{
			m_status->setText( T( "Select a scanned material to preview it." ) );
			return;
		}
		m_status->setText( T( "Loading material preview..." ) );
		m_timer.start();
	}

private:
	static QString T( const char *s ) { return QCoreApplication::translate( "CMainWindow", s ); }
	void Start()
	{
		if ( m_busy || m_gameDir.isEmpty() || m_material.isEmpty() ) return;
		m_busy = true;
		const quint64 generation = m_generation;
		const QStringList paths = m_paths;
		const QString material = m_material;
		const auto image = std::make_shared<QImage>();
		QThread *worker = QThread::create( [paths, material, image] {
			CMaterialPreviewRenderer renderer;
			renderer.SetSearchPaths( paths );
			// The renderer returns pixels flipped for OpenGL texture coordinates;
			// a Qt image uses top-down coordinates and needs the inverse flip.
			if ( renderer.LoadMaterialImage( material, *image ) ) *image = image->mirrored();
		} );
		connect( worker, &QThread::finished, this, [this, generation, image] {
			m_busy = false;
			if ( generation == m_generation )
			{
				if ( image->isNull() ) m_status->setText( T( "Preview unavailable for this material." ) );
				else
				{
					m_image->setPixmap( QPixmap::fromImage( image->scaled( m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation ) ) );
					m_status->setText( T( "Base texture: %1 x %2" ).arg( image->width() ).arg( image->height() ) );
				}
			}
			else if ( !m_gameDir.isEmpty() && !m_material.isEmpty() ) m_timer.start();
		} );
		connect( worker, &QThread::finished, worker, &QObject::deleteLater );
		worker->start();
	}
	QLabel *m_image;
	QLabel *m_status;
	QTimer m_timer;
	QString m_gameDir, m_material;
	QStringList m_paths;
	quint64 m_generation = 0;
	bool m_busy = false;
};
