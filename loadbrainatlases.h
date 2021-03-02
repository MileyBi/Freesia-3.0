#ifndef LOADBRAINATLASES_H
#define LOADBRAINATLASES_H

#include <QWidget>
#include <QObject>
#include <QModelIndex>
#include <opencv2/opencv.hpp>
#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkImageReslice.h>
#include <vtkImageActor.h>
#include <QMutexLocker>

QT_BEGIN_NAMESPACE
class QTreeView;
class QStandardItemModel;
class QStandardItem;
class QLineEdit;
class QPainterPath;
struct TransformParameters;
QT_END_NAMESPACE

struct ModelInfo{
    QString name,fullName,imagePath,regionPath;
    float voxelSize;
    cv::Point3i dimension;
};

struct RegionNode{
    int id,parentId,color;
    QString acronym,name,path, colorHex;
    QList<RegionNode*> subRegions;
    RegionNode *parentNode;
    QList<int> voxelIndexes;
    int totalVoxelNumber;

    RegionNode(int _id,int _parentId,QString _acronym,QString _name,int _color, QString _colorHex)
        :id(_id),parentId(_parentId),color(_color),acronym(_acronym),name(_name),colorHex(_colorHex),parentNode(nullptr),totalVoxelNumber(0){}
    ~RegionNode(){qDeleteAll(subRegions);}
};

struct RegionTree{
    int id,parentId,color;
    QString acronym,name, colorHex;
    QList<RegionTree*> subRegions;
    QModelIndex modelIndex;
    RegionTree(int _id,int _parentId,int _color,QString _acronym,QString _name, QString _colorHex)
        :id(_id),parentId(_parentId),color(_color),acronym(_acronym),name(_name), colorHex(_colorHex){}
};

class LoadBrainAtlases : public QObject
{
    Q_OBJECT
    QString color_hex_hover,color_hex_select;
    const QString m_modelFolderPath;
    QList<ModelInfo> m_models;
    uint16_t *m_voxels;
    QList<RegionNode*> m_rootRegions;
    QMap<int,RegionNode*> m_color2Regions,m_id2Regions;

    QTreeView *m_pTableView;
    QList<QStandardItem*> m_items;
    QList<RegionTree*> m_allRegions, m_rootRegionsTree;
    QMap<void*,RegionTree*> m_models2RegionTree;
    QString m_searchText;int m_searchIndex;

    vtkSmartPointer<vtkImageData> m_modelImageData,m_selectImageData,m_hoverImageData,m_selectMultiImageData;
    vtkSmartPointer<vtkImageReslice> m_resliceModel,m_resliceSelect,m_resliceHover,m_resliceMultiSelect;
    vtkSmartPointer<vtkImageData> selectModelData,hoverModelData,selectMultiModelData;
    cv::Point3d m_volumeSize;
    const int m_modelFactor=2;
    QMutex m_imageActorMutex;
    vtkSmartPointer<vtkImageActor> m_selectActors[2],m_hoverActors[2],m_selectMultiActors[2];
    cv::Mat m_modelImage,m_nextModelImage;
    QList<int> m_colorStack;
    QMap<int,QString> m_color2ColorHex;

    LoadBrainAtlases();
    void loadFreesiaJson();
    bool loadRegions(const QString &filePath);
    QStandardItem* getItem(RegionTree*,QStandardItem *parent);
    QStandardItem* getItem(const QString &text,const QString &tooltipText);
    void setBuffer();
    void setBuffer1(char *buffer,const cv::Point3i &volumeSize,const cv::Point3d &voxelSize);
    void setBuffer2(char *buffer,const cv::Point3i &volumeSize,const cv::Point3d &voxelSize);
    void setBuffer3(char *buffer,const cv::Point3i &volumeSize,const cv::Point3d &voxelSize);
    void updateModelImage(const cv::Mat &image,double *origin,double *spacing);
    void updateSelectImage(const cv::Mat &image,double *origin,double *spacing);
    void updateSelectMultiImage(const cv::Mat &image,double *origin,double *spacing);
    void updateHoverImage(const cv::Mat &image,double *origin,double *spacing);
    void getRegionSubColors(RegionNode *node,QList<int> &colors);
    char *getRegionVoxelByColor(int color,bool isHover);
    char *getMultiRegionVoxelByColor();
    bool dumpCellCounting(size_t colorCounts[65536],const QString &filePath);


public:
    static LoadBrainAtlases *i(){static LoadBrainAtlases m;return &m;}
    float m_voxelSize;cv::Point3i m_size;
    QStandardItemModel *m_pModel;QStringList m_modelNames;
    QLineEdit *m_searchEditor;QString hoverRegionName;
    QMap<int,RegionTree*> m_id2RegionTree,m_color2RegionTree;
    int m_isAtlasShow=0;int m_modelIndex=1;
    TransformParameters *m_transform3d;
    vtkSmartPointer<vtkImageActor> m_modelActors[2];
    vtkSmartPointer<vtkImageData> modelData;

    void transform3d();
    void selectRegion(int x, int y);
    void hoverRegion(int x, int y);
    void selectMultiRegion(int x, int y);
    bool selectRegionByColor(int color);
    bool selectMultiRegionByColor(int color);
    void getModelSize(cv::Point3i &dimension,cv::Point3i &voxelSize);
    void getModelByIndex(int index,int type=2);
    void searchRegion();

signals:
    void modelSelected();int noItemSelect();
    void regionSelected1(int);void regionSelected2(int);

public slots:
    void loadItems(int);
    bool loadSelectedModel(int);
    bool updateModel(int);
    void selectRow(QModelIndex);
    void selectRows(QModelIndex);
    void removeModelImage();
    void onMergeCellCounting(QString);

};


#endif // LOADBRAINATLASES_H


