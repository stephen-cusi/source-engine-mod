#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QStyledItemDelegate>

// Each activation toggles one flag; reopening the editor permits additional
// flags. Bits not described by the FGD are deliberately left untouched.
class CPropertyFlagsEditor : public QComboBox
{
public:
    CPropertyFlagsEditor( QWidget *parent, const QString &original, quint32 value )
        : QComboBox( parent ), m_original( original ), m_value( value )
    {
        connect( this, qOverload<int>( &QComboBox::activated ), this, [this]( int index ) {
            const quint32 mask = itemData( index ).toUInt();
            if ( !mask ) return;
            m_value = ( ( m_value & mask ) == mask ) ? ( m_value & ~mask ) : ( m_value | mask );
            m_changed = true;
        } );
    }
    QString value() const { return m_changed ? QString::number( m_value ) : m_original; }
    quint32 bits() const { return m_value; }
private:
    QString m_original;
    quint32 m_value;
    bool m_changed = false;
};

class CPropertyValueDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor( QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index ) const override
    {
        // A mixed/unset label is display-only, never a value to write to VMF.
        if ( index.data( Qt::UserRole + 3 ).toBool() ) return new QLineEdit( parent );
        const QVariantList flags = index.data( Qt::UserRole + 2 ).toList();
        if ( !flags.isEmpty() )
        {
            const QString original = index.data( Qt::EditRole ).toString();
            bool ok = original.isEmpty();
            const qlonglong parsed = original.isEmpty() ? 0 : original.toLongLong( &ok );
            if ( !ok || parsed < -2147483648LL || parsed > 4294967295LL )
                return QStyledItemDelegate::createEditor( parent, option, index );
            CPropertyFlagsEditor *editor = new CPropertyFlagsEditor( parent, original, static_cast<quint32>( parsed ) );
            for ( const QVariant &entry : flags )
            {
                const QStringList pair = entry.toStringList();
                if ( pair.size() != 2 ) continue;
                bool valid = false;
                const quint32 mask = pair[0].toUInt( &valid );
                if ( !valid || !mask ) continue;
                editor->addItem( ( ( editor->bits() & mask ) == mask ? QStringLiteral( "[x] " ) : QStringLiteral( "[ ] " ) ) + pair[1], mask );
            }
            editor->setCurrentIndex( -1 );
            connect( editor, qOverload<int>( &QComboBox::activated ), editor, [this, editor]( int ) {
                CPropertyValueDelegate *delegate = const_cast<CPropertyValueDelegate *>( this );
                emit delegate->commitData( editor );
                emit delegate->closeEditor( editor );
            } );
            return editor;
        }
        const QVariantList choices = index.data( Qt::UserRole + 1 ).toList();
        if ( choices.isEmpty() ) return QStyledItemDelegate::createEditor( parent, option, index );
        QComboBox *combo = new QComboBox( parent );
        const QString current = index.data( Qt::EditRole ).toString();
        int selected = -1;
        for ( const QVariant &entry : choices )
        {
            const QStringList pair = entry.toStringList();
            if ( pair.size() != 2 ) continue;
            combo->addItem( pair[1], pair[0] );
            if ( pair[0] == current ) selected = combo->count() - 1;
        }
        if ( selected < 0 )
        {
            combo->insertItem( 0, current, current );
            selected = 0;
        }
        combo->setCurrentIndex( selected );
        return combo;
    }
    void setEditorData( QWidget *editor, const QModelIndex &index ) const override
    {
        if ( index.data( Qt::UserRole + 3 ).toBool() )
        {
            if ( QLineEdit *line = qobject_cast<QLineEdit *>( editor ) ) { line->clear(); line->setModified( false ); return; }
        }
        if ( dynamic_cast<CPropertyFlagsEditor *>( editor ) ) return;
        if ( index.data( Qt::UserRole + 1 ).toList().isEmpty() || !qobject_cast<QComboBox *>( editor ) )
            QStyledItemDelegate::setEditorData( editor, index );
    }
    void setModelData( QWidget *editor, QAbstractItemModel *model, const QModelIndex &index ) const override
    {
        if ( index.data( Qt::UserRole + 3 ).toBool() )
        {
            QLineEdit *line = qobject_cast<QLineEdit *>( editor );
            if ( line && line->isModified() ) model->setData( index, line->text(), Qt::EditRole );
            return;
        }
        if ( CPropertyFlagsEditor *flags = dynamic_cast<CPropertyFlagsEditor *>( editor ) )
        {
            model->setData( index, flags->value(), Qt::EditRole );
            return;
        }
        QComboBox *combo = qobject_cast<QComboBox *>( editor );
        if ( combo && !index.data( Qt::UserRole + 1 ).toList().isEmpty() )
            model->setData( index, combo->currentData().toString(), Qt::EditRole );
        else QStyledItemDelegate::setModelData( editor, model, index );
    }
};
