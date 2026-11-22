#pragma once

#include "mapmodel.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>
#include "mathlib/vmatrix.h"
#include <cmath>

// Edits a private copy until the user accepts; plane and editor data are untouched.
class CFacePropertiesDialog : public QDialog
{
public:
	explicit CFacePropertiesDialog( const QList<SideLite> &faces, QWidget *parent = nullptr, const QString &currentMaterial = QString(), const QStringList &faceLabels = QStringList() )
		: QDialog( parent ), m_faces( faces )
	{
		setWindowTitle( T( "Face Properties" ) );
		resize( 1000, 360 );
		QVBoxLayout *layout = new QVBoxLayout( this );
		m_table = new QTableWidget( faces.size(), 8, this );
		m_table->setObjectName( "facePropertiesTable" );
		m_table->setSelectionBehavior( QAbstractItemView::SelectRows );
		m_table->setSelectionMode( QAbstractItemView::ExtendedSelection );
		m_table->setHorizontalHeaderLabels( { T( "Face" ), T( "Material" ), T( "U axis and offset" ), T( "U scale" ),
			T( "V axis and offset" ), T( "V scale" ), T( "Stored rotation" ), T( "Lightmap scale" ) } );
		m_table->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
		m_table->horizontalHeader()->setStretchLastSection( true );
		for ( int row = 0; row < faces.size(); ++row )
		{
			const SideLite &side = faces[row];
			m_rotationBaselines.append( side.rotation );
			auto number = []( float value ) { return QString::number( value, 'g', 9 ); };
			auto axisText = [&]( const float *axis ) { return number( axis[0] ) + ' ' + number( axis[1] ) + ' ' + number( axis[2] ) + ' ' + number( axis[3] ); };
			const QStringList values = { row < faceLabels.size() ? faceLabels[row] : QString::number( row + 1 ), side.texture, axisText( side.uaxis ), number( side.uscale ),
				axisText( side.vaxis ), number( side.vscale ), number( side.rotation ), QString::number( side.lightmapscale ) };
			for ( int column = 0; column < values.size(); ++column )
			{
				QTableWidgetItem *item = new QTableWidgetItem( values[column] );
				if ( column == 0 ) item->setFlags( item->flags() & ~Qt::ItemIsEditable );
				m_table->setItem( row, column, item );
			}
		}
		layout->addWidget( m_table );
		QHBoxLayout *selection = new QHBoxLayout;
		QPushButton *selectAll = new QPushButton( T( "Select All Faces" ), this );
		QPushButton *invertSelection = new QPushButton( T( "Invert Face Selection" ), this );
		QPushButton *clearSelection = new QPushButton( T( "Clear Face Selection" ), this );
		selectAll->setObjectName( "faceSelectAll" );
		invertSelection->setObjectName( "faceInvertSelection" );
		clearSelection->setObjectName( "faceClearSelection" );
		selection->addWidget( selectAll ); selection->addWidget( invertSelection ); selection->addWidget( clearSelection ); selection->addStretch();
		layout->addLayout( selection );
		connect( selectAll, &QPushButton::clicked, m_table, &QTableWidget::selectAll );
		connect( clearSelection, &QPushButton::clicked, m_table, &QTableWidget::clearSelection );
		connect( invertSelection, &QPushButton::clicked, this, [this] {
			for ( int row = 0; row < m_table->rowCount(); ++row )
				m_table->selectionModel()->select( m_table->model()->index( row, 0 ), QItemSelectionModel::Toggle | QItemSelectionModel::Rows );
		} );
		QHBoxLayout *materialLayout = new QHBoxLayout;
		QLabel *materialLabel = new QLabel( T( "Material for selected faces:" ), this );
		QLineEdit *material = new QLineEdit( currentMaterial, this );
		material->setObjectName( "faceBatchMaterial" );
		materialLabel->setBuddy( material );
		QPushButton *applyMaterial = new QPushButton( T( "Apply to Selected Faces" ), this );
		applyMaterial->setObjectName( "faceBatchApply" );
		materialLayout->addWidget( materialLabel ); materialLayout->addWidget( material, 1 ); materialLayout->addWidget( applyMaterial );
		layout->addLayout( materialLayout );
		const auto updateMaterialButton = [this, material, applyMaterial] {
			applyMaterial->setEnabled( !material->text().trimmed().isEmpty() && !m_table->selectionModel()->selectedRows().isEmpty() );
		};
		connect( material, &QLineEdit::textChanged, this, [updateMaterialButton] { updateMaterialButton(); } );
		connect( m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [updateMaterialButton] { updateMaterialButton(); } );
		connect( applyMaterial, &QPushButton::clicked, this, [this, material] { ApplyMaterialToSelected( material->text() ); } );
		updateMaterialButton();
		QHBoxLayout *alignment = new QHBoxLayout;
		QPushButton *world = new QPushButton( T( "Align to World" ), this );
		QPushButton *face = new QPushButton( T( "Align to Face" ), this );
		world->setObjectName( "faceAlignWorld" );
		face->setObjectName( "faceAlignFace" );
		alignment->addWidget( world ); alignment->addWidget( face ); alignment->addStretch();
		layout->addLayout( alignment );
		auto applyAlignment = [this]( bool faceAligned ) {
			if ( !AlignSelected( faceAligned ) )
				QMessageBox::warning( this, T( "Face Properties" ), T( "Cannot align the selected faces. Check their geometry, rotation and texture offsets." ) );
		};
		connect( world, &QPushButton::clicked, this, [applyAlignment] { applyAlignment( false ); } );
		connect( face, &QPushButton::clicked, this, [applyAlignment] { applyAlignment( true ); } );
		QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
		connect( buttons, &QDialogButtonBox::accepted, this, [this] {
			QList<SideLite> parsed;
			if ( !ReadFaces( parsed ) ) { QMessageBox::warning( this, T( "Face Properties" ), T( "Enter a material and valid finite texture axes, nonzero scales and positive lightmap scale." ) ); return; }
			m_faces = parsed; accept();
		} );
		connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
		layout->addWidget( buttons );
	}
	QList<SideLite> Faces() const { return m_faces; }
	void ApplyMaterialToSelected( const QString &material )
	{
		const QString name = material.trimmed();
		if ( name.isEmpty() ) return;
		for ( const QModelIndex &index : m_table->selectionModel()->selectedRows() ) m_table->item( index.row(), 1 )->setText( name );
	}
	bool ReadFaces( QList<SideLite> &result ) const
	{
		QList<SideLite> parsed = m_faces;
		for ( int row = 0; row < parsed.size(); ++row )
		{
			SideLite &face = parsed[row];
			face.texture = m_table->item( row, 1 )->text().trimmed();
			auto parseFloat = []( const QString &text, float &value ) { bool ok = false; value = text.toFloat( &ok ); return ok && std::isfinite( value ); };
			auto parseAxis = [&]( int column, float *axis ) {
				const QStringList parts = m_table->item( row, column )->text().trimmed().split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
				if ( parts.size() != 4 ) return false;
				for ( int i = 0; i < 4; ++i ) if ( !parseFloat( parts[i], axis[i] ) ) return false;
				return axis[0] != 0 || axis[1] != 0 || axis[2] != 0;
			};
			bool lightOk = false;
			face.lightmapscale = m_table->item( row, 7 )->text().toInt( &lightOk );
			if ( face.texture.isEmpty() || !parseAxis( 2, face.uaxis ) || !parseFloat( m_table->item( row, 3 )->text(), face.uscale ) || face.uscale == 0 ||
				 !parseAxis( 4, face.vaxis ) || !parseFloat( m_table->item( row, 5 )->text(), face.vscale ) || face.vscale == 0 ||
				 !parseFloat( m_table->item( row, 6 )->text(), face.rotation ) || !lightOk || face.lightmapscale < 1 ) return false;
			if ( face.rotation != m_rotationBaselines[row] )
			{
				// Original CMapFace::RotateTextureAxes rotates around V cross U.
				// MatrixBuildRotationAboutAxis is the shared mathlib implementation
				// of Hammer's AxisAngleMatrix. Normalize for non-unit VMF axes.
				const double x = double(face.vaxis[1]) * face.uaxis[2] - double(face.vaxis[2]) * face.uaxis[1];
				const double y = double(face.vaxis[2]) * face.uaxis[0] - double(face.vaxis[0]) * face.uaxis[2];
				const double z = double(face.vaxis[0]) * face.uaxis[1] - double(face.vaxis[1]) * face.uaxis[0];
				const double length = std::hypot( std::hypot( x, y ), z );
				if ( !std::isfinite( length ) || length < 1e-12 ) return false;
				const Vector axis( float( x / length ), float( y / length ), float( z / length ) );
				const float degrees = float( std::remainder( double(face.rotation) - double(m_rotationBaselines[row]), 360.0 ) );
				matrix3x4_t matrix;
				MatrixBuildRotationAboutAxis( axis, degrees, matrix );
				for ( float *textureAxis : { face.uaxis, face.vaxis } )
				{
					float rotated[3];
					for ( int i = 0; i < 3; ++i )
					{
						const double v = double(matrix[i][0]) * textureAxis[0] + double(matrix[i][1]) * textureAxis[1] + double(matrix[i][2]) * textureAxis[2];
						rotated[i] = float( v );
						if ( !std::isfinite( v ) || !std::isfinite( rotated[i] ) ) return false;
					}
					for ( int i = 0; i < 3; ++i ) textureAxis[i] = rotated[i];
				}
			}
		}
		result = parsed;
		return true;
	}
	bool AlignSelected( bool faceAligned )
	{
		const QModelIndexList rows = m_table->selectionModel()->selectedRows();
		if ( rows.isEmpty() ) return true;
		struct Change { int row; QString u; QString v; float rotation; };
		QList<Change> changes;
		for ( const QModelIndex &index : rows )
		{
			const int row = index.row();
			const SideLite &side = m_faces[row];
			const auto parseOffset = [&]( int column, float &offset ) {
				const QStringList parts = m_table->item( row, column )->text().trimmed().split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
				bool ok = false;
				if ( parts.size() != 4 ) return false;
				offset = parts[3].toFloat( &ok );
				return ok && std::isfinite( offset );
			};
			float uOffset, vOffset;
			bool rotationOk = false;
			const float rotation = m_table->item( row, 6 )->text().toFloat( &rotationOk );
			if ( !parseOffset( 2, uOffset ) || !parseOffset( 4, vOffset ) || !rotationOk || !std::isfinite( rotation ) ) return false;
			// Same normal convention and six-direction tie breaking as Hammer's
			// CMapFace::GetOrientation and InitializeTextureAxes.
			double a[3], b[3], n[3];
			for ( int i = 0; i < 3; ++i ) { a[i] = side.planePts[1][i] - side.planePts[0][i]; b[i] = side.planePts[2][i] - side.planePts[0][i]; }
			n[0] = b[1]*a[2]-b[2]*a[1]; n[1] = b[2]*a[0]-b[0]*a[2]; n[2] = b[0]*a[1]-b[1]*a[0];
			const double length = std::hypot( std::hypot( n[0], n[1] ), n[2] );
			if ( !std::isfinite( length ) || length < 1e-12 ) return false;
			for ( double &v : n ) v /= length;
			const double normals[6][3] = { {0,0,1}, {0,0,-1}, {0,-1,0}, {0,1,0}, {-1,0,0}, {1,0,0} };
			const double down[6][3] = { {0,-1,0}, {0,-1,0}, {0,0,-1}, {0,0,-1}, {0,0,-1}, {0,0,-1} };
			const double right[6][3] = { {1,0,0}, {1,0,0}, {1,0,0}, {1,0,0}, {0,1,0}, {0,1,0} };
			int orientation = 0; double best = 0;
			for ( int i = 0; i < 6; ++i ) { const double dot = n[0]*normals[i][0] + n[1]*normals[i][1] + n[2]*normals[i][2]; if ( dot >= best ) { best = dot; orientation = i; } }
			Vector u{ float(right[orientation][0]), float(right[orientation][1]), float(right[orientation][2]) };
			Vector v{ float(down[orientation][0]), float(down[orientation][1]), float(down[orientation][2]) };
			if ( faceAligned )
			{
				const Vector normal{ float(n[0]), float(n[1]), float(n[2]) };
				CrossProduct( normal, v, u ); if ( VectorNormalize( u ) < 1e-6f ) return false;
				CrossProduct( u, normal, v ); if ( VectorNormalize( v ) < 1e-6f ) return false;
			}
			if ( rotation != 0 )
			{
				Vector axis; CrossProduct( v, u, axis ); if ( VectorNormalize( axis ) < 1e-6f ) return false;
				matrix3x4_t matrix; MatrixBuildRotationAboutAxis( axis, float(std::remainder( double(rotation), 360.0 )), matrix );
				Vector rotatedU, rotatedV; VectorRotate( u, matrix, rotatedU ); VectorRotate( v, matrix, rotatedV ); u = rotatedU; v = rotatedV;
			}
			auto text = []( const Vector &axis, float offset ) { return QStringLiteral( "%1 %2 %3 %4" ).arg( axis.x, 0, 'g', 9 ).arg( axis.y, 0, 'g', 9 ).arg( axis.z, 0, 'g', 9 ).arg( offset, 0, 'g', 9 ); };
			changes.append( { row, text( u, uOffset ), text( v, vOffset ), rotation } );
		}
		for ( const Change &change : changes )
		{
			m_table->item( change.row, 2 )->setText( change.u );
			m_table->item( change.row, 4 )->setText( change.v );
			m_rotationBaselines[change.row] = change.rotation;
		}
		return true;
	}
private:
	static QString T( const char *text ) { return QCoreApplication::translate( "CFacePropertiesDialog", text ); }
	QTableWidget *m_table;
	QList<SideLite> m_faces;
	QList<float> m_rotationBaselines;
};
