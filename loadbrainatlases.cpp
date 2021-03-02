#include "common.h"
#include "loadbrainatlases.h"
#include <QApplication>
#include <QStandardItemModel>
#include <QDebug>
#include <vtkImageImport.h>
#include <vtkLookupTable.h>
#include <vtkImageMapToColors.h>
#include <QFileDialog>
#include <QFileInfo>
#include <QDirIterator>

LoadBrainAtlases::LoadBrainAtlases():m_modelFolderPath(QApplication::applicationDirPath()+"/data/"),m_voxels(nullptr),m_transform3d(new TransformParameters){
    m_pModel=new QStandardItemModel;loadFreesiaJson();
}

void LoadBrainAtlases::loadFreesiaJson(){
    Common *c=Common::i();
    QVariantMap info;
    if(!Common::loadJson(m_modelFolderPath+"freesia-atlas.json",info)){emit c->showMessage("Unable to load brain model information");return;}
    foreach(QVariant v,info["atlas"].toList()){
        QVariantMap v1=v.toMap();ModelInfo info;info.name=v1["name"].toString();
        info.fullName=v1["full_name"].toString();info.voxelSize=v1["voxel_size"].toFloat();
        info.imagePath=m_modelFolderPath+v1["annotation_path"].toString();
        info.regionPath=m_modelFolderPath+v1["structures_path"].toString();
        QStringList v1s=v1["image_dimension"].toString().split(" ");if(v1s.length()!=3){continue;}
        info.dimension=cv::Point3i(v1s[0].toInt(),v1s[1].toInt(),v1s[2].toInt());
        m_modelNames.append(info.name);
        m_models.append(info);
    }
}

void LoadBrainAtlases::loadItems(int model_index){
    Common *c=Common::i();if(model_index<0){emit c->showMessage("No model was selected",3000);return;}
    const QString filePath= m_models[model_index].regionPath; QVariantMap params;
    if(!Common::loadJson(filePath,params)){return;}

    qDeleteAll(m_allRegions);m_allRegions.clear();m_rootRegionsTree.clear();m_models2RegionTree.clear();m_id2RegionTree.clear();
    qDeleteAll(m_items);m_items.clear();m_pModel->removeRows(0,m_pModel->rowCount());

    QMap<int,QVariantMap> items; QMapIterator<QString,QVariant> iter(params);
    while(iter.hasNext()){
        iter.next();
        QVariantMap item=iter.value().toMap();
        int order=item["graph_order"].toInt();if(order==0){continue;}
        if(items.contains(order)){qWarning()<<"item with order"<<order<<"existed";continue;}
        items.insert(order,item);
    }

    QMapIterator<int,QVariantMap> iter1(items);
    while(iter1.hasNext()){
        iter1.next();
        QVariantMap item=iter1.value();
        int sId=item["id"].toInt(),color=item["label_color"].toInt();
        bool bOK;int parentId=item["parent_structure_id"].toInt(&bOK);if(!bOK){parentId=-1;}
        QString acronym=item["acronym"].toString();
        QString name=item["name"].toString();
        QString colorHex=item["color_hex_triplet"].toString();
        RegionTree *node=new RegionTree(sId,parentId,color,acronym,name,colorHex);
        m_allRegions.append(node);
        m_id2RegionTree.insert(sId,node);
        m_color2RegionTree.insert(color,node);
    }
    QListIterator<RegionTree*> iter2(m_allRegions);
    while(iter2.hasNext()){
        RegionTree *node=iter2.next();
        int parentId=node->parentId;
        if(!m_id2RegionTree.contains(parentId)){m_rootRegionsTree.append(node);continue;}
        RegionTree *parentNode=m_id2RegionTree[parentId];
        parentNode->subRegions.append(node);
    }

    QListIterator<RegionTree*> iter3(m_rootRegionsTree);
    while(iter3.hasNext()){getItem(iter3.next(),nullptr);}
    emit modelSelected();
    loadSelectedModel(model_index);
}

