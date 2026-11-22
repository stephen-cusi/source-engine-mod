#pragma once

#include "fgddata.h"
#include "mapmodel.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

// Keep the editor editable: maps may contain custom I/O not present in FGD.
class CEntityIoSuggestionDelegate : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	QWidget *createEditor( QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index ) const override
	{
		QComboBox *combo = new QComboBox( parent );
		combo->setEditable( true );
		combo->addItems( index.data( Qt::UserRole + 1 ).toStringList() );
		return combo;
	}
	void setEditorData( QWidget *editor, const QModelIndex &index ) const override
	{
		static_cast<QComboBox *>( editor )->setEditText( index.data( Qt::EditRole ).toString() );
	}
	void setModelData( QWidget *editor, QAbstractItemModel *model, const QModelIndex &index ) const override
	{
		model->setData( index, static_cast<QComboBox *>( editor )->currentText(), Qt::EditRole );
	}
};

// Stages the ordered VMF connections verbatim until the user accepts.
class CEntityOutputsDialog : public QDialog
{
public:
	using OutputList = QList<QPair<QString, QString> >;
	static QString T( const char *text ) { return QCoreApplication::translate( "CEntityOutputsDialog", text ); }
	explicit CEntityOutputsDialog( const OutputList &outputs, QWidget *parent = nullptr,
		const CFgdData *fgd = nullptr, const MapDocLite *map = nullptr, const QString &sourceClass = QString() ) : QDialog( parent )
	{
		if ( fgd )
		{
			m_outputNames = fgd->ClassOutputs( sourceClass );
			m_inputNames.insert( QStringLiteral( "!self" ), fgd->ClassInputs( sourceClass ) );
			m_targetNames << QStringLiteral( "!self" ) << QStringLiteral( "!activator" ) << QStringLiteral( "!caller" );
			if ( map )
				for ( const EntityLite &entity : map->entities )
				{
					QString name;
					for ( auto it = entity.keyvalues.constBegin(); it != entity.keyvalues.constEnd(); ++it )
						if ( it.key().compare( QLatin1String( "targetname" ), Qt::CaseInsensitive ) == 0 ) { name = it.value(); break; }
					if ( name.isEmpty() ) continue;
					QStringList &inputs = m_inputNames[name.toCaseFolded()];
					inputs.append( fgd->ClassInputs( entity.classname ) );
					inputs.removeDuplicates();
					inputs.sort( Qt::CaseInsensitive );
					m_targetNames.append( name );
				}
			m_targetNames.removeDuplicates();
			m_targetNames.sort( Qt::CaseInsensitive );
		}
		setWindowTitle( T( "Entity Outputs" ) );
		resize( 1080, 420 );
		QVBoxLayout *layout = new QVBoxLayout( this );
		QLabel *help = new QLabel( T( "Edit the fields or the raw connection. Ambiguous connections can only be edited in the raw column." ), this );
		help->setWordWrap( true );
		layout->addWidget( help );
		m_table = new QTableWidget( 0, 7, this );
		m_table->setObjectName( "entityOutputsTable" );
		m_table->setHorizontalHeaderLabels( QStringList() << T( "Output" ) << T( "Target" ) << T( "Input" ) << T( "Parameter" ) << T( "Delay" ) << T( "Times" ) << T( "Raw connection" ) );
		m_table->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
		m_table->horizontalHeader()->setSectionResizeMode( 6, QHeaderView::Stretch );
		m_table->setSelectionBehavior( QAbstractItemView::SelectRows );
		m_table->setSelectionMode( QAbstractItemView::ExtendedSelection );
		for ( int column = 0; column <= 2; ++column )
			m_table->setItemDelegateForColumn( column, new CEntityIoSuggestionDelegate( m_table ) );
		for ( const auto &output : outputs ) AddRow( output.first, output.second );
		connect( m_table, &QTableWidget::itemChanged, this, [this]( QTableWidgetItem *item ) { UpdateRow( item ); } );
		layout->addWidget( m_table );
		QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
		QPushButton *add = buttons->addButton( T( "Add" ), QDialogButtonBox::ActionRole );
		QPushButton *copy = buttons->addButton( T( "Copy" ), QDialogButtonBox::ActionRole );
		QPushButton *paste = buttons->addButton( T( "Paste" ), QDialogButtonBox::ActionRole );
		QPushButton *moveUp = buttons->addButton( T( "Move Up" ), QDialogButtonBox::ActionRole );
		QPushButton *moveDown = buttons->addButton( T( "Move Down" ), QDialogButtonBox::ActionRole );
		QPushButton *remove = buttons->addButton( T( "Remove" ), QDialogButtonBox::ActionRole );
		copy->setObjectName( "entityOutputsCopy" );
		paste->setObjectName( "entityOutputsPaste" );
		moveUp->setObjectName( "entityOutputsMoveUp" );
		moveDown->setObjectName( "entityOutputsMoveDown" );
		remove->setObjectName( "entityOutputsRemove" );
		connect( add, &QPushButton::clicked, this, [this] { AddRow( QStringLiteral( "OnTrigger" ), QStringLiteral( ",,,0,-1" ) ); m_table->setCurrentCell( m_table->rowCount() - 1, 0 ); } );
		connect( copy, &QPushButton::clicked, this, [this] {
			const QList<int> rows = SelectedRows();
			if ( rows.isEmpty() ) return;
			OutputList &buffer = CopyBuffer(); buffer.clear();
			for ( int row : rows ) buffer.append( qMakePair( m_table->item( row, 0 )->text(), m_table->item( row, 6 )->text() ) );
		} );
		connect( paste, &QPushButton::clicked, this, [this] {
			const OutputList copied = CopyBuffer();
			if ( copied.isEmpty() ) return;
			const int first = m_table->rowCount();
			for ( const auto &output : copied ) AddRow( output.first, output.second );
			m_table->clearSelection();
			for ( int row = first; row < m_table->rowCount(); ++row )
				m_table->selectionModel()->select( m_table->model()->index( row, 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
			m_table->setCurrentCell( first, 0, QItemSelectionModel::NoUpdate );
		} );
		connect( remove, &QPushButton::clicked, this, [this] {
			QList<int> rows = SelectedRows();
			for ( auto it = rows.crbegin(); it != rows.crend(); ++it ) m_table->removeRow( *it );
		} );
		connect( moveUp, &QPushButton::clicked, this, [this] { MoveSelectedRows( -1 ); } );
		connect( moveDown, &QPushButton::clicked, this, [this] { MoveSelectedRows( 1 ); } );
		connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
		connect( buttons, &QDialogButtonBox::accepted, this, [this] {
			for ( const auto &output : Outputs() )
				if ( output.first.trimmed().isEmpty() || output.first.contains( QLatin1Char( '\n' ) ) ||
					 output.first.contains( QLatin1Char( '\r' ) ) || output.second.contains( QLatin1Char( '\n' ) ) ||
					 output.second.contains( QLatin1Char( '\r' ) ) )
				{
					QMessageBox::warning( this, T( "Entity Outputs" ), T( "Each output needs a name and must not contain line breaks." ) );
					return;
				}
			accept();
		} );
		layout->addWidget( buttons );
	}
	OutputList Outputs() const
	{
		OutputList outputs;
		for ( int row = 0; row < m_table->rowCount(); ++row )
			outputs.append( qMakePair( m_table->item( row, 0 )->text(), m_table->item( row, 6 )->text() ) );
		return outputs;
	}
private:
	// Like Hammer's internal connection buffer, this outlives each properties
	// dialog but is separate from the map-object and system clipboards.
	static OutputList &CopyBuffer() { static OutputList buffer; return buffer; }
	QStringList InputsForTarget( const QString &target ) const
	{
		const QString key = target.toCaseFolded();
		if ( !key.contains( QLatin1Char( '*' ) ) || key.startsWith( QLatin1Char( '!' ) ) )
			return m_inputNames.value( key );
		// Hammer treats the part before the first '*' as a target-name prefix.
		// Suggestions are advisory: preserve the user-entered target and input.
		const QString prefix = key.left( key.indexOf( QLatin1Char( '*' ) ) );
		QStringList inputs;
		QSet<QString> seen;
		for ( auto it = m_inputNames.cbegin(); it != m_inputNames.cend(); ++it )
		{
			if ( it.key().startsWith( QLatin1Char( '!' ) ) || !it.key().startsWith( prefix ) ) continue;
			for ( const QString &input : it.value() )
				if ( !seen.contains( input.toCaseFolded() ) ) { seen.insert( input.toCaseFolded() ); inputs.append( input ); }
		}
		inputs.sort( Qt::CaseInsensitive );
		return inputs;
	}
	QList<int> SelectedRows() const
	{
		QList<int> rows;
		for ( const QModelIndex &index : m_table->selectionModel()->selectedRows() ) rows.append( index.row() );
		std::sort( rows.begin(), rows.end() );
		return rows;
	}
	void MoveSelectedRows( int direction )
	{
		QList<int> rows = SelectedRows();
		if ( rows.isEmpty() ) return;
		if ( direction > 0 ) std::reverse( rows.begin(), rows.end() );
		QSet<int> selected( rows.begin(), rows.end() );
		const QSignalBlocker blocker( m_table );
		for ( int row : rows )
		{
			const int target = row + direction;
			if ( target < 0 || target >= m_table->rowCount() || selected.contains( target ) ) continue;
			for ( int col = 0; col < m_table->columnCount(); ++col )
			{
				QTableWidgetItem *source = m_table->takeItem( row, col );
				QTableWidgetItem *other = m_table->takeItem( target, col );
				m_table->setItem( target, col, source );
				m_table->setItem( row, col, other );
			}
			selected.remove( row ); selected.insert( target );
		}
		m_table->clearSelection();
		for ( int row : selected )
			m_table->selectionModel()->select( m_table->model()->index( row, 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
	}
	void AddRow( const QString &name, const QString &value )
	{
		const QSignalBlocker blocker( m_table );
		const int row = m_table->rowCount();
		m_table->insertRow( row );
		m_table->setItem( row, 0, new QTableWidgetItem( name ) );
		m_table->item( row, 0 )->setData( Qt::UserRole + 1, m_outputNames );
		for ( int col = 1; col <= 6; ++col ) m_table->setItem( row, col, new QTableWidgetItem );
		m_table->item( row, 1 )->setData( Qt::UserRole + 1, m_targetNames );
		m_table->item( row, 6 )->setText( value );
		ParseRow( row );
	}

	// Unrecognized connections stay editable verbatim rather than being truncated.
	void ParseRow( int row )
	{
		const QSignalBlocker blocker( m_table );
		const QString raw = m_table->item( row, 6 )->text();
		const QChar delimiter = raw.contains( QChar( 27 ) ) ? QChar( 27 ) : QLatin1Char( ',' );
		const QStringList parts = raw.split( delimiter );
		const bool valid = parts.size() == 5;
		for ( int col = 1; col <= 5; ++col )
		{
			QTableWidgetItem *field = m_table->item( row, col );
			field->setText( valid ? parts[col - 1] : QString() );
			field->setFlags( valid ? field->flags() | Qt::ItemIsEditable : field->flags() & ~Qt::ItemIsEditable );
		}
		m_table->item( row, 2 )->setData( Qt::UserRole + 1,
			valid ? InputsForTarget( parts[0] ) : QStringList() );
		m_table->item( row, 6 )->setToolTip( valid ? QString() : T( "Edit this connection as raw text to preserve its data." ) );
	}
	void UpdateRow( QTableWidgetItem *item )
	{
		const int row = item->row(), col = item->column();
		if ( col == 6 ) { ParseRow( row ); return; }
		if ( col < 1 || col > 5 ) return;
		QStringList fields;
		bool hasComma = false, hasEscape = false;
		for ( int i = 1; i <= 5; ++i )
		{
			const QString value = m_table->item( row, i )->text();
			fields << value;
			hasComma |= value.contains( QLatin1Char( ',' ) );
			hasEscape |= value.contains( QChar( 27 ) );
		}
		const QString oldRaw = m_table->item( row, 6 )->text();
		const bool usesEscape = oldRaw.contains( QChar( 27 ) );
		// The bundled HL2 server parses comma-separated outputs only. Never
		// automatically change a comma connection to an ESC connection.
		if ( hasEscape || ( hasComma && !usesEscape ) )
		{
			ParseRow( row );
			QMessageBox::warning( this, T( "Entity Outputs" ),
				T( "A field cannot contain its connection separator. Use the raw connection column to edit unsupported formats." ) );
			return;
		}
		const QChar delimiter = usesEscape ? QChar( 27 ) : QLatin1Char( ',' );
		const QSignalBlocker blocker( m_table );
		m_table->item( row, 6 )->setText( fields.join( delimiter ) );
		if ( col == 1 ) m_table->item( row, 2 )->setData( Qt::UserRole + 1, InputsForTarget( fields[0] ) );
	}
	QTableWidget *m_table;
	QStringList m_outputNames, m_targetNames;
	QMap<QString, QStringList> m_inputNames;
};
