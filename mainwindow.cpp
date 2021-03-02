#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "loadbrainatlases.h"
#include "loadsliceimages.h"
#include "buildwarpingfield.h"
#include "statusbar.h"
#include <QMouseEvent>
#include <QKeyEvent>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    Common *c=Common::i();StatusBar *statusBar=new StatusBar(true);
    LoadBrainAtlases *m=LoadBrainAtlases::i();LoadSliceImages *s=LoadSliceImages::i();

    ui->setupUi(this); 
    ui->splitter->setStretchFactor(0,2);ui->splitter->setStretchFactor(1,11);
    ui->statusBar->addWidget(statusBar,1);
    ui->viewWidget->insertWidget(0, c->m_imageViewer);
    connect(c,&Common::toggleFullscreen,[this]{if(!m_bViewerFullscreen){ui->toolBox->hide(); m_bViewerFullscreen=!m_bViewerFullscreen;}else{ui->toolBox->show();m_bViewerFullscreen=!m_bViewerFullscreen;}});
    connect(c,&Common::showMessage,statusBar,&StatusBar::showMessage,Qt::DirectConnection);
    connect(ui->selectAtlas, SIGNAL(currentIndexChanged(int)), m, SLOT(loadItems(int)));
    connect(m, SIGNAL(modelSelected()), this, SLOT(treeviewUpdate()));
    connect(c, &Common::atlasNumberChanged, ui->horizontalScrollBar, [this](int number){ui->indexEdit->setMaximum(number);ui->horizontalScrollBar->setRange(1,number);int v=(1+number)/2;ui->horizontalScrollBar->setValue(v);});
    connect(ui->horizontalScrollBar, SIGNAL(valueChanged(int)), this, SLOT(scrollBarUpdate(int)));
    ui->selectAtlas->addItems(m->m_modelNames);
/***********************************************************************************************************************************************************************************/
    connect(m, SIGNAL(noItemSelect()), ui->treeView,SLOT(clearSelection()));
    connect(m,SIGNAL(regionSelected1(int)),this, SLOT(selectItemByColor(int)));
    connect(m,SIGNAL(regionSelected2(int)),this, SLOT(selectMultiItemByColor(int)));
    connect(ui->treeView,&QTreeView::clicked,[this](QModelIndex index){selectColorChanged(index);});

    connect(c,SIGNAL(comboBoxIndex(int)),ui->comboBox,SLOT(setCurrentIndex(int)));
    connect(ui->comboBox,SIGNAL(currentIndexChanged(int)), this,SLOT(scrollboxIndex(int)));
    connect(ui->actionAxial_Plane, SIGNAL(triggered()), c, SLOT(axialType()));
    connect(ui->actionCoronal_Plane, SIGNAL(triggered()), c, SLOT(coronalType()));
    connect(ui->actionSagital_Plane, SIGNAL(triggered()), c, SLOT(sagittalType()));
    connect(ui->actionBrain_Contour, SIGNAL(triggered()), m, SLOT(removeModelImage()));

    connect(ui->spinBox_rotationx, SIGNAL(valueChanged(double)), this, SLOT(valueChanged3d()));
    connect(ui->spinBox_rotationy, SIGNAL(valueChanged(double)), this, SLOT(valueChanged3d()));
    connect(ui->spinBox_offset2_x, SIGNAL(valueChanged(int)), this, SLOT(valueChanged3d()));
    connect(ui->spinBox_offset2_y, SIGNAL(valueChanged(int)), this, SLOT(valueChanged3d()));
    connect(ui->spinBox_scale2_x, SIGNAL(valueChanged(double)), this, SLOT(valueChanged3d()));
    connect(ui->spinBox_scale2_y, SIGNAL(valueChanged(double)), this, SLOT(valueChanged3d()));
/***********************************************************************************************************************************************************************************/
    connect(ui->verticalSlider, SIGNAL(valueChanged(int)), s, SLOT(onContrastChanged(int)));
    connect(s, SIGNAL(imageUpdate()),c,SLOT(updateImage()),Qt::QueuedConnection);
    connect(ui->actionOpen_Dir, SIGNAL(triggered()),s,SLOT(onImportDirectory()));
    connect(ui->actionOpen_File,SIGNAL(triggered()),s,SLOT(onLoadProject()));
    connect(ui->actionAutoCrop,SIGNAL(toggled(bool)), s, SLOT(changeAutoCrop(bool)));
    connect(ui->actionAutoRotate,SIGNAL(toggled(bool)), s, SLOT(changeAutoRotate(bool)));
    connect(ui->spinBox_crop1_b, SIGNAL(valueChanged(int)), this, SLOT(valueChanged2d()));
    connect(ui->spinBox_crop1_t, SIGNAL(valueChanged(int)), this, SLOT(valueChanged2d()));
    connect(ui->spinBox_crop1_l, SIGNAL(valueChanged(int)), this, SLOT(valueChanged2d()));
    connect(ui->spinBox_crop1_r, SIGNAL(valueChanged(int)), this, SLOT(valueChanged2d()));
    connect(ui->spinBox_offset1_x, SIGNAL(valueChanged(int)), this, SLOT(valueChanged2d()));
    connect(ui->spinBox_offset1_y, SIGNAL(valueChanged(int)), this, SLOT(valueChanged2d()));
    connect(ui->spinBox_scale1_x, SIGNAL(valueChanged(double)), this, SLOT(valueChanged2d()));
    connect(ui->spinBox_scale1_y, SIGNAL(valueChanged(double)), this, SLOT(valueChanged2d()));
    connect(ui->spinBox_rotationz, SIGNAL(valueChanged(double)), this, SLOT(valueChanged2d()));
    connect(c, &Common::transform2dChanged, this, &MainWindow::onTransform2dChanged,Qt::QueuedConnection);
    connect(c, &Common::transform3dChanged, this, &MainWindow::onTransform3dChanged,Qt::QueuedConnection);
    connect(s, &LoadSliceImages::addComboBoxItems, [this](QStringList items){ui->comboBox->clear();ui->comboBox->addItems(items);});