bool LoadBrainAtlases::loadSelectedModel(int model_index){
    Common *c=Common::i();if(model_index<0){emit c->showMessage("No model was selected",3000);return false;}
    emit c->showMessage("Loading brain atlas ...");
    ModelInfo modelInfo=m_models[model_index];
    loadRegions(modelInfo.regionPath);
    FILE *fp=fopen(modelInfo.imagePath.toStdString().c_str(),"rb");
    if(nullptr==fp){emit c->showMessage("Unable to load model "+modelInfo.name);return false;}
    m_size=modelInfo.dimension;m_voxelSize=modelInfo.voxelSize;
    c->m_voxelSize=m_voxelSize;c->m_maxModelNumber=m_size.z;
    free(m_voxels);size_t num=m_size.x*m_size.y*m_size.z,length=num*2;
    m_voxels=(uint16_t*)malloc(length);fread((char*)m_voxels,length,1,fp);fclose(fp);

    int i=0,count=0;
    for(uint16_t *v=m_voxels,*vEnd=v+num;v!=vEnd;v++,i++){
        uint16_t color=*v;if(color==0){continue;}
        RegionNode *ptr=m_color2Regions.value(color,nullptr);
        if(nullptr!=ptr){ptr->voxelIndexes.append(i);count++;}
    }

    foreach(RegionNode *node,m_color2Regions){
        int n=node->voxelIndexes.length();QStringList pathNames;
        for(RegionNode *p=node;nullptr!=p;p=p->parentNode){p->totalVoxelNumber+=n;pathNames.prepend(p->acronym);}
        node->path=pathNames.join("/");
    }
    setBuffer();emit c->atlasNumberChanged(m_size.z);emit c->showMessage("Done");
    return true;
}

bool LoadBrainAtlases::updateModel(int index){
    Common *c=Common::i();
    if(index>c->m_maxModelNumber){index=c->m_maxModelNumber-3;}
    m_modelIndex = index;
    getModelByIndex(index, Common::i()->m_modelType);
    if(nullptr!=modelData){
        int pSize[3]={0};modelData->GetDimensions(pSize);int w=pSize[0],h=pSize[1];
        uint16_t *pData=(uint16_t*)modelData->GetScalarPointer();
        if(w>0&&h>0){//int w=m.cols,h=m.rows;
            double *origin=modelData->GetOrigin(),*spacing=modelData->GetSpacing();
            cv::Mat m(cv::Size(pSize[0],pSize[1]),CV_16UC1,pData);updateModelImage(m,origin,spacing);
        }
    }
    if(nullptr!=selectModelData){
        int pSize[3]={0};selectModelData->GetDimensions(pSize);
        char *pData=(char*)selectModelData->GetScalarPointer();cv::Mat m(cv::Size(pSize[0],pSize[1]),CV_16UC1,pData);
        double *origin=selectModelData->GetOrigin(),*spacing=selectModelData->GetSpacing();
        updateSelectImage(m,origin,spacing);
    }
    if(nullptr!=selectMultiModelData){
        int pSize[3]={0};selectMultiModelData->GetDimensions(pSize);
        char *pData=(char*)selectMultiModelData->GetScalarPointer();cv::Mat m(cv::Size(pSize[0],pSize[1]),CV_16UC1,pData);
        double *origin=selectMultiModelData->GetOrigin(),*spacing=selectMultiModelData->GetSpacing();
        updateSelectMultiImage(m,origin,spacing);
    }
    return true;
}

void LoadBrainAtlases::selectRegion(int x, int y){
    vtkSmartPointer<vtkImageActor> act=m_modelActors[0];if(nullptr==act){return;}
    double *pos=act->GetPosition(),*spacing=act->GetInput()->GetSpacing();
    double picked1[3];Common::i()->getPointPosition(x,y,picked1);
    int x1=int(round((picked1[0]-pos[0])/m_modelFactor/spacing[0])),y1=int(round((picked1[1]-pos[1])/m_modelFactor/spacing[1])),color=0;
    if(x1>=0&&y1>=0&&x1<m_modelImage.cols&&y1<m_modelImage.rows){color=m_modelImage.at<uint16_t>(y1,x1);}
    selectRegionByColor(color);emit regionSelected1(color);
}

void LoadBrainAtlases::selectMultiRegion(int x, int y){
    vtkSmartPointer<vtkImageActor> act=m_modelActors[0];if(nullptr==act){return;}
    double *pos=act->GetPosition(),*spacing=act->GetInput()->GetSpacing();
    double picked1[3];Common::i()->getPointPosition(x,y,picked1);
    int x1=int(round((picked1[0]-pos[0])/m_modelFactor/spacing[0])),y1=int(round((picked1[1]-pos[1])/m_modelFactor/spacing[1])),color=0;
    if(x1>=0&&y1>=0&&x1<m_modelImage.cols&&y1<m_modelImage.rows){color=m_modelImage.at<uint16_t>(y1,x1);}
    selectMultiRegionByColor(color);emit regionSelected2(color);
}

