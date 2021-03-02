#ifndef IMPORTEXPORTDIALOG_H
#define IMPORTEXPORTDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class PathEdit;
class QDoubleSpinBox;
class QSpinBox;
QT_END_NAMESPACE

struct ImportParams{
    float voxels[3];QString path;int groupSize;
};

class ImportDialog : public QDialog
{
    Q_OBJECT

    PathEdit *m_pathEdit;
    QDoubleSpinBox *m_voxelBoxes[3];
    QSpinBox *m_groupSizeBox;
public:
    explicit ImportDialog();
    ~ImportDialog();

    bool getParameters(ImportParams &p);
signals:

public slots:
};

#endif // IMPORTEXPORTDIALOG_H
