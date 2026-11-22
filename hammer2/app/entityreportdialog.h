#pragma once

#include <QDialog>
#include <QSet>

class CHammerDocument;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QTableWidget;

class CEntityReportDialog : public QDialog
{
public:
    enum { GoToResult = QDialog::Accepted + 1, PropertiesResult };
    explicit CEntityReportDialog( CHammerDocument *document, QWidget *parent = nullptr );
    void Refresh();
    QSet<int> SelectedEntityIds() const;
private:
    CHammerDocument *m_document;
    QLineEdit *m_classFilter;
    QComboBox *m_entityType;
    QCheckBox *m_useKeyValue;
    QLineEdit *m_key;
    QLineEdit *m_value;
    QCheckBox *m_exact;
    QCheckBox *m_selectedOnly;
    QTableWidget *m_table;
    QLabel *m_count;
};