void LoadBrainAtlases::hoverRegion(int x, int y){
    Common *c=Common::i();
    vtkSmartPointer<vtkImageActor> act=m_modelActors[0];if(nullptr==act){return;}
    double *pos=act->GetPosition(),*spacing=act->GetInput()->GetSpacing();
    double picked1[3];c->getPointPosition(x,y,picked1);
    int x1=int(round((picked1[0]-pos[0])/m_modelFactor/spacing[0])),y1=int(round((picked1[1]-pos[1])/m_modelFactor/spacing[1])),color=0;
    if(x1>=0&&y1>=0&&x1<m_modelImage.cols&&y1<m_modelImage.rows){color=m_modelImage.at<uint16_t>(y1,x1);}if(color<0){return;}
    cv::Point3i size,voxelSize;getModelSize(size,voxelSize);
    if(color==0){c->m_renderer->RemoveActor(m_hoverActors[0]);c->m_imageViewer->GetRenderWindow()->Render();return;}
    char *data=(0==color?nullptr:getRegionVoxelByColor(color, true));
    setBuffer2(data,size,voxelSize);if(nullptr!=data){free(data);}
    getModelByIndex(m_modelIndex, Common::i()->m_modelType);
    if(nullptr!=hoverModelData){
        int pSize[3]={0};hoverModelData->GetDimensions(pSize);
        char *pData=(char*)hoverModelData->GetScalarPointer();cv::Mat m(cv::Size(pSize[0],pSize[1]),CV_16UC1,pData);
        double *origin=hoverModelData->GetOrigin(),*spacing=hoverModelData->GetSpacing();
        updateHoverImage(m,origin,spacing);}
}

bool LoadBrainAtlases::selectRegionByColor(int color){
    if(color<0){return false;}Common *c=Common::i();
    c->m_renderer->RemoveActor(m_selectMultiActors[0]);c->m_imageViewer->GetRenderWindow()->Render();
    m_colorStack.clear();m_resliceMultiSelect=nullptr;selectMultiModelData=nullptr; emit noItemSelect();
    if(color==0){c->m_renderer->RemoveActor(m_selectActors[0]);c->m_imageViewer->GetRenderWindow()->Render();
        m_resliceSelect=nullptr;selectModelData=nullptr; emit noItemSelect();return false;}
    cv::Point3i size,voxelSize;getModelSize(size,voxelSize);
    char *data=(0==color?nullptr:getRegionVoxelByColor(color, false));
    setBuffer1(data,size,voxelSize);if(nullptr!=data){free(data);}
    updateModel(m_modelIndex);
    return true;
}

bool LoadBrainAtlases::selectMultiRegionByColor(int color){
    if(color<=0){return false;}Common *c=Common::i();
    c->m_renderer->RemoveActor(m_selectActors[0]);c->m_imageViewer->GetRenderWindow()->Render();
    m_resliceSelect=nullptr;selectModelData=nullptr; emit noItemSelect();   
    if(m_colorStack.contains(color)){m_colorStack.removeOne(color);}
    else{m_colorStack.append(color);}
    if(m_colorStack.isEmpty()){
        c->m_renderer->RemoveActor(m_selectMultiActors[0]);c->m_imageViewer->GetRenderWindow()->Render();
        m_resliceMultiSelect=nullptr;selectMultiModelData=nullptr; emit noItemSelect();return false;
    }
    char *data=(0==color?nullptr:getMultiRegionVoxelByColor());
    cv::Point3i size,voxelSize;getModelSize(size,voxelSize);
    setBuffer3(data,size,voxelSize);if(nullptr!=data){free(data);}
    updateModel(m_modelIndex);
    return true;
}