/***********************************************************************************************************************************************************************************/
    connect(ui->actionCellLabelling, &QAction::toggled, [this](bool check){if(check){ui->actionSliceWarping->setChecked(false);}Common::i()->onCellLabel(check);});
    connect(ui->actionSliceWarping, &QAction::toggled, [this](bool check){if(check){ui->actionCellLabelling->setChecked(false);}Common::i()->onSliceWarping(check);});
    connect(ui->actionRemoveWarpingMarker, SIGNAL(triggered()), c, SLOT(removeAllWarping()));
    connect(ui->actionRemoveCellMarker, SIGNAL(triggered()), c, SLOT(removeAllCell()));
    connect(ui->actionImport, SIGNAL(triggered()), s, SLOT(onImportSpots()));
    connect(ui->actionSave,SIGNAL(triggered()),s,SLOT(onSaveProject()));
    connect(ui->actionExport,SIGNAL(triggered()),s,SLOT(onExportCellCounting()));
    connect(s,SIGNAL(mergeCellCounting(QString)),m,SLOT(onMergeCellCounting(QString)));
}


void MainWindow::treeviewUpdate(){ui->treeView->setModel(LoadBrainAtlases::i()->m_pModel);}

void MainWindow::valueChanged3d(){
    if(!((QSpinBox*)sender())->hasFocus()&&!((QDoubleSpinBox*)sender())->hasFocus()){return;}
    TransformParameters *ptr=new TransformParameters;
    ptr->rotation[0]=ui->spinBox_rotationx->value();ptr->rotation[1]=ui->spinBox_rotationy->value();
    ptr->offset[0]=ui->spinBox_offset2_x->value();ptr->offset[1]=ui->spinBox_offset2_y->value();
    ptr->scale[0]=ui->spinBox_scale2_x->value();ptr->scale[1]=ui->spinBox_scale2_y->value();
    delete Common::i()->p_params3d.exchange(ptr);
    LoadBrainAtlases::i()->transform3d();
}

void MainWindow::valueChanged2d(){
    if(!((QSpinBox*)sender())->hasFocus()&&!((QDoubleSpinBox*)sender())->hasFocus()){return;}
    if(!LoadSliceImages::i()->m_bEdited){return;}
    TransformParameters *ptr=new TransformParameters;
    ptr->rotation[2]=ui->spinBox_rotationz->value();ptr->sliceIndex=ui->groupBox_1->title().split("#")[1].toInt();
    ptr->offset[0]=ui->spinBox_offset1_x->value();ptr->offset[1]=ui->spinBox_offset1_y->value();
    ptr->scale[0]=ui->spinBox_scale1_x->value();ptr->scale[1]=ui->spinBox_scale1_y->value();
    ptr->roi.x=ui->spinBox_crop1_l->value();ptr->roi.y=ui->spinBox_crop1_t->value();
    ptr->roi.width=ui->spinBox_crop1_r->value()-ui->spinBox_crop1_l->value();
    ptr->roi.height=ui->spinBox_crop1_b->value()-ui->spinBox_crop1_t->value();
    delete Common::i()->p_params2d.exchange(ptr);
    LoadSliceImages::i()->transform2d();
}

void MainWindow::onTransform3dChanged(TransformParameters *p){
    ui->spinBox_rotationx->setValue(p->rotation[0]);ui->spinBox_rotationy->setValue(p->rotation[1]);
    ui->spinBox_offset2_x->setValue(p->offset[0]);ui->spinBox_offset2_y->setValue(p->offset[1]);
    ui->spinBox_scale2_x->setValue(p->scale[0]);ui->spinBox_scale2_y->setValue(p->scale[1]);
    delete p;
}

