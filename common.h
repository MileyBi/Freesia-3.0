#ifndef COMMON_H
#define COMMON_H

#include <vtkAutoInit.h> // if not using CMake to compile, necessary to use this macro
#define vtkRenderingCore_AUTOINIT 3(vtkInteractionStyle, vtkRenderingFreeType, vtkRenderingOpenGL2)
#define vtkRenderingVolume_AUTOINIT 1(vtkRenderingVolumeOpenGL2)
#define vtkRenderingContext2D_AUTOINIT 1(vtkRenderingContextOpenGL2)
#include <atomic>
#include <QVariantMap>
#include <vtkMatrix4x4.h>
#include <vtkTransform.h>
#include <opencv2/opencv.hpp>
#include <QVTKOpenGLWidget.h>
#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSphereSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>

struct TransformParameters{
    double rotation[3],offset[3],scale[3];int sliceIndex;cv::Rect roi;
    TransformParameters(){for(int i=0;i<3;i++){rotation[i]=0;offset[i]=0;scale[i]=1;}}
    TransformParameters(const TransformParameters &p){copy(p);}
    void copy(const TransformParameters &p){for(int i=0;i<3;i++){rotation[i]=p.rotation[i];offset[i]=p.offset[i];scale[i]=p.scale[i];}sliceIndex=p.sliceIndex;roi=p.roi;}
};

class Common : public QObject
{   Q_OBJECT
    QString m_configFolderPath,m_configFilePath;QVariantMap m_configParams;
    explicit Common();

public:

    vtkSmartPointer<vtkRenderer> m_renderer;QVTKOpenGLWidget *m_imageViewer; 
    int m_modelType=2;float m_voxelSize;bool isCellPicking=false;
    int m_maxImageNumber,m_maxModelNumber,m_conboBoxIndex;
    bool isSliceWarping=false;std::atomic_int m_nextWarpType;
    std::atomic<TransformParameters*> p_params3d,p_params2d;
    bool enableMouseEvent;
    static Common *i(){static Common c;return &c;}
    void resetViewer();
    static bool loadJson(const QString &filePath,QVariantMap &params);
    static bool saveJson(const QString &filePath,const QVariantMap& params);
    QString readConfig(const QString &key);
    bool modifyConfig(const QString &key,const QString &value);
    void getPointPosition(int x, int y,double picked1[3]);

    const QString p_softwareName;
signals:
    void showMessage(const QString & message, int timeout = 0);void toggleFullscreen();
    void comboBoxIndex(int);void atlasNumberChanged(int number);
    void transform2dChanged(TransformParameters*);void transform3dChanged(TransformParameters*);

public slots:
    void axialType();void coronalType();void sagittalType();void updateImage();
    void onSliceWarping(bool);void removeAllWarping();
    void onCellLabel(bool);void removeAllCell();

};

inline void rotatePts(const double p[3],const double c[3],double angle,double output[3]){
    cv::Point2d ip(p[0],p[1]),ic(c[0],c[1]);
    double x=ip.x-ic.x,y=ip.y-ic.y,rotation=angle*3.14/180;
    double x1=x*cos(rotation)-y*sin(rotation),y1=x*sin(rotation)+y*cos(rotation);
    x1+=ic.x;y1+=ic.y;output[0]=x1;output[1]=y1;output[2]=0;
}

inline vtkSmartPointer<vtkActor> getActor(vtkAlgorithmOutput *output,double color[3]){
    vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();mapper->SetInputConnection(output);
    vtkSmartPointer<vtkProperty> prop=vtkSmartPointer<vtkProperty>::New();prop->SetColor(color);
    vtkSmartPointer<vtkActor> actor=vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);actor->SetProperty(prop);return actor;
}

inline vtkSmartPointer<vtkActor> getNodeActor(double *pos,double color[3]){
    vtkSmartPointer<vtkSphereSource> sphereSource=vtkSmartPointer<vtkSphereSource>::New();
    sphereSource->SetCenter(pos);sphereSource->SetRadius(20);
    return getActor(sphereSource->GetOutputPort(),color);//s_colorSelected
}

inline void InitializeAxialMatrix(vtkMatrix4x4* orientationMatrix)
{
    if (!orientationMatrix)
    {
        return;
    }
    orientationMatrix->SetElement(0,0,-1.0);
    orientationMatrix->SetElement(1,0,0.0);
    orientationMatrix->SetElement(2,0,0.0);
    orientationMatrix->SetElement(0,1,0.0);
    orientationMatrix->SetElement(1,1,1.0);
    orientationMatrix->SetElement(2,1,0.0);
    orientationMatrix->SetElement(0,2,0.0);
    orientationMatrix->SetElement(1,2,0.0);
    orientationMatrix->SetElement(2,2,1.0);
}
//----------------------------------------------------------------------------
inline void InitializeSagittalMatrix(vtkMatrix4x4* orientationMatrix)
{
    if (!orientationMatrix)
    {
        return;
    }
    orientationMatrix->SetElement(0,0,0.0);
    orientationMatrix->SetElement(1,0,-1.0);
    orientationMatrix->SetElement(2,0,0.0);
    orientationMatrix->SetElement(0,1,0.0);
    orientationMatrix->SetElement(1,1,0.0);
    orientationMatrix->SetElement(2,1,1.0);
    orientationMatrix->SetElement(0,2,1.0);
    orientationMatrix->SetElement(1,2,0.0);
    orientationMatrix->SetElement(2,2,0.0);
}
//----------------------------------------------------------------------------
inline void InitializeCoronalMatrix(vtkMatrix4x4* orientationMatrix)
{
    if (!orientationMatrix)
    {
        return;
    }
    orientationMatrix->SetElement(0,0,-1.0);
    orientationMatrix->SetElement(1,0,0.0);
    orientationMatrix->SetElement(2,0,0.0);
    orientationMatrix->SetElement(0,1,0.0);
    orientationMatrix->SetElement(1,1,0.0);
    orientationMatrix->SetElement(2,1,1.0);
    orientationMatrix->SetElement(0,2,0.0);
    orientationMatrix->SetElement(1,2,1.0);
    orientationMatrix->SetElement(2,2,0.0);
}
//----------------------------------------------------------------------------
inline vtkSmartPointer<vtkMatrix4x4> getTransformMatrix(const TransformParameters *p,const cv::Point3f &size){
    vtkSmartPointer<vtkTransform> tranform=vtkSmartPointer<vtkTransform>::New();
    cv::Point3f c=size/2;tranform->Translate(c.x,c.y,c.z);
    tranform->RotateX(p->rotation[0]);tranform->RotateY(p->rotation[1]);//tranform->RotateZ(p->rotation[2]);
    tranform->Translate(-c.x,-c.y,-c.z);tranform->Translate(p->offset);
    vtkSmartPointer<vtkMatrix4x4> matrix=vtkSmartPointer<vtkMatrix4x4>::New();
    matrix->DeepCopy(tranform->GetMatrix());return matrix;
}

inline double calcuDistance(double x,double y,double x1,double y1,double x2,double y2) {
    double x0=(x1+x2)*0.5;double y0=(y1+y2)*0.5;double d=pow((x-x0),2)+pow((y-y0),2);return sqrt(d);
}
#endif // COMMON_H