void LoadBrainAtlases::getModelByIndex(int index,int type){
    vtkSmartPointer<vtkMatrix4x4> resliceAxes1=vtkSmartPointer<vtkMatrix4x4>::New();
    resliceAxes1->Identity();

    if(0==type){
        InitializeSagittalMatrix(resliceAxes1);
    }else if(1==type){
        InitializeCoronalMatrix(resliceAxes1);
    }else{
        InitializeAxialMatrix(resliceAxes1);
    }

    m_volumeSize=cv::Point3f(m_size.x*m_voxelSize,m_size.y*m_voxelSize,m_size.z*m_voxelSize);
    vtkSmartPointer<vtkMatrix4x4> resliceAxes=vtkSmartPointer<vtkMatrix4x4>::New();
    resliceAxes->Multiply4x4(getTransformMatrix(m_transform3d,m_volumeSize),resliceAxes1,resliceAxes);//tranform->GetMatrix()
    double modelSpacing[]={m_voxelSize*m_transform3d->scale[0],m_voxelSize*m_transform3d->scale[1],m_voxelSize*m_transform3d->scale[2]};
    double originOffset[]={-(m_transform3d->scale[0]-1)*m_volumeSize.x/2,-(m_transform3d->scale[1]-1)*m_volumeSize.y/2,-(m_transform3d->scale[2]-1)*m_volumeSize.z/2};
    m_modelImageData->SetSpacing(modelSpacing);m_modelImageData->SetOrigin(originOffset);

    double position[4]={0,0,index*m_voxelSize,1};double *position1=resliceAxes->MultiplyDoublePoint(position);
    for(int i=0;i<3;i++){resliceAxes->SetElement(i,3,position1[i]);}

    if(nullptr!=m_resliceModel){
        m_resliceModel->SetResliceAxes(resliceAxes);m_resliceModel->Update();
        modelData=m_resliceModel->GetOutput();
    }

    if(nullptr!=m_resliceSelect){
        m_selectImageData->SetSpacing(modelSpacing);m_selectImageData->SetOrigin(originOffset);
        m_resliceSelect->SetResliceAxes(resliceAxes);m_resliceSelect->Update();
        selectModelData=m_resliceSelect->GetOutput();
    }

    if(nullptr!=m_resliceHover){
        m_hoverImageData->SetSpacing(modelSpacing);m_hoverImageData->SetOrigin(originOffset);
        m_resliceHover->SetResliceAxes(resliceAxes);m_resliceHover->Update();
        hoverModelData=m_resliceHover->GetOutput();
    }

    if(nullptr!=m_resliceMultiSelect){
        m_selectMultiImageData->SetSpacing(modelSpacing);m_selectMultiImageData->SetOrigin(originOffset);
        m_resliceMultiSelect->SetResliceAxes(resliceAxes);m_resliceMultiSelect->Update();
        selectMultiModelData=m_resliceMultiSelect->GetOutput();
    }
}

void LoadBrainAtlases::updateModelImage(const cv::Mat &image,double *origin,double *spacing){
    int w=image.cols,h=image.rows;if(image.empty()){return;}
    cv::Mat m=cv::Mat::zeros(cv::Size(2*w,2*h),CV_8UC1);
    double spacing1[3]={spacing[0]/m_modelFactor,spacing[1]/m_modelFactor,spacing[2]/m_modelFactor};

    uint16_t *pData2=(uint16_t*)image.data;uint8_t *pData3=m.data;int w1=m.cols;
    for(int x=0,y=0;;){
        uint8_t *pData4=pData3+2*y*w1+2*x;
        uint16_t v1=*pData2,v2=*(pData2+1),v3=*(pData2+w+1),v4=*(pData2+w);

        bool bOK1=(v1==v2),bOK2=(v2==v3),bOK3=(v3==v4),bOK4=(v4==v1);
        if(!bOK1){*(pData4+1)=255;}
        if(!bOK2){*(pData4+w1+2)=255;}
        if(!bOK3){*(pData4+w1*2+1)=255;}
        if(!bOK4){*(pData4+w1)=255;}
        if((bOK1||bOK2||bOK3||bOK4)&&(bOK1!=bOK2&&bOK2!=bOK3&&bOK3!=bOK4&&bOK4!=bOK1)){*(pData4+w1+1)=255;}

        x++;pData2++;if(x==w-1){x=0;y++;pData2+=1;if(y==h-1){break;}}
    }

    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();
    importer->SetDataScalarTypeToUnsignedChar();importer->SetWholeExtent(0,m.cols-1,0,m.rows-1,0,0);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(m.data);
    importer->Update();

    vtkSmartPointer<vtkImageData> imageData=vtkSmartPointer<vtkImageData>::New();imageData->DeepCopy(importer->GetOutput());
    imageData->SetSpacing(spacing1);

    vtkSmartPointer<vtkLookupTable> colorTable=vtkSmartPointer<vtkLookupTable>::New();
    colorTable->SetRange(0,255);colorTable->SetValueRange(0.0,1.0);colorTable->SetAlphaRange(0,1);
    colorTable->SetSaturationRange(0,1);colorTable->SetHueRange(0,0.3);
    colorTable->SetRampToLinear();colorTable->Build();
    colorTable->SetTableValue(255,1.0,1.0,1.0,1.0);
    colorTable->SetTableValue(0,0.0,0.0,0.0,0.0);
    vtkSmartPointer<vtkImageMapToColors> colorMap=vtkSmartPointer<vtkImageMapToColors>::New();
    colorMap->SetLookupTable(colorTable);colorMap->SetInputData(imageData);colorMap->Update();

    vtkSmartPointer<vtkImageActor> imgActor=vtkSmartPointer<vtkImageActor>::New();
    imgActor->SetInputData(colorMap->GetOutput());imgActor->SetPosition(origin);

    Common *c=Common::i();m_imageActorMutex.lock();
    m_modelActors[1]=imgActor;m_nextModelImage=image.clone();
    c->m_renderer->RemoveActor(m_modelActors[0]);
    if(nullptr!=m_modelActors[1]){c->m_renderer->AddActor(m_modelActors[1]);}
    m_modelActors[0]=m_modelActors[1];m_modelActors[1]=nullptr;
    m_modelImage=m_nextModelImage;
    m_imageActorMutex.unlock();
    if(m_isAtlasShow){m_modelActors[0]->VisibilityOff();}
    c->m_imageViewer->GetRenderWindow()->Render();
}

