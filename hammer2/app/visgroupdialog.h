#pragma once

#include "document.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

// Manual groups only. Each change is committed as one document undo step.
class CVisgroupDialog : public QDialog
{
public:
	explicit CVisgroupDialog( CHammerDocument *document, QWidget *parent = nullptr ) : QDialog( parent ), m_document( document )
	{
		setWindowTitle( T( "Visgroups" ) ); resize( 480, 420 );
		auto *layout = new QVBoxLayout( this );
		auto *help = new QLabel( T( "Select a group, then add or remove selected map objects. Show/Hide affects this session only; saved visibility and hidden map objects remain unchanged." ), this );
		help->setWordWrap( true ); layout->addWidget( help );
		m_tree = new QTreeWidget( this ); m_tree->setObjectName( "visgroupTree" );
		m_tree->setHeaderLabels( { T( "Group" ), T( "ID" ) } ); layout->addWidget( m_tree, 1 );
		auto *definitions = new QHBoxLayout;
		auto *newRoot = new QPushButton( T( "New Group..." ), this );
		auto *newChild = new QPushButton( T( "New Child..." ), this );
		auto *rename = new QPushButton( T( "Rename..." ), this );
		auto *deleteGroup = new QPushButton( T( "Delete Group..." ), this ); deleteGroup->setObjectName( "visgroupDelete" );
		definitions->addWidget( newRoot ); definitions->addWidget( newChild ); definitions->addWidget( rename ); definitions->addWidget( deleteGroup ); layout->addLayout( definitions );
		auto *members = new QHBoxLayout;
		auto *add = new QPushButton( T( "Add Selection to Group" ), this );
		auto *remove = new QPushButton( T( "Remove Selection from Group" ), this );
		auto *select = new QPushButton( T( "Select Group Members" ), this ); select->setObjectName( "visgroupSelectMembers" );
		members->addWidget( add ); members->addWidget( remove ); members->addWidget( select ); layout->addLayout( members );
		auto *visibility = new QHBoxLayout;
		auto *show = new QPushButton( T( "Show Group" ), this ); show->setObjectName( "visgroupShow" );
		auto *hide = new QPushButton( T( "Hide Group" ), this ); hide->setObjectName( "visgroupHide" );
		visibility->addWidget( show ); visibility->addWidget( hide ); layout->addLayout( visibility );
		auto *close = new QDialogButtonBox( QDialogButtonBox::Close, this );
		connect( close, &QDialogButtonBox::rejected, this, &QDialog::reject ); layout->addWidget( close );
		connect( newRoot, &QPushButton::clicked, this, [this] { CreateGroup( 0 ); } );
		connect( newChild, &QPushButton::clicked, this, [this] { if ( const int id = CurrentId() ) CreateGroup( id ); } );
		connect( rename, &QPushButton::clicked, this, [this] {
			const int id = CurrentId(); if ( !id ) return; bool ok = false;
			const QString value = QInputDialog::getText( this, T( "Rename Visgroup" ), T( "Name:" ), QLineEdit::Normal,
				m_tree->currentItem()->text( 0 ), &ok );
			if ( ok && !m_document->RenameVisgroup( id, value ) ) Error();
			if ( ok ) Refresh( id );
		} );
		connect( deleteGroup, &QPushButton::clicked, this, [this] {
			const int id = CurrentId(); if ( !id ) return;
			if ( QMessageBox::question( this, T( "Delete Visgroup" ), T( "Delete this group and remove its membership from objects? Session visibility overrides remain until changed or the map is reopened." ) ) != QMessageBox::Yes ) return;
			if ( !m_document->DeleteLeafVisgroup( id ) )
				QMessageBox::warning( this, T( "Delete Visgroup" ), T( "Only a group without child groups can be deleted. Groups referenced by hidden objects or colour assignments cannot yet be deleted." ) );
			else Refresh( 0 );
		} );
		connect( add, &QPushButton::clicked, this, [this] { ChangeMembership( true ); } );
		connect( remove, &QPushButton::clicked, this, [this] { ChangeMembership( false ); } );
		connect( select, &QPushButton::clicked, this, [this] {
			const int id = CurrentId(); if ( !id ) return;
			if ( m_document->SelectVisgroupMembers( id ) ) accept();
			else QMessageBox::information( this, T( "Visgroups" ), T( "No visible members were selected. Check the group ID and visibility." ) );
		} );
		connect( show, &QPushButton::clicked, this, [this] { const int id = CurrentId(); if ( id ) m_document->SetVisgroupVisible( id, true ); } );
		connect( hide, &QPushButton::clicked, this, [this] { const int id = CurrentId(); if ( id ) m_document->SetVisgroupVisible( id, false ); } );
		connect( m_tree, &QTreeWidget::itemSelectionChanged, this, [this, newChild, rename, deleteGroup, add, remove, select, show, hide] {
			const bool selected = CurrentId() > 0; newChild->setEnabled( selected ); rename->setEnabled( selected );
			deleteGroup->setEnabled( selected ); add->setEnabled( selected ); remove->setEnabled( selected ); select->setEnabled( selected ); show->setEnabled( selected ); hide->setEnabled( selected );
		} );
		newChild->setEnabled( false ); rename->setEnabled( false ); deleteGroup->setEnabled( false ); add->setEnabled( false ); remove->setEnabled( false ); select->setEnabled( false ); show->setEnabled( false ); hide->setEnabled( false );
		Refresh( 0 );
	}
	static QString T( const char *text ) { return QCoreApplication::translate( "VisgroupDialog", text ); }
private:
	int CurrentId() const { return m_tree->currentItem() ? m_tree->currentItem()->data( 0, Qt::UserRole ).toInt() : 0; }
	void Error() { QMessageBox::warning( this, T( "Visgroups" ), T( "Could not make the change. Check the group name and select visible map objects. Hidden or color-group assignments may need the original Hammer." ) ); }
	void CreateGroup( int parentId )
	{
		bool ok = false; const QString name = QInputDialog::getText( this, T( "New Visgroup" ), T( "Name:" ), QLineEdit::Normal, QString(), &ok );
		if ( !ok ) return; const int id = m_document->CreateVisgroup( name, parentId );
		if ( id < 0 ) { Error(); return; } Refresh( id );
	}
	void ChangeMembership( bool add ) { const int id = CurrentId(); if ( id && !m_document->SetSelectionVisgroupMembership( id, add ) ) Error(); }
	void Refresh( int selected )
	{
		m_tree->clear(); QTreeWidgetItem *chosen = nullptr;
		std::function<void( const QList<VisGroupLite> &, QTreeWidgetItem * )> fill = [&]( const QList<VisGroupLite> &groups, QTreeWidgetItem *parent ) {
			for ( const VisGroupLite &group : groups )
			{
				QString name; int id = 0;
				for ( const auto &p : group.properties ) {
					if ( p.first.compare( QLatin1String( "name" ), Qt::CaseInsensitive ) == 0 ) name = p.second;
					if ( p.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 ) id = p.second.toInt();
				}
				auto *item = parent ? new QTreeWidgetItem( parent ) : new QTreeWidgetItem( m_tree );
				item->setText( 0, name.isEmpty() ? T( "(unnamed)" ) : name ); item->setText( 1, QString::number( id ) );
				item->setData( 0, Qt::UserRole, id ); if ( id == selected ) chosen = item; fill( group.children, item );
			}
		};
		fill( m_document->Map().visgroups, nullptr ); m_tree->expandAll(); if ( chosen ) m_tree->setCurrentItem( chosen );
	}
	CHammerDocument *m_document;
	QTreeWidget *m_tree;
};
