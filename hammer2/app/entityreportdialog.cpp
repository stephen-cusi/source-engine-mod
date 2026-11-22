#include "entityreportdialog.h"
#include "document.h"
#include "brushentityview.h"

#include <QCheckBox>
#include <QCompleter>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
QString T( const char *text ) { return QCoreApplication::translate( "CEntityReportDialog", text ); }
}

CEntityReportDialog::CEntityReportDialog( CHammerDocument *document, QWidget *parent, ActionHandler handleAction )
    : QDialog( parent ), m_document( document )
{
    setWindowTitle( T( "Entity Report" ) );
    resize( 700, 450 );
    QVBoxLayout *layout = new QVBoxLayout( this );
    QFormLayout *form = new QFormLayout;
    m_entityType = new QComboBox( this );
    m_entityType->setObjectName( "entityReportType" );
    m_entityType->addItem( T( "All entities" ), 0 );
    m_entityType->addItem( T( "Point entities" ), 1 );
    m_entityType->addItem( T( "Brush entities" ), 2 );
    form->addRow( T( "Entity type:" ), m_entityType );
    m_classFilter = new QLineEdit( this );
    m_classFilter->setObjectName( "entityReportClass" );
    QStringList classes = m_document->Fgd().PointClassNames();
    for ( const QString &name : m_document->Fgd().SolidClassNames() )
        if ( !classes.contains( name, Qt::CaseInsensitive ) ) classes.append( name );
    for ( const EntityLite &entity : m_document->Map().entities )
        if ( !entity.classname.isEmpty() && !classes.contains( entity.classname, Qt::CaseInsensitive ) )
            classes.append( entity.classname );
    classes.sort( Qt::CaseInsensitive );
    QCompleter *classCompleter = new QCompleter( classes, m_classFilter );
    classCompleter->setCaseSensitivity( Qt::CaseInsensitive );
    classCompleter->setFilterMode( Qt::MatchContains );
    m_classFilter->setCompleter( classCompleter );
    form->addRow( T( "Class contains:" ), m_classFilter );
    m_useKeyValue = new QCheckBox( T( "Filter by key/value" ), this );
    m_useKeyValue->setObjectName( "entityReportKeyFilter" );
    form->addRow( m_useKeyValue );
    m_key = new QLineEdit( this );
    m_key->setObjectName( "entityReportKey" );
    m_key->setPlaceholderText( T( "Any property" ) );
    m_value = new QLineEdit( this );
    m_value->setObjectName( "entityReportValue" );
    m_exact = new QCheckBox( T( "Exact value" ), this );
    m_value->setPlaceholderText( T( "Empty: match existing properties, or empty values when exact" ) );
    form->addRow( T( "Property key:" ), m_key );
    form->addRow( T( "Value:" ), m_value );
    m_exact->setObjectName( "entityReportExact" );
    form->addRow( m_exact );
    m_selectedOnly = new QCheckBox( T( "Only currently selected entities" ), this );
    m_selectedOnly->setObjectName( "entityReportSelectedOnly" );
    form->addRow( m_selectedOnly );
    layout->addLayout( form );
    m_table = new QTableWidget( 0, 3, this );
    m_table->setObjectName( "entityReportTable" );
    m_table->setHorizontalHeaderLabels( QStringList() << T( "Class" ) << T( "Name" ) << T( "Origin" ) );
    m_table->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
    m_table->setSelectionBehavior( QAbstractItemView::SelectRows );
    m_table->setSelectionMode( QAbstractItemView::ExtendedSelection );
    m_table->setEditTriggers( QAbstractItemView::NoEditTriggers );
    m_table->setSortingEnabled( true );
    layout->addWidget( m_table );
    m_count = new QLabel( this );
    m_count->setObjectName( "entityReportCount" );
    layout->addWidget( m_count );
    QDialogButtonBox *buttons = new QDialogButtonBox(
        handleAction ? QDialogButtonBox::Close : QDialogButtonBox::Cancel, this );
    QPushButton *select = buttons->addButton( T( "Select" ), QDialogButtonBox::ActionRole );
    QPushButton *goTo = buttons->addButton( T( "Go to" ), QDialogButtonBox::ActionRole );
    QPushButton *properties = buttons->addButton( T( "Properties" ), QDialogButtonBox::ActionRole );
    QPushButton *remove = buttons->addButton( T( "Delete" ), QDialogButtonBox::ActionRole );
    goTo->setObjectName( "entityReportGoTo" );
    properties->setObjectName( "entityReportProperties" );
    remove->setObjectName( "entityReportDelete" );
    goTo->setEnabled( false );
    properties->setEnabled( false );
    remove->setEnabled( false );
    layout->addWidget( buttons );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    select->setEnabled( false );
    connect( m_table, &QTableWidget::itemSelectionChanged, this, [this, select, goTo, properties, remove] {
        const bool hasSelection = !SelectedEntityIds().isEmpty();
        select->setEnabled( hasSelection );
        goTo->setEnabled( hasSelection );
        properties->setEnabled( hasSelection );
        remove->setEnabled( hasSelection );
    } );
    const auto applyAction = [this, handleAction]( int result ) {
        const QSet<int> selected = SelectedEntityIds();
        if ( selected.isEmpty() ) return;
        if ( handleAction ) handleAction( result, selected );
        else done( result );
    };
    connect( select, &QPushButton::clicked, this, [applyAction] { applyAction( QDialog::Accepted ); } );
    connect( goTo, &QPushButton::clicked, this, [applyAction] { applyAction( GoToResult ); } );
    connect( properties, &QPushButton::clicked, this, [applyAction] { applyAction( PropertiesResult ); } );
    connect( remove, &QPushButton::clicked, this, [this] {
        const QSet<int> selected = SelectedEntityIds();
        if ( selected.isEmpty() || QMessageBox::question( this, T( "Delete Objects" ),
             T( "Delete selected objects?" ), QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes )
            return;
        m_document->SelectObjects( selected );
        if ( m_document->DeleteSelection() ) Refresh();
    } );
    connect( m_table, &QTableWidget::cellDoubleClicked, this, [applyAction]( int, int ) { applyAction( QDialog::Accepted ); } );
    connect( m_classFilter, &QLineEdit::textChanged, this, [this] { Refresh(); } );
    connect( m_entityType, &QComboBox::currentIndexChanged, this, [this] { Refresh(); } );
    connect( m_key, &QLineEdit::textChanged, this, [this] { Refresh(); } );
    connect( m_value, &QLineEdit::textChanged, this, [this] { Refresh(); } );
    connect( m_exact, &QCheckBox::toggled, this, [this] { Refresh(); } );
    connect( m_selectedOnly, &QCheckBox::toggled, this, [this] { Refresh(); } );
    connect( m_useKeyValue, &QCheckBox::toggled, this, [this]( bool enabled ) {
        m_key->setEnabled( enabled ); m_value->setEnabled( enabled ); m_exact->setEnabled( enabled ); Refresh();
    } );
    connect( m_document, &CHammerDocument::stateChanged, this, [this] { Refresh(); } );
    connect( m_document, &CHammerDocument::selectionChanged, this, [this] {
        if ( m_selectedOnly->isChecked() ) Refresh();
    } );
    m_key->setEnabled( false ); m_value->setEnabled( false ); m_exact->setEnabled( false );
    Refresh();
}

void CEntityReportDialog::Refresh()
{
    const quint64 identity = m_document->DocumentIdentity();
    const QSet<int> previous = identity == m_documentIdentity ? SelectedEntityIds() : QSet<int>();
    m_documentIdentity = identity;
    const bool sorting = m_table->isSortingEnabled();
    m_table->setSortingEnabled( false );
    m_table->setRowCount( 0 );
    for ( const EntityLite &entity : m_document->Map().entities )
    {
        if ( !m_document->GetViewFilter().EntityVisible( entity ) ) continue;
        bool ownsBrush = false, visibleBrush = false;
        for ( const SolidLite &solid : m_document->Map().solids ) if ( solid.ownerEntityId == entity.id )
        {
            ownsBrush = true;
            visibleBrush = visibleBrush || HammerBrushEntityView::Visible( m_document->Map(), m_document->GetViewFilter(), solid );
        }
        if ( ownsBrush && !visibleBrush ) continue;
        const int type = m_entityType->currentData().toInt();
        if ( ( type == 1 && ownsBrush ) || ( type == 2 && !ownsBrush ) ) continue;
        if ( m_selectedOnly->isChecked() && !m_document->IsSelected( entity.id ) ) continue;
        if ( !entity.classname.contains( m_classFilter->text(), Qt::CaseInsensitive ) ) continue;
        if ( m_useKeyValue->isChecked() )
        {
            const QString key = m_key->text(), value = m_value->text();
            const auto matches = [&]( const QString &k, const QString &v ) {
                return ( key.isEmpty() || k.compare( key, Qt::CaseInsensitive ) == 0 ) &&
                    ( m_exact->isChecked() ? v.compare( value, Qt::CaseInsensitive ) == 0 :
                      v.contains( value, Qt::CaseInsensitive ) );
            };
            bool found = matches( QStringLiteral( "classname" ), entity.classname ) ||
                matches( QStringLiteral( "origin" ), FormatVec3( entity.origin ) );
            for ( auto it = entity.keyvalues.constBegin(); !found && it != entity.keyvalues.constEnd(); ++it )
                found = matches( it.key(), it.value() );
            if ( !found ) continue;
        }
        const int row = m_table->rowCount();
        m_table->insertRow( row );
        QTableWidgetItem *classItem = new QTableWidgetItem( entity.classname );
        classItem->setData( Qt::UserRole, entity.id );
        m_table->setItem( row, 0, classItem );
        QString targetName;
        for ( auto it = entity.keyvalues.cbegin(); it != entity.keyvalues.cend(); ++it )
            if ( it.key().compare( QLatin1String( "targetname" ), Qt::CaseInsensitive ) == 0 ) { targetName = it.value(); break; }
        m_table->setItem( row, 1, new QTableWidgetItem( targetName ) );
        m_table->setItem( row, 2, new QTableWidgetItem( FormatVec3( entity.origin ) ) );
        if ( previous.contains( entity.id ) )
            for ( int col = 0; col < 3; ++col ) m_table->item( row, col )->setSelected( true );
    }
    m_table->setSortingEnabled( sorting );
    m_count->setText( T( "%1 of %2 entities" ).arg( m_table->rowCount() ).arg( m_document->Map().entities.size() ) );
}

QSet<int> CEntityReportDialog::SelectedEntityIds() const
{
    QSet<int> ids;
    for ( const QModelIndex &index : m_table->selectionModel()->selectedRows() )
        if ( QTableWidgetItem *item = m_table->item( index.row(), 0 ) )
            ids.insert( item->data( Qt::UserRole ).toInt() );
    return ids;
}