void LoadBrainAtlases::updateSelectImage(const cv::Mat &image,double *origin,double *spacing){
    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();
    importer->SetDataScalarTypeToUnsignedChar();importer->SetWholeExtent(0,image.cols-1,0,image.rows-1,0,0);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(image.data);
    importer->Update();

    vtkSmartPointer<vtkImageData> imageData=vtkSmartPointer<vtkImageData>::New();imageData->DeepCopy(importer->GetOutput());
    imageData->SetSpacing(spacing);

    vtkSmartPointer<vtkLookupTable> colorTable=vtkSmartPointer<vtkLookupTable>::New();
    colorTable->SetRange(0,255);colorTable->SetValueRange(0.0,1.0);colorTable->SetAlphaRange(0,1);
    colorTable->SetSaturationRange(0,1);colorTable->SetHueRange(0,1);
    colorTable->SetRampToLinear();colorTable->Build();

    bool ok;int R=color_hex_select.mid(0,2).toInt(&ok,16),G=color_hex_select.mid(2,2).toInt(&ok,16),B=color_hex_select.mid(4,2).toInt(&ok,16);
    colorTable->SetTableValue(255,R,G,B,1.0);
    vtkSmartPointer<vtkImageMapToColors> colorMap=vtkSmartPointer<vtkImageMapToColors>::New();
    colorMap->SetLookupTable(colorTable);colorMap->SetInputData(imageData);colorMap->Update();

    vtkSmartPointer<vtkImageActor> imgActor=vtkSmartPointer<vtkImageActor>::New();
    imgActor->SetInputData(colorMap->GetOutput());imgActor->SetPosition(origin);

    Common *c=Common::i();m_imageActorMutex.lock();
    m_selectActors[1]=imgActor;c->m_renderer->RemoveActor(m_selectActors[0]);
    if(nullptr!=m_selectActors[1]){c->m_renderer->AddActor(m_selectActors[1]);}
    m_selectActors[0]=m_selectActors[1];m_selectActors[1]=nullptr;
    m_imageActorMutex.unlock();
    c->m_imageViewer->GetRenderWindow()->Render();
}

void LoadBrainAtlases::updateSelectMultiImage(const cv::Mat &image,double *origin,double *spacing){
    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();
    importer->SetDataScalarTypeToUnsignedChar();importer->SetWholeExtent(0,image.cols-1,0,image.rows-1,0,0);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(image.data);
    importer->Update();

    vtkSmartPointer<vtkImageData> imageData=vtkSmartPointer<vtkImageData>::New();imageData->DeepCopy(importer->GetOutput());
    imageData->SetSpacing(spacing);

    vtkSmartPointer<vtkLookupTable> colorTable=vtkSmartPointer<vtkLookupTable>::New();
    colorTable->SetRange(0,255);colorTable->SetValueRange(0.0,1.0);colorTable->SetAlphaRange(0,1);
    colorTable->SetSaturationRange(0,1);colorTable->SetHueRange(0,1);
    colorTable->SetRampToLinear();colorTable->Build();
    colorTable->SetTableValue(255,1.0,1.0,1.0,1.0);
//    colorTable->SetNumberOfColors(1500);
//    foreach(int color, m_color2ColorHex.keys()){
//        QString color_Hex = m_color2ColorHex[color];
//        bool ok;int R=color_Hex.mid(0,2).toInt(&ok,16),G=color_Hex.mid(2,2).toInt(&ok,16),B=color_Hex.mid(4,2).toInt(&ok,16);
//        colorTable->SetTableValue(color,R,G,B,1.0);
//    }colorTable->Build();

    vtkSmartPointer<vtkImageMapToColors> colorMap=vtkSmartPointer<vtkImageMapToColors>::New();
    colorMap->SetLookupTable(colorTable);colorMap->SetInputData(imageData);colorMap->Update();

    vtkSmartPointer<vtkImageActor> imgActor=vtkSmartPointer<vtkImageActor>::New();
    imgActor->SetInputData(colorMap->GetOutput());imgActor->SetPosition(origin);

    Common *c=Common::i();m_imageActorMutex.lock();
    m_selectMultiActors[1]=imgActor;c->m_renderer->RemoveActor(m_selectMultiActors[0]);
    if(nullptr!=m_selectMultiActors[1]){c->m_renderer->AddActor(m_selectMultiActors[1]);}
    m_selectMultiActors[0]=m_selectMultiActors[1];m_selectMultiActors[1]=nullptr;
    m_imageActorMutex.unlock();
    c->m_imageViewer->GetRenderWindow()->Render();
}

