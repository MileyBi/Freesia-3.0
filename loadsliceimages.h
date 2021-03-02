#ifndef LOADSLICEIMAGES_H
#define LOADSLICEIMAGES_H
#include<iostream>
#include <stdlib.h>
#include <atomic>
#include <thread>
#include <QWidget>
#include <QMap>
#include <opencv2/opencv.hpp>
#include <vtkSmartPointer.h>
#include <vtkImageReslice.h>
#include <vtkVolumeProperty.h>
#include <vtkGPUVolumeRayCastMapper.h>
#include <vtkImageMapToColors.h>
#include <vtkLookupTable.h>
#include <vtkImageActor.h>
#include <QMutexLocker>
#include <vtkImageData.h>
#include <vtkImageBlend.h>
#include "pickcellpoints.h"

using namespace std;

QT_BEGIN_NAMESPACE
struct ImportParams;
struct Group;
struct Image{
    int index,groupIndex,modelIndex;QList<cv::Mat> data;QStringList filePath;QList<QSize> size;QString spotsPath;QList<Point*> spots;cv::Mat cell;cv::Rect roi;
    Image():spots(){}
};
QT_END_NAMESPACE

class LoadSliceImages : public QObject
{
    Q_OBJECT
    bool m_bRunning;std::thread *m_dataThread;int thresh;int m_grayLevel;
    QMap<int, QStringList> m_multiFilePaths;QStringList items;
    int m_lastImageType=-1;int m_lastImageIndex=-1;bool bIndexUpdated=true;
    float m_voxelSizes[3];int m_groupSize;cv::Point3d m_volumeSize;
    QString m_imageFolderPath,m_projectPath;atomic<ImportParams*> m_importParams;
    QMap<int,Group*> m_groups;cv::Mat mask;Group *m_currentGroup;
    QMap<int,vtkSmartPointer<vtkImageReslice>> m_reslice;
    vtkSmartPointer<vtkImageBlend> m_imageBlend;vtkImageData *imageData;
    int m_nextGroupIndex,m_nextImageIndex2;QMutex m_imageActorMutex;
    std::atomic<QString*>m_projectPathPtr, m_spotsPath2Import,m_projectPath2Save,m_exportPath;


//    vtkSmartPointer<vtkPiecewiseFunction> m_opacity;
//    vtkSmartPointer<vtkVolume> m_volume;

    LoadSliceImages();~LoadSliceImages();
    void mainLoop();bool importDirectory();bool loadProject();bool updateImage();void importSpots();
    void buildWarpField();void buildWarpField(int sliceIndex, cv::Mat &warpField, bool bInversed);
    bool importImages();void loadImages();void getSliceByIndex(int index,int type);
    void setBuffer(char *buffer,const cv::Point3i &volumeSize,const cv::Point3d &voxelSize, int index);
    void updateResliceImage(const cv::Mat &image,double *origin,double *spacing,double rotation,int imageIndex,int groupIndex);
    void updateCellCloud(const cv::Mat &cell,double *origin, double *spacing, double rotation);
    cv::Rect autoCrop(const cv::Mat &image);
    bool loadJsonFile(QString path);bool loadFlsmFile(QString path);bool loadVisorFile(QString path);
    void loadTarBundle(const QString &filePath,cv::Mat &dst);
    void saveProject();QVariantList exportAllMarkers();
    void exportCellCounting();

public:
    int m_imageIndex=1,m_groupIndex=-1;bool m_isTransformed;
    int m_bEdited;bool m_autoCrop, m_autoRotate;Image *m_currentImage;
    QList<Image*> m_images;cv::Point3i m_rawImageSize;
    vtkSmartPointer<vtkImageMapToColors> m_colorMap2;
    vtkSmartPointer<vtkLookupTable> m_colorTable;
    vtkSmartPointer<vtkImageActor> m_imageActors[2],m_cellActors[2];

    static LoadSliceImages *i(){static LoadSliceImages s;return &s;}
    void transform2d();void changeImageModel(int index);

signals:
    void imageUpdate();
    void imageNumberChanged(int);
    void addComboBoxItems(QStringList);
    void mergeCellCounting(QString);

public slots:
    void onImportDirectory();void onLoadProject();void onImportSpots();
    void onContrastChanged(int);void onSaveProject();void onExportCellCounting();
    void changeAutoCrop(bool isCrop){m_autoCrop=isCrop;m_isTransformed=true;}
    void changeAutoRotate(bool isRotate){m_autoRotate=isRotate;/*m_isTransformed=true;*/}
};

#endif // LOADSLICEIMAGES_H
