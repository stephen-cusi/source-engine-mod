// Worldspawn key/value editor. Changes are staged until the dialog is accepted.
#pragma once

#include <QDialog>
#include <QMap>
#include <QString>

class QTableWidget;
class QLineEdit;
class QCheckBox;
class CFgdData;

class CMapPropertiesDialog : public QDialog
{
	Q_OBJECT
public:
	explicit CMapPropertiesDialog( const QMap<QString, QString> &properties,
		const QMap<QString, QString> &fgdProperties, QWidget *parent = nullptr,
		const CFgdData *fgd = nullptr );
	QMap<QString, QString> Properties() const;

private:
	void AddProperty();
	void RemoveProperty();
	void RefreshNamesAndFilter();
	QTableWidget *m_pTable;
	QLineEdit *m_pFilter;
	QCheckBox *m_pRawKeys;
};