void LoadBrainAtlases::updateHoverImage(const cv::Mat &image,double *origin,double *spacing){
    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();
    importer->SetDataScalarTypeToUnsignedChar();importer->SetWholeExtent(0,image.cols-1,0,image.rows-1,0,0);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(image.data);
    importer->Update();

    vtkSmartPointer<vtkImageData> imageData=vtkSmartPointer<vtkImageData>::New();imageData->DeepCopy(importer->GetOutput());
    imageData->SetSpacing(spacing);

    vtkSmartPointer<vtkLookupTable> colorTable=vtkSmartPointer<vtkLookupTable>::New();
    colorTable->SetRange(0,255);colorTable->SetValueRange(0.0,1.0);colorTable->SetAlphaRange(0,1);
    colorTable->SetSaturationRange(0,1);colorTable->SetHueRange(0,1);
    colorTable->SetRampToLinear();colorTable->Build();
    bool ok;int R=color_hex_hover.mid(0,2).toInt(&ok,16),G=color_hex_hover.mid(2,2).toInt(&ok,16),B=color_hex_hover.mid(4,2).toInt(&ok,16);
    colorTable->SetTableValue(255,R,G,B,1.0);
    vtkSmartPointer<vtkImageMapToColors> colorMap=vtkSmartPointer<vtkImageMapToColors>::New();
    colorMap->SetLookupTable(colorTable);colorMap->SetInputData(imageData);colorMap->Update();

    vtkSmartPointer<vtkImageActor> imgActor=vtkSmartPointer<vtkImageActor>::New();
    imgActor->SetInputData(colorMap->GetOutput());imgActor->SetPosition(origin);

    Common *c=Common::i();m_imageActorMutex.lock();
    m_hoverActors[1]=imgActor;c->m_renderer->RemoveActor(m_hoverActors[0]);
    if(nullptr!=m_hoverActors[1]){c->m_renderer->AddActor(m_hoverActors[1]);}
    m_hoverActors[0]=m_hoverActors[1];m_hoverActors[1]=nullptr;
    m_imageActorMutex.unlock();
    c->m_imageViewer->GetRenderWindow()->Render();
}

void LoadBrainAtlases::removeModelImage(){
    Common *c=Common::i();
    if(!m_isAtlasShow){m_modelActors[0]->VisibilityOff();c->m_imageViewer->GetRenderWindow()->Render();m_isAtlasShow=1;}
    else{m_modelActors[0]->VisibilityOn();c->m_imageViewer->GetRenderWindow()->Render();m_isAtlasShow=0;}
}

void LoadBrainAtlases::setBuffer(){
    char *buffer=(char*)m_voxels;
    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();
    importer->SetDataScalarTypeToUnsignedShort();importer->SetWholeExtent(0,m_size.x-1,0,m_size.y-1,0,m_size.z-1);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(buffer);
    importer->Update();

    m_modelImageData=vtkSmartPointer<vtkImageData>::New();m_modelImageData->DeepCopy(importer->GetOutput());
    m_modelImageData->SetSpacing(m_voxelSize,m_voxelSize,m_voxelSize);

    m_resliceModel=vtkSmartPointer<vtkImageReslice>::New();m_resliceModel->SetInputData(m_modelImageData);
    m_resliceModel->SetOutputDimensionality(2);m_resliceModel->SetInterpolationModeToNearestNeighbor();m_resliceModel->SetAutoCropOutput(true);
}

void LoadBrainAtlases::setBuffer1(char *buffer,const cv::Point3i &volumeSize,const cv::Point3d &voxelSize){
    if(nullptr==buffer){m_resliceSelect=nullptr;return;}
    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();//qDebug()<<imageSize<<imageNumber;
    importer->SetDataScalarTypeToUnsignedChar();importer->SetWholeExtent(0,volumeSize.x-1,0,volumeSize.y-1,0,volumeSize.z-1);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(buffer);
    importer->Update();

    m_selectImageData=vtkSmartPointer<vtkImageData>::New();m_selectImageData->DeepCopy(importer->GetOutput());
    m_selectImageData->SetSpacing(voxelSize.x,voxelSize.y,voxelSize.z);

    m_resliceSelect=vtkSmartPointer<vtkImageReslice>::New();m_resliceSelect->SetInputData(m_selectImageData);
    m_resliceSelect->SetOutputDimensionality(2);m_resliceSelect->SetInterpolationModeToNearestNeighbor();m_resliceSelect->SetAutoCropOutput(true);
}