void MainWindow::onTransform2dChanged(TransformParameters *p){
    ui->groupBox_1->setTitle(QString("Slice Parameters-#%1").arg(p->sliceIndex));
    ui->spinBox_rotationz->setValue(p->rotation[2]);
    ui->spinBox_crop1_l->setValue(p->roi.x);ui->spinBox_crop1_t->setValue(p->roi.y);
    ui->spinBox_crop1_r->setValue(p->roi.x+p->roi.width);ui->spinBox_crop1_b->setValue(p->roi.y+p->roi.height);
    ui->spinBox_offset1_x->setValue(p->offset[0]);ui->spinBox_offset1_y->setValue(p->offset[1]);
    ui->spinBox_scale1_x->setValue(p->scale[0]);ui->spinBox_scale1_y->setValue(p->scale[1]);
    delete p;
}

void MainWindow::selectItemByColor(int color){
    selectIndexs.clear();
    RegionTree *node=LoadBrainAtlases::i()->m_color2RegionTree.value(color);if(nullptr==node){return;}
    QModelIndex index=node->modelIndex;ui->treeView->collapseAll();
    QModelIndex parentIndex=index.parent();
    while(parentIndex.isValid()){ui->treeView->expand(parentIndex);parentIndex=parentIndex.parent();}
    ui->treeView->selectionModel()->select(index,QItemSelectionModel::Select);ui->treeView->scrollTo(index);
}

void MainWindow::selectMultiItemByColor(int color){
    RegionTree *node=LoadBrainAtlases::i()->m_color2RegionTree.value(color);if(nullptr==node){return;}
    QModelIndex index=node->modelIndex;ui->treeView->scrollTo(index);
    if(selectIndexs.contains(index)){selectIndexs.removeOne(index);}else{selectIndexs.append(index);}
    if(selectIndexs.isEmpty()){ui->treeView->collapseAll();return;}
    QItemSelection selection;
    foreach(QModelIndex index, selectIndexs) {QItemSelection sel(index, index);selection.merge(sel, QItemSelectionModel::Select);}
    ui->treeView->selectionModel()->select(selection, QItemSelectionModel::Select);
}

void MainWindow::selectColorChanged(QModelIndex index){
    if(QApplication::keyboardModifiers () == Qt::ControlModifier){
        LoadBrainAtlases::i()->selectRows(index);
        if(selectIndexs.contains(index)){selectIndexs.removeOne(index);}else{selectIndexs.append(index);}
        if(selectIndexs.isEmpty()){ui->treeView->collapseAll();return;}
        QItemSelection selection;
        foreach(QModelIndex index, selectIndexs) {QItemSelection sel(index, index);selection.merge(sel, QItemSelectionModel::Select);}
        ui->treeView->selectionModel()->select(selection, QItemSelectionModel::Select);
    }
    else{LoadBrainAtlases::i()->selectRow(index);selectIndexs.clear();ui->treeView->selectionModel()->select(index,QItemSelectionModel::Select);}
}

void MainWindow::scrollBarUpdate(int index){
    LoadBrainAtlases *m=LoadBrainAtlases::i();LoadSliceImages *s=LoadSliceImages::i();Common *c=Common::i();
    if(ui->comboBox->currentIndex()==0){
        m->updateModel(index);s->changeImageModel(index);}
    else{
        if(c->m_modelType==2){
            int m_modelIndex=s->m_images[index-1]->modelIndex;
            m->updateModel(m_modelIndex);s->m_imageIndex=index;}
        else{
            m->updateModel(index);s->m_imageIndex=index;}
    }
}

void MainWindow::scrollboxIndex(int index){
    LoadBrainAtlases *m=LoadBrainAtlases::i(); LoadSliceImages *s=LoadSliceImages::i();Common *c=Common::i();
    c->m_conboBoxIndex=ui->comboBox->currentIndex();
    if(index==0){
        int maximun=c->m_maxModelNumber;
        int current=m->m_modelIndex;
        ui->indexEdit->setMaximum(maximun);ui->horizontalScrollBar->setRange(1,maximun);ui->horizontalScrollBar->setValue(current);}
    else{
        int maximun=c->m_maxImageNumber;
        int current=s->m_imageIndex;
        ui->indexEdit->setMaximum(maximun);ui->horizontalScrollBar->setRange(1,maximun);ui->horizontalScrollBar->setValue(current);
        if(Common::i()->m_modelType==2){int m_modelIndex=s->m_images[current-1]->modelIndex;m->updateModel(m_modelIndex);}
        s->m_isTransformed=true;
    }
}

void MainWindow::keyPressEvent(QKeyEvent *e){
    Common *c=Common::i();
    if(e->isAutoRepeat()||e->modifiers()!=Qt::NoModifier){return;}
    if(!c->isSliceWarping&&!c->isCellPicking){return;}
    switch(e->key()){
    case Qt::Key_Delete:{c->isSliceWarping?BuildWarpingField::i()->removeCurrentNode():PickCellPoints::i()->removeCurrentPoint();}break;
    case Qt::Key_Backspace:{c->isSliceWarping?BuildWarpingField::i()->removeCurrentNode():PickCellPoints::i()->removeCurrentPoint();}break;
    }
}

MainWindow::~MainWindow(){delete ui;}

