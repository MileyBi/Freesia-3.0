#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include "common.h"
#include <QMainWindow>
#include <QStandardItemModel>
#include<QItemSelection>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
    bool m_bViewerFullscreen = false;
    QModelIndexList selectIndexs;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;

private slots:
    void treeviewUpdate();
    void valueChanged3d();
    void valueChanged2d();
    void selectItemByColor(int);
    void selectMultiItemByColor(int);
    void selectColorChanged(QModelIndex);
    void scrollBarUpdate(int);
    void scrollboxIndex(int);
    void onTransform3dChanged(TransformParameters*);
    void onTransform2dChanged(TransformParameters*);
protected:
    virtual void keyPressEvent(QKeyEvent*);
};

#endif // MAINWINDOW_H