void LoadBrainAtlases::setBuffer2(char *buffer,const cv::Point3i &volumeSize,const cv::Point3d &voxelSize){
    if(nullptr==buffer){m_resliceSelect=nullptr;return;}
    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();//qDebug()<<imageSize<<imageNumber;
    importer->SetDataScalarTypeToUnsignedChar();importer->SetWholeExtent(0,volumeSize.x-1,0,volumeSize.y-1,0,volumeSize.z-1);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(buffer);
    importer->Update();

    m_hoverImageData=vtkSmartPointer<vtkImageData>::New();m_hoverImageData->DeepCopy(importer->GetOutput());
    m_hoverImageData->SetSpacing(voxelSize.x,voxelSize.y,voxelSize.z);

    m_resliceHover=vtkSmartPointer<vtkImageReslice>::New();m_resliceHover->SetInputData(m_hoverImageData);
    m_resliceHover->SetOutputDimensionality(2);m_resliceHover->SetInterpolationModeToNearestNeighbor();m_resliceHover->SetAutoCropOutput(true);
}

void LoadBrainAtlases::setBuffer3(char *buffer,const cv::Point3i &volumeSize,const cv::Point3d &voxelSize){
    if(nullptr==buffer){m_resliceMultiSelect=nullptr;return;}
    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();//qDebug()<<imageSize<<imageNumber;
    importer->SetDataScalarTypeToUnsignedChar();importer->SetWholeExtent(0,volumeSize.x-1,0,volumeSize.y-1,0,volumeSize.z-1);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(buffer);
    importer->Update();

    m_selectMultiImageData=vtkSmartPointer<vtkImageData>::New();m_selectMultiImageData->DeepCopy(importer->GetOutput());
    m_selectMultiImageData->SetSpacing(voxelSize.x,voxelSize.y,voxelSize.z);

    m_resliceMultiSelect=vtkSmartPointer<vtkImageReslice>::New();m_resliceMultiSelect->SetInputData(m_selectMultiImageData);
    m_resliceMultiSelect->SetOutputDimensionality(2);m_resliceMultiSelect->SetInterpolationModeToNearestNeighbor();m_resliceMultiSelect->SetAutoCropOutput(true);
}

QStandardItem *LoadBrainAtlases::getItem(const QString &text,const QString &tooltipText){
    QStandardItem *item=new QStandardItem(text);
    item->setToolTip(tooltipText);
    item->setEditable(false);
    item->setTextAlignment(Qt::AlignVCenter|Qt::AlignLeft);
    return item;
}

QStandardItem *LoadBrainAtlases::getItem(RegionTree *node, QStandardItem *parent){
    QStandardItem *pItem=getItem(node->acronym+" ("+node->name+")",node->name);
    if(nullptr==parent){m_pModel->appendRow(pItem);}else{parent->appendRow(pItem);}
    node->modelIndex=pItem->index();
    QListIterator<RegionTree*> iter(node->subRegions);
    while(iter.hasNext()){getItem(iter.next(),pItem);}
    m_models2RegionTree.insert(pItem,node);
    return pItem;
}

bool LoadBrainAtlases::loadRegions(const QString &filePath){
    QVariantMap params;
    if(!Common::loadJson(filePath,params)){return false;}
    qDeleteAll(m_rootRegions);m_rootRegions.clear();m_color2Regions.clear();m_id2Regions.clear();

    QMapIterator<QString,QVariant> iter(params);
    while(iter.hasNext()){
        iter.next();QVariantMap item=iter.value().toMap();//if(item["graph_order"].toInt()==0){continue;}
        int sId=item["id"].toInt();
        QString acronym=item["acronym"].toString();
        QString name=item["name"].toString();
        QString colorHex=item["color_hex_triplet"].toString();
        bool bOK;int parentId=item["parent_structure_id"].toInt(&bOK);if(!bOK){parentId=-1;}
        int color=item["label_color"].toInt();if(color<=0){continue;}
        RegionNode *node=new RegionNode(sId,parentId,acronym,name,color,colorHex);
        m_color2Regions[color]=node;m_id2Regions[sId]=node;
    }
    foreach(RegionNode *node,m_color2Regions){
        node->parentNode=m_id2Regions.value(node->parentId,nullptr);
        if(nullptr!=node->parentNode){node->parentNode->subRegions.append(node);}
        else{m_rootRegions.append(node);}
    }
    return true;
}

void LoadBrainAtlases::transform3d(){
    TransformParameters *ptr=Common::i()->p_params3d.exchange(nullptr);if(nullptr==ptr){return;}
    m_transform3d->copy(*ptr);delete ptr;
    updateModel(m_modelIndex);
}

