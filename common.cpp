#include "common.h"
#include "interactorstyle.h"
#include "loadbrainatlases.h"
#include "loadsliceimages.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <vtkCamera.h>

Common::Common():m_imageViewer(new QVTKOpenGLWidget),m_nextWarpType(-1),p_params3d(new TransformParameters),p_params2d(nullptr),enableMouseEvent(false){
    m_renderer=vtkSmartPointer<vtkRenderer>::New();
    m_imageViewer->GetRenderWindow()->AddRenderer(m_renderer);
    vtkSmartPointer<ImageInteractorStyle> interactor=vtkSmartPointer<ImageInteractorStyle>::New();
    m_imageViewer->GetInteractor()->SetInteractorStyle(interactor);
    m_configFolderPath=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/VISoR/freesia/";
    QDir().mkpath(m_configFolderPath);m_configFilePath=m_configFolderPath+"config.json";
    loadJson(m_configFilePath,m_configParams);
}

void Common::resetViewer(){m_renderer->ResetCamera();m_imageViewer->GetRenderWindow()->Render();}

void Common::axialType(){if(isSliceWarping||isCellPicking){return;}m_modelType=2;m_maxImageNumber=LoadSliceImages::i()->m_rawImageSize.z;LoadSliceImages::i()->m_imageIndex=max(1,m_maxImageNumber/2);
                         m_maxModelNumber=LoadBrainAtlases::i()->m_size.z;int index=m_conboBoxIndex==0?m_maxModelNumber:m_maxImageNumber;emit atlasNumberChanged(index);}

void Common::coronalType(){if(isSliceWarping||isCellPicking){return;} m_modelType=1;m_maxImageNumber=LoadSliceImages::i()->m_rawImageSize.y;LoadSliceImages::i()->m_imageIndex=max(1,m_maxImageNumber/2);
                           m_maxModelNumber=LoadBrainAtlases::i()->m_size.y;int index=m_conboBoxIndex==0?m_maxModelNumber:m_maxImageNumber;emit atlasNumberChanged(index);}

void Common::sagittalType(){if(isSliceWarping||isCellPicking){return;}m_modelType=0;m_maxImageNumber=LoadSliceImages::i()->m_rawImageSize.x;LoadSliceImages::i()->m_imageIndex=max(1,m_maxImageNumber/2);
                            m_maxModelNumber=LoadBrainAtlases::i()->m_size.x;int index=m_conboBoxIndex==0?m_maxModelNumber:m_maxImageNumber;emit atlasNumberChanged(index);}

void Common::updateImage(){LoadSliceImages *s=LoadSliceImages::i();BuildWarpingField *w=BuildWarpingField::i();PickCellPoints *p=PickCellPoints::i();
                           m_renderer->RemoveActor(s->m_imageActors[0]);if(nullptr!=s->m_imageActors[1]){m_renderer->AddActor(s->m_imageActors[1]);}
                            s->m_imageActors[0]=s->m_imageActors[1];s->m_imageActors[1]=nullptr;

                           m_renderer->RemoveActor(s->m_cellActors[0]);if(nullptr!=s->m_cellActors[1]){m_renderer->AddActor(s->m_cellActors[1]);}
                            s->m_cellActors[0]=s->m_cellActors[1];s->m_cellActors[1]=nullptr;

                            if(isSliceWarping){QList<Node*> *nodes=nullptr;
                                nodes=w->m_allNodes.value(s->m_groupIndex,nullptr);
                                if(nullptr==nodes){nodes=new QList<Node*>();w->m_allNodes[s->m_groupIndex]=nodes;}
                                w->updateNodesPoisition(nodes);}

                            if(isCellPicking){QList<Point*> *points=nullptr;
                                points=&s->m_currentImage->spots;
                                if(nullptr==points){points=new QList<Point*>();s->m_currentImage->spots=*points;}
                                p->updatePointsPoisition(points);}

                            m_imageViewer->GetRenderWindow()->Render();
                          }

void Common::onSliceWarping(bool isWarping){isSliceWarping=isWarping;if(!isWarping){BuildWarpingField::i()->hideNodeMarkers();m_nextWarpType=1;}
    else{LoadSliceImages::i()->m_isTransformed=true;}}

void Common::onCellLabel(bool isPicking){isCellPicking=isPicking;if(!isPicking){PickCellPoints::i()->hidePointMarkers();}
    LoadSliceImages::i()->m_isTransformed=true;}

void Common::removeAllWarping(){BuildWarpingField::i()->m_allNodes.clear();BuildWarpingField::i()->hideNodeMarkers();m_nextWarpType=1;}

void Common::removeAllCell(){LoadSliceImages *s=LoadSliceImages::i();if(!s->m_bEdited){return;}
                            PickCellPoints::i()->hidePointMarkers();m_renderer->RemoveActor(LoadSliceImages::i()->m_cellActors[0]);
                            foreach(Image *image,s->m_images){image->spots.clear();
                            image->cell=cv::Mat::zeros(s->m_rawImageSize.y,s->m_rawImageSize.x,CV_16UC1);}
                            s->m_isTransformed=true;}

bool Common::loadJson(const QString &filePath,QVariantMap &params){
    QFile file(filePath);if(!file.exists()||!file.open(QIODevice::ReadOnly)){return false;}
    QByteArray saveData = file.readAll();file.close();
    QJsonDocument loadDoc(QJsonDocument::fromJson(saveData));
    params=loadDoc.object().toVariantMap();return !params.empty();
}
bool Common::saveJson(const QString &filePath,const QVariantMap& params){
    QFile file(filePath);if(!file.open(QIODevice::WriteOnly)){return false;}
    QJsonDocument saveDoc(QJsonObject::fromVariantMap(params));
    file.write(saveDoc.toJson());file.close();return true;
}

QString Common::readConfig(const QString &key){
    return m_configParams.contains(key)?m_configParams[key].toString():"";
}

bool Common::modifyConfig(const QString &key, const QString &value){
    m_configParams[key]=value;return saveJson(m_configFilePath,m_configParams);
}

void Common::getPointPosition(int x, int y,double picked1[3]){
    m_renderer->SetDisplayPoint(x,y,0);m_renderer->DisplayToWorld();double *picked=m_renderer->GetWorldPoint();
    double cameraPos[3];m_renderer->GetActiveCamera()->GetPosition(cameraPos);
    double f=cameraPos[2]/(cameraPos[2]-picked[2]);
    picked1[2]={0};for(int i=0;i<2;i++){picked1[i]=cameraPos[i]+(picked[i]-cameraPos[i])*f;}
}
