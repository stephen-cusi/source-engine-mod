// Worldspawn properties editor.
#include "mappropertiesdialog.h"
#include "fgddata.h"
#include "propertyvaluedelegate.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
	bool IsStructural( const QString &key )
	{
		return key.compare( QLatin1String( "classname" ), Qt::CaseInsensitive ) == 0 ||
			key.compare( QLatin1String( "id" ), Qt::CaseInsensitive ) == 0 ||
			key.compare( QLatin1String( "mapversion" ), Qt::CaseInsensitive ) == 0;
	}
}

CMapPropertiesDialog::CMapPropertiesDialog( const QMap<QString, QString> &properties,
	const QMap<QString, QString> &fgdProperties, QWidget *parent, const CFgdData *fgd )
	: QDialog( parent ), m_pTable( new QTableWidget( this ) ),
	  m_pFilter( new QLineEdit( this ) ), m_pRawKeys( new QCheckBox( tr( "Show raw keys" ), this ) )
{
	setWindowTitle( tr( "Map Properties" ) );
	resize( 760, 520 );
	QVBoxLayout *layout = new QVBoxLayout( this );
	QHBoxLayout *search = new QHBoxLayout();
	m_pFilter->setObjectName( QStringLiteral( "worldPropertyFilter" ) );
	m_pFilter->setPlaceholderText( tr( "Filter properties" ) );
	m_pRawKeys->setObjectName( QStringLiteral( "worldPropertyRawKeys" ) );
	search->addWidget( m_pFilter );
	search->addWidget( m_pRawKeys );
	layout->addLayout( search );
	m_pTable->setObjectName( QStringLiteral( "worldProperties" ) );
	m_pTable->setColumnCount( 2 );
	m_pTable->setItemDelegateForColumn( 1, new CPropertyValueDelegate( m_pTable ) );
	m_pTable->setHorizontalHeaderLabels( QStringList() << tr( "Property" ) << tr( "Value" ) );
	m_pTable->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
	m_pTable->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );
	m_pTable->verticalHeader()->hide();
	m_pTable->setSelectionBehavior( QAbstractItemView::SelectRows );
	m_pTable->setSelectionMode( QAbstractItemView::SingleSelection );
	layout->addWidget( m_pTable );

	QMap<QString, QString> names = fgdProperties;
	if ( names.isEmpty() ) names.insert( QStringLiteral( "skyname" ), QStringLiteral( "skyname" ) );
	for ( auto it = properties.constBegin(); it != properties.constEnd(); ++it )
	{
		bool found = false;
		for ( auto schema = names.constBegin(); schema != names.constEnd(); ++schema )
			if ( schema.key().compare( it.key(), Qt::CaseInsensitive ) == 0 ) { found = true; break; }
		if ( !found ) names.insert( it.key(), it.key() );
	}
	for ( auto it = names.constBegin(); it != names.constEnd(); ++it )
	{
		QString key = it.key();
		bool present = false;
		for ( auto existing = properties.constBegin(); existing != properties.constEnd(); ++existing )
			if ( existing.key().compare( key, Qt::CaseInsensitive ) == 0 ) { key = existing.key(); present = true; break; }
		const int row = m_pTable->rowCount();
		m_pTable->insertRow( row );
		QTableWidgetItem *name = new QTableWidgetItem( it.value().isEmpty() ? key : it.value() );
		name->setData( Qt::UserRole, key );
		name->setData( Qt::UserRole + 1, it.value().isEmpty() ? key : it.value() );
		name->setFlags( name->flags() & ~Qt::ItemIsEditable );
		name->setToolTip( it.value() );
		m_pTable->setItem( row, 0, name );
		QTableWidgetItem *value = new QTableWidgetItem( present ? properties.value( key ) : QString() );
		value->setData( Qt::UserRole, present );
		if ( fgd )
		{
			QVariantList choices;
			for ( const auto &choice : fgd->PropertyChoices( QStringLiteral( "worldspawn" ), key ) )
				choices.append( QStringList() << choice.first << choice.second );
			if ( !choices.isEmpty() ) value->setData( Qt::UserRole + 1, choices );
			QVariantList flags;
			for ( const auto &flag : fgd->PropertyFlags( QStringLiteral( "worldspawn" ), key ) )
				flags.append( QStringList() << flag.first << flag.second );
			if ( !flags.isEmpty() ) value->setData( Qt::UserRole + 2, flags );
		}
		if ( IsStructural( key ) ) value->setFlags( value->flags() & ~Qt::ItemIsEditable );
		m_pTable->setItem( row, 1, value );
	}
	connect( m_pTable, &QTableWidget::cellChanged, this, [this]( int row, int column ) {
		if ( column == 1 && m_pTable->item( row, 1 ) )
		{
			QTableWidgetItem *item = m_pTable->item( row, 1 );
			if ( !item->data( Qt::UserRole ).toBool() ) item->setData( Qt::UserRole, true );
		}
	} );
	connect( m_pFilter, &QLineEdit::textChanged, this, [this] { RefreshNamesAndFilter(); } );
	connect( m_pRawKeys, &QCheckBox::toggled, this, [this] { RefreshNamesAndFilter(); } );
	RefreshNamesAndFilter();

	QHBoxLayout *tools = new QHBoxLayout();
	QPushButton *add = new QPushButton( tr( "Add Property..." ), this );
	QPushButton *remove = new QPushButton( tr( "Remove Property" ), this );
	tools->addWidget( add ); tools->addWidget( remove ); tools->addStretch();
	layout->addLayout( tools );
	connect( add, &QPushButton::clicked, this, &CMapPropertiesDialog::AddProperty );
	connect( remove, &QPushButton::clicked, this, &CMapPropertiesDialog::RemoveProperty );
	QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
	layout->addWidget( buttons );
	connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
}