void LoadBrainAtlases::getModelSize(cv::Point3i &dimension, cv::Point3i &voxelSize){
    dimension=m_size;voxelSize=cv::Point3i(int(m_voxelSize),int(m_voxelSize),int(m_voxelSize));
}

void LoadBrainAtlases::getRegionSubColors(RegionNode *node,QList<int> &colors){
    colors.append(node->color);
//    m_color2ColorHex[node->color]=node->colorHex;
    foreach(RegionNode *p,node->subRegions){getRegionSubColors(p,colors);}
}

char *LoadBrainAtlases::getRegionVoxelByColor(int color, bool isHover){
    RegionNode *node=m_color2Regions.value(color,nullptr);if(nullptr==node){return nullptr;}
    color_hex_hover=node->colorHex;hoverRegionName=node->name;
    if(!isHover){color_hex_select=node->colorHex;}
    QList<int> colors;getRegionSubColors(node,colors);
    int length=m_size.x*m_size.y*m_size.z;char *data=(char*)malloc(length);memset(data,0,length);
    foreach(int color,colors){foreach(int index,m_color2Regions[color]->voxelIndexes){*(data+index)=255;}}
    return data;
}

char *LoadBrainAtlases::getMultiRegionVoxelByColor(){
    QList<int> colorsAll;
    foreach(int color,m_colorStack){
        RegionNode *node=m_color2Regions.value(color,nullptr);if(nullptr==node){return nullptr;}
        hoverRegionName=node->name;
        QList<int> colors;getRegionSubColors(node,colors);
        colorsAll.append(colors);
    }
    int length=m_size.x*m_size.y*m_size.z;char *data=(char*)malloc(length);memset(data,0,length);
    foreach(int color,colorsAll){foreach(int index,m_color2Regions[color]->voxelIndexes){*(data+index)=255;}}
    return data;
}

void LoadBrainAtlases::selectRow(QModelIndex index){
    void *v=(void*)m_pModel->itemFromIndex(index);
    RegionTree *node=m_models2RegionTree.value(v,nullptr);
    if(nullptr!=node){selectRegionByColor(node->color);}
}
void LoadBrainAtlases::selectRows(QModelIndex index){
    void *v=(void*)m_pModel->itemFromIndex(index);
    RegionTree *node=m_models2RegionTree.value(v,nullptr);
    if(nullptr!=node){selectMultiRegionByColor(node->color);}
}

void LoadBrainAtlases::onMergeCellCounting(QString path){
    Common *c=Common::i();
    size_t colorCounts[65536]={0};emit c->showMessage("Combining all the cell-counting results");
    QDirIterator it(path,QStringList()<<"*.json",QDir::Files,QDirIterator::Subdirectories);
    while(it.hasNext()){
        it.next();QVariantMap info;if(!c->loadJson(it.filePath(),info)){continue;}
        QVariantList regions=info["regions"].toList();if(regions.empty()){continue;}
        foreach(QVariant v,regions){
            QVariantMap v1=v.toMap();bool bOK1,bOK2;
            size_t color=v1["color"].toULongLong(&bOK1),number=v1["number"].toULongLong(&bOK2);
            if(bOK1&&bOK2&&color<=65535){colorCounts[color]+=number;}
        }
    }

    QString outputName="cell-counting.csv",outputPath=path+"/"+outputName;
    if(LoadBrainAtlases::i()->dumpCellCounting(colorCounts,outputPath)){
        emit c->showMessage("Result has been saved at "+outputName);}
    else{emit c->showMessage("Failed");}c->enableMouseEvent=false;
}

bool LoadBrainAtlases::dumpCellCounting(size_t colorCounts[], const QString &filePath){
    QFile file(filePath);if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){return false;}

    foreach(RegionNode *node,m_color2Regions){
        size_t number=colorCounts[node->color];if(number==0){continue;}
        while(nullptr!=node->parentNode){
            colorCounts[node->parentNode->color]+=number;node=node->parentNode;
        }
    }
    double voxelSize3d=pow(m_voxelSize,3);

    QTextStream out(&file);out<<"id,acronym,count,density,volume,path,name\n";
    foreach(RegionNode *node,m_color2Regions){
        size_t n=colorCounts[node->color];int n1=node->totalVoxelNumber;if(n1<=0){continue;}
        double volume=n1*voxelSize3d,density=n/volume;
        out<<node->id<<","<<node->acronym<<","<<colorCounts[node->color]<<","<<density<<","<<volume<<","<<node->path<<","<<node->name<<"\n";
    }

    out.flush();file.close();return true;
}