QMap<QString, QString> CMapPropertiesDialog::Properties() const
{
	QMap<QString, QString> properties;
	for ( int row = 0; row < m_pTable->rowCount(); ++row )
	{
		QTableWidgetItem *key = m_pTable->item( row, 0 );
		QTableWidgetItem *value = m_pTable->item( row, 1 );
		if ( key && value && value->data( Qt::UserRole ).toBool() )
			properties.insert( key->data( Qt::UserRole ).toString(), value->text() );
	}
	return properties;
}

void CMapPropertiesDialog::AddProperty()
{
	bool ok = false;
	const QString key = QInputDialog::getText( this, tr( "Add Property" ), tr( "Property key:" ), QLineEdit::Normal, QString(), &ok ).trimmed();
	if ( !ok ) return;
	if ( key.isEmpty() || key.contains( QLatin1Char( '\n' ) ) || key.contains( QLatin1Char( '\r' ) ) || IsStructural( key ) )
	{
		QMessageBox::warning( this, tr( "Map Properties" ), tr( "Enter a valid property key." ) );
		return;
	}
	for ( int row = 0; row < m_pTable->rowCount(); ++row )
	{
		if ( m_pTable->item( row, 0 )->data( Qt::UserRole ).toString().compare( key, Qt::CaseInsensitive ) == 0 )
		{
			m_pFilter->clear();
			m_pTable->setCurrentCell( row, 1 );
			m_pTable->editItem( m_pTable->item( row, 1 ) );
			return;
		}
	}
	const int row = m_pTable->rowCount();
	m_pTable->insertRow( row );
	QTableWidgetItem *name = new QTableWidgetItem( key );
	name->setData( Qt::UserRole, key );
	name->setData( Qt::UserRole + 1, key );
	name->setFlags( name->flags() & ~Qt::ItemIsEditable );
	m_pTable->setItem( row, 0, name );
	QTableWidgetItem *value = new QTableWidgetItem();
	value->setData( Qt::UserRole, true );
	m_pTable->setItem( row, 1, value );
	m_pFilter->clear();
	m_pTable->setCurrentCell( row, 1 );
	m_pTable->editItem( value );
}

void CMapPropertiesDialog::RemoveProperty()
{
	const int row = m_pTable->currentRow();
	if ( row < 0 || m_pTable->isRowHidden( row ) || !m_pTable->item( row, 0 ) ||
		IsStructural( m_pTable->item( row, 0 )->data( Qt::UserRole ).toString() ) )
		return;
	m_pTable->removeRow( row );
}

void CMapPropertiesDialog::RefreshNamesAndFilter()
{
	const QString filter = m_pFilter->text().trimmed();
	for ( int row = 0; row < m_pTable->rowCount(); ++row )
	{
		QTableWidgetItem *name = m_pTable->item( row, 0 );
		if ( !name ) continue;
		const QString key = name->data( Qt::UserRole ).toString();
		const QString label = name->data( Qt::UserRole + 1 ).toString();
		name->setText( m_pRawKeys->isChecked() ? key : label );
		name->setToolTip( key );
		m_pTable->setRowHidden( row, !filter.isEmpty() &&
			!key.contains( filter, Qt::CaseInsensitive ) && !label.contains( filter, Qt::CaseInsensitive ) );
	}
}
