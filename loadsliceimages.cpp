 #include "common.h"
#include "loadsliceimages.h"
#include "loadbrainatlases.h"
#include "importexportdialog.h"
#include "buildwarpingfield.h"
#include <QElapsedTimer>
#include <QDirIterator>
#include <vtkImageImport.h>
#include <vtkImageMask.h>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <slidereader.h>
#include <flsmreader.h>

extern "C"{
#include <nn.h>
}

struct Group{
    TransformParameters param,paramSaved;cv::Mat warpField;
};

LoadSliceImages::LoadSliceImages():m_bRunning(true),thresh(100),m_grayLevel(1000),m_isTransformed(false),m_bEdited(0),m_autoCrop(true),m_autoRotate(true),m_rawImageSize(1,1,1){
    m_dataThread=new std::thread(&LoadSliceImages::mainLoop,this);
}

LoadSliceImages::~LoadSliceImages(){m_bRunning=false;m_dataThread->join();delete m_dataThread;}

void LoadSliceImages::mainLoop(){
    QElapsedTimer timer;
    while(m_bRunning){
        timer.start();importDirectory();loadProject();
        if(m_bEdited){updateImage();buildWarpField();importSpots();
                      saveProject();exportCellCounting();thresh=max(thresh-5,5);}
        int timeLeft=thresh-timer.elapsed();if(timeLeft>0){std::this_thread::sleep_for(std::chrono::milliseconds(timeLeft));}
    }
}

void LoadSliceImages::onImportDirectory(){
    ImportDialog dialog;if(dialog.exec()!=QDialog::Accepted){return;}
    ImportParams *ptr=new ImportParams;if(dialog.getParameters(*ptr)){delete m_importParams.exchange(ptr);}else{delete ptr;}
}

bool LoadSliceImages::importDirectory(){
    ImportParams *ptr=m_importParams.exchange(nullptr);if(nullptr==ptr){return false;}
    QDir dir(ptr->path);if(!dir.exists()){return false;}
    Common *c=Common::i();m_multiFilePaths.clear();items.clear();
    m_voxelSizes[0]=ptr->voxels[0];m_voxelSizes[1]=ptr->voxels[1];m_voxelSizes[2]=ptr->voxels[2];m_groupSize=ptr->groupSize;
    QStringList channel_list = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    items.append("Model");items.append("Slice");
    if(channel_list.size()>1){int count=-1;
        foreach(QString dirPath,channel_list){int i=0;
            QDirIterator it(ptr->path+'/'+dirPath,QStringList()<<"*.tif"<<"*.tiff"<<"*.jpg"<<"*.bmp"<<"*.png",QDir::Files,QDirIterator::NoIteratorFlags);
            while(it.hasNext()){it.next();m_multiFilePaths[i].append(it.filePath());i++;}
            if(count!=-1&&count!=i){c->showMessage("Different number of images!"); return false;}count=i;
            QDir path(dirPath);items.append(path.dirName());
        }
    }
    else{
        QDirIterator it(ptr->path,QStringList()<<"*.tif"<<"*.tiff"<<"*.jpg"<<"*.bmp"<<"*.png",QDir::Files,QDirIterator::NoIteratorFlags);
        int i=0;while(it.hasNext()){it.next();m_multiFilePaths[i].append(it.filePath());i++;}
    }
    m_imageFolderPath=ptr->path;delete ptr;
    importImages();return true;
}

bool LoadSliceImages::updateImage(){
    Common *c=Common::i();
    if(!m_isTransformed&&c->m_modelType==m_lastImageType&&m_imageIndex==m_lastImageIndex){return false;}
    if(m_lastImageIndex!=m_imageIndex){bIndexUpdated=true;}else{bIndexUpdated=false;}
    m_isTransformed=false;m_lastImageType=c->m_modelType;m_lastImageIndex=m_imageIndex;
    getSliceByIndex(m_imageIndex-1, m_lastImageType);

    if(nullptr!=imageData){
        int pSize[3]={0};imageData->GetDimensions(pSize);
        char *pData=(char*)imageData->GetScalarPointer();cv::Mat m(cv::Size(pSize[0],pSize[1]),CV_16UC1,pData);
        double *origin=imageData->GetOrigin(),*spacing=imageData->GetSpacing(),rotation=0;
        m_groupIndex=-1;mask=cv::Mat::ones(cv::Size(pSize[0],pSize[1]),CV_8UC1);

        if(2==m_lastImageType){
            Image *pImage=m_images[m_imageIndex-1];/*m=pImage->data;*/m_currentImage=pImage;m_groupIndex=pImage->groupIndex;
            Group *slice=m_groups.value(m_groupIndex,nullptr);m_currentGroup=slice;cv::Mat cell=pImage->cell;
            TransformParameters *param=new TransformParameters;if(bIndexUpdated){param->sliceIndex=m_groupIndex;}
            if(nullptr!=slice){
                for(int k=0;k<2;k++){
                    origin[k]+=slice->param.offset[k]-pSize[k]*(slice->param.scale[k]-1)*spacing[k]/2;
                    spacing[k]*=slice->param.scale[k];}
                rotation=slice->param.rotation[2];if(bIndexUpdated){param->copy(slice->param);}
                if(bIndexUpdated){emit c->transform2dChanged(param);}
                cv::Rect rect=slice->param.roi;rect.x=min(rect.x, pSize[0]);rect.y=min(rect.y, pSize[1]);
                rect.width=min(rect.width,pSize[0]-rect.x);rect.height=min(rect.height,pSize[1]-rect.y);
                if(rect.width>0&&rect.height>0&&m_autoCrop){mask(rect)=0;}
                else{mask=0;}

                if(!c->isSliceWarping&&!c->isCellPicking&&!slice->warpField.empty()&&slice->warpField.size()==m.size()&&slice->warpField.type()==CV_32FC2){
                    cv::Mat dst=cv::Mat::zeros(m.size(),m.type());int w=m.cols,h=m.rows;
                    uint16_t *pDst=(uint16_t*)dst.data,*pDstEnd=pDst+m.size().area(),*pSrc=(uint16_t*)m.data;
                    float *pMap=(float*)slice->warpField.data;
                    for(;pDst!=pDstEnd;pDst++,pMap+=2){int x=pMap[0],y=pMap[1];if(x>=0&&x<w&&y>=0&&y<h){*pDst=*(pSrc+y*w+x);}}

                    cv::Mat dst1=cv::Mat::zeros(cell.size(),cell.type());int w1=cell.cols,h1=cell.rows;
                    uint16_t *pDst1=(uint16_t*)dst1.data,*pDstEnd1=pDst1+cell.size().area(),*pSrc1=(uint16_t*)cell.data;
                    float *pMap1=(float*)slice->warpField.data;
                    for(;pDst1!=pDstEnd1;pDst1++,pMap1+=2){int x=pMap1[0],y=pMap1[1];if(x>=0&&x<w1&&y>=0&&y<h1){*pDst1=*(pSrc1+y*w1+x);}}
                    m=dst;cell=dst1;
                }
            }
        if(!c->isCellPicking){updateCellCloud(cell,origin,spacing,rotation);}
        }
        else{mask=0;}
        updateResliceImage(m,origin,spacing,rotation,m_imageIndex,m_groupIndex);
        emit imageUpdate();
    }
    return true;
}

void LoadSliceImages::setBuffer(char *buffer,const cv::Point3i &volumeSize,const cv::Point3d &voxelSize,int index){
    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();//qDebug()<<imageSize<<imageNumber;
    importer->SetDataScalarTypeToUnsignedShort();importer->SetWholeExtent(0,volumeSize.x-1,0,volumeSize.y-1,0,volumeSize.z-1);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(buffer);
    importer->Update();
    m_volumeSize=cv::Point3f(volumeSize.x*voxelSize.x,volumeSize.y*voxelSize.y,volumeSize.z*voxelSize.z);
    vtkSmartPointer<vtkImageData> m_sliceImageData=vtkSmartPointer<vtkImageData>::New();
    m_sliceImageData->DeepCopy(importer->GetOutput());m_sliceImageData->SetSpacing(voxelSize.x,voxelSize.y,voxelSize.z);
    m_reslice[index]=vtkSmartPointer<vtkImageReslice>::New();m_reslice[index]->SetInputData(m_sliceImageData);
    m_reslice[index]->SetOutputDimensionality(2);m_reslice[index]->SetInterpolationModeToLinear();m_reslice[index]->SetAutoCropOutput(true);
}

cv::Rect LoadSliceImages::autoCrop(const cv::Mat &image){
//    cv::Size size=image.size();double scale=1;cv::resize(img, img, cv::Size(128, int(size.height/scale)), 0, 0, cv::INTER_CUBIC);
    cv::Mat img;image.convertTo(img,CV_8UC1,0.25);
    cv::Mat thr;cv::threshold(img, thr, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
    cv::Mat kernel=cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::Mat dilating;cv::morphologyEx(thr,dilating, cv::MORPH_DILATE, kernel);
    cv::Mat labels,stats,centroids;
    int nccomps=cv::connectedComponentsWithStats(dilating,labels,stats,centroids,8,CV_16U);
    std::vector<int> a;
    int maxValue=0,subMaxValue=0;
    int maxIndex=0, subMaxIndex=0;
    for (int i=1;i<nccomps;i++) {
        int area=stats.at<int>(i, cv::CC_STAT_AREA);
        if(area>subMaxValue){subMaxValue=area;subMaxIndex=i;
            if(subMaxValue>maxValue){
                int tmpValue=maxValue;maxValue=subMaxValue;subMaxValue=tmpValue;
                int tmpIndex=maxIndex;maxIndex=subMaxIndex;subMaxIndex=tmpIndex;}
        }
    }
    int left,top,width,height;
    if(subMaxValue>0.7*maxValue){
        int maxRight=stats.at<int>(maxIndex, cv::CC_STAT_LEFT)+stats.at<int>(maxIndex, cv::CC_STAT_WIDTH);
        int subMaxRight=stats.at<int>(subMaxIndex, cv::CC_STAT_LEFT)+stats.at<int>(subMaxIndex, cv::CC_STAT_WIDTH);
        int maxBottom=stats.at<int>(maxIndex, cv::CC_STAT_TOP)+stats.at<int>(maxIndex, cv::CC_STAT_HEIGHT);
        int subMaxBottom=stats.at<int>(subMaxIndex, cv::CC_STAT_TOP)+stats.at<int>(subMaxIndex, cv::CC_STAT_HEIGHT);
        int right=max(maxRight, subMaxRight);
        int bottom=max(maxBottom,subMaxBottom);
        left=min(stats.at<int>(maxIndex, cv::CC_STAT_LEFT),stats.at<int>(subMaxIndex, cv::CC_STAT_LEFT));
        top=min(stats.at<int>(maxIndex, cv::CC_STAT_TOP),stats.at<int>(subMaxIndex, cv::CC_STAT_TOP));
        width=right-left;
        height=bottom-top;
    }
    else{
        left=stats.at<int>(maxIndex, cv::CC_STAT_LEFT);
        top=stats.at<int>(maxIndex, cv::CC_STAT_TOP);
        width=stats.at<int>(maxIndex, cv::CC_STAT_WIDTH);
        height=stats.at<int>(maxIndex, cv::CC_STAT_HEIGHT);
    }
    cv::Rect rectROI(left,top,width,height);return rectROI;
//    cv::Rect rectROI(int(left*scale),int(top*scale),int(width*scale),int(height*scale));
}

void LoadSliceImages::getSliceByIndex(int index,int type){
    vtkSmartPointer<vtkMatrix4x4> resliceAxes1=vtkSmartPointer<vtkMatrix4x4>::New();
    resliceAxes1->Identity();
    if(0==type){
        InitializeSagittalMatrix(resliceAxes1);
    }else if(1==type){
        InitializeCoronalMatrix(resliceAxes1);
    }else{
        InitializeAxialMatrix(resliceAxes1);
    }
    double pVoxel=m_voxelSizes[type];
    double position[4]={0,0,index*pVoxel,1}; double *position2=resliceAxes1->MultiplyDoublePoint(position);
    for(int i=0;i<3;i++){resliceAxes1->SetElement(i,3,position2[i]);}
    int channel=Common::i()->m_conboBoxIndex-2; 

    if(items.length()==1||channel==-2){
        m_reslice[0]->SetResliceAxes(resliceAxes1);m_reslice[0]->Update();
        imageData=m_reslice[0]->GetOutput();
    }
    else if(channel==-1){
        int i=0;m_imageBlend=vtkSmartPointer<vtkImageBlend>::New();
        foreach(vtkSmartPointer<vtkImageReslice> reslice, m_reslice){
            reslice->SetResliceAxes(resliceAxes1);reslice->Update();
            m_imageBlend->AddInputData(reslice->GetOutput());m_imageBlend->SetOpacity(i, 1.0/(items.length()-1.0));i+=1;
        }
        m_imageBlend->Update();imageData=m_imageBlend->GetOutput();
    }
    else{
        m_reslice[channel]->SetResliceAxes(resliceAxes1);m_reslice[channel]->Update();
        imageData=m_reslice[channel]->GetOutput();
    }
}

void LoadSliceImages::updateResliceImage(const cv::Mat &image,double *origin,double *spacing,double rotation,int imageIndex,int groupIndex){
    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();
    importer->SetDataScalarTypeToUnsignedShort();importer->SetWholeExtent(0,image.cols-1,0,image.rows-1,0,0);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(image.data);
    importer->Update();

    vtkSmartPointer<vtkImageData> imageData=vtkSmartPointer<vtkImageData>::New();imageData->DeepCopy(importer->GetOutput());
    imageData->SetSpacing(spacing);

    vtkSmartPointer<vtkImageMask> imageMaskData=vtkSmartPointer<vtkImageMask>::New();
    imageMaskData->SetImageInputData(imageData);

    vtkSmartPointer<vtkImageImport> maskSource=vtkSmartPointer<vtkImageImport>::New();
    maskSource->SetDataScalarTypeToUnsignedChar();maskSource->SetWholeExtent(0,image.cols-1,0,image.rows-1,0,0);
    maskSource->SetNumberOfScalarComponents(1);maskSource->SetDataExtentToWholeExtent();maskSource->SetImportVoidPointer(mask.data);
    maskSource->Update();
    vtkSmartPointer<vtkImageData> maskData=vtkSmartPointer<vtkImageData>::New();maskData->DeepCopy(maskSource->GetOutput());
    maskData->SetSpacing(spacing);
    imageMaskData->SetMaskInputData(maskData);
    imageMaskData->NotMaskOn();
    imageMaskData->Update();

    m_colorTable=vtkSmartPointer<vtkLookupTable>::New();
    m_colorTable->SetRange(0,m_grayLevel);m_colorTable->SetValueRange(0.0,1.0);m_colorTable->SetSaturationRange(0,0);m_colorTable->SetHueRange(0,0);
    m_colorTable->SetRampToLinear();m_colorTable->Build();
    m_colorMap2=vtkSmartPointer<vtkImageMapToColors>::New();m_colorMap2->SetLookupTable(m_colorTable);
    m_colorMap2->SetInputData(imageMaskData->GetOutput());m_colorMap2->Update();

    vtkSmartPointer<vtkImageActor> imgActor=vtkSmartPointer<vtkImageActor>::New();
    imgActor->SetInputData(m_colorMap2->GetOutput());imgActor->SetPosition(origin);
    imgActor->SetOrigin(image.cols*spacing[0]/2,image.rows*spacing[1]/2,0);
    if(rotation!=0){imgActor->SetOrientation(0,0,rotation);}

    m_imageActorMutex.lock();
    m_imageActors[1]=imgActor;m_nextGroupIndex=groupIndex;m_nextImageIndex2=imageIndex;
    m_imageActorMutex.unlock();
}

void LoadSliceImages::updateCellCloud(const cv::Mat &image,double *origin, double *spacing, double rotation){
    vtkSmartPointer<vtkImageImport> importer=vtkSmartPointer<vtkImageImport>::New();
    importer->SetDataScalarTypeToUnsignedShort();importer->SetWholeExtent(0,image.cols-1,0,image.rows-1,0,0);
    importer->SetNumberOfScalarComponents(1);importer->SetDataExtentToWholeExtent();importer->SetImportVoidPointer(image.data);
    importer->Update();

    vtkSmartPointer<vtkImageData> imageData=vtkSmartPointer<vtkImageData>::New();imageData->DeepCopy(importer->GetOutput());
    imageData->SetSpacing(spacing);

    vtkSmartPointer<vtkLookupTable> colorTable=vtkSmartPointer<vtkLookupTable>::New();
    colorTable->SetRange(0,255);colorTable->SetValueRange(0.0,1.0);colorTable->SetAlphaRange(0,1);
    colorTable->SetSaturationRange(0,1);colorTable->SetHueRange(0,1.0);
    colorTable->SetRampToLinear();colorTable->Build();
    colorTable->SetTableValue(255,0.0,0.0,1.0,1.0);
    colorTable->SetTableValue(0,0.0,0.0,0.0,0.0);

    vtkSmartPointer<vtkImageMapToColors> colorMap=vtkSmartPointer<vtkImageMapToColors>::New();
    colorMap->SetLookupTable(colorTable);colorMap->SetInputData(imageData);colorMap->Update();

    vtkSmartPointer<vtkImageActor> imgActor=vtkSmartPointer<vtkImageActor>::New();
    imgActor->SetInputData(colorMap->GetOutput());imgActor->SetPosition(origin);
    imgActor->SetOrigin(image.cols*spacing[0]/2,image.rows*spacing[1]/2,0);
    if(rotation!=0){imgActor->SetOrientation(0,0,rotation);}

    m_imageActorMutex.lock();m_cellActors[1]=imgActor;m_imageActorMutex.unlock();
}

void LoadSliceImages::transform2d(){
    if(m_bEdited){
        TransformParameters *ptr=Common::i()->p_params2d.exchange(nullptr);if(nullptr==ptr){return;}
        Group *slice=m_groups.value(ptr->sliceIndex,nullptr);
        if(nullptr==slice){slice=new Group;m_groups[ptr->sliceIndex]=slice;}
        slice->param.copy(*ptr);m_isTransformed=true;delete ptr;}
}

void LoadSliceImages::changeImageModel(int index){
    Common *c=Common::i();
    if(m_bEdited&&c->m_modelType==2){m_images[m_imageIndex-1]->modelIndex=index;}}

void LoadSliceImages::buildWarpField(){
    Common *c=Common::i();int warpType=c->m_nextWarpType.exchange(-1);if(warpType<0){return;}
    int index=1;int number=m_groups.size();
    foreach(Group *p,m_groups){
        emit c->showMessage("Performing deformation for group "+QString::number(index)+"/"+QString::number(number));
        buildWarpField(p->param.sliceIndex,p->warpField,true);
    }
    emit c->showMessage("Done");m_isTransformed=true;
}

void LoadSliceImages::buildWarpField(int sliceIndex, cv::Mat &warpField, bool bInversed){
    Group *slice=m_groups[sliceIndex];
    double offsets[2]={slice->param.offset[0]-m_currentGroup->param.offset[0],slice->param.offset[1]-m_currentGroup->param.offset[1]},
            scales[2]={slice->param.scale[0]/m_currentGroup->param.scale[0],slice->param.scale[1]/m_currentGroup->param.scale[1]};

    QList<QLineF> lines;BuildWarpingField::i()->getMarkers(sliceIndex,offsets,scales,slice->param.rotation[2],lines);
    if(lines.empty()){warpField=cv::Mat();return;}

    QPointF factor(1,1);int width=m_rawImageSize.x,height=m_rawImageSize.y;
    int nout=width*height,nin=lines.length()+4;

    point *poutX=new point[nout],*poutY=new point[nout];point *_poutX=poutX,*_poutY=poutY;
    for(int y=0;y<height;y++){
        for(int x=0;x<width;x++){
            _poutX->x=x;_poutX->y=y;_poutX->z=0;_poutY->x=x;_poutY->y=y;_poutY->z=0;_poutX++;_poutY++;
        }
    }

    point *pinX=new point[nin],*pinY=new point[nin];point *_pinX=pinX,*_pinY=pinY;
    foreach(QLineF p,lines){
        double x,y,dx=(p.p1().x()-p.p2().x())*factor.x(),dy=(p.p1().y()-p.p2().y())*factor.y();

        if(bInversed){x=p.p2().x()*factor.x();y=p.p2().y()*factor.y();}
        else{x=p.p1().x()*factor.x();y=p.p1().y()*factor.y();dx=-dx;dy=-dy;}

        _pinX->x=x;_pinX->y=y;_pinX->z=dx;_pinY->x=x;_pinY->y=y;_pinY->z=dy;_pinX++;_pinY++;
    }
    foreach(QPoint p,QList<QPoint>()<<QPoint(-width,-height)<<QPoint(2*width,-height)<<QPoint(-width,2*height)<<QPoint(2*width,2*height)){
        _pinX->x=p.x();_pinX->y=p.y();_pinX->z=0;_pinX++;_pinY->x=p.x();_pinY->y=p.y();_pinY->z=0;_pinY++;
    }

    nnpi_interpolate_points(nin,pinX,-DBL_MAX,nout,poutX);
    nnpi_interpolate_points(nin,pinY,-DBL_MAX,nout,poutY);

    cv::Mat output=cv::Mat(height,width,CV_32FC2);
    float *pData=(float*)output.data;_poutX=poutX;_poutY=poutY;
    for(int i=0,x=0,y=0;i<nout;i++,_poutX++,_poutY++,pData+=2){
        float dx=_poutX->z,dy=_poutY->z,x1=x+dx,y1=y+dy;
        pData[0]=x1;pData[1]=y1;x++;if(x==width){x=0;y++;}
    }
    warpField=output;delete[] pinX;delete[] pinY;delete[] poutX;delete[] poutY;
}

void LoadSliceImages::onImportSpots(){
    if(!m_bEdited){Common::i()->showMessage("Please load images first"); return;}
    QString path=QFileDialog::getExistingDirectory(nullptr,"Select a directory to import spots","",QFileDialog::ShowDirsOnly|QFileDialog::DontResolveSymlinks);
    if(!path.isEmpty()){delete m_spotsPath2Import.exchange(new QString(path));}
}

void LoadSliceImages::importSpots(){
    QString *pathPtr=m_spotsPath2Import.exchange(nullptr);if(nullptr==pathPtr){return;}
    QString path=*pathPtr;delete pathPtr;
    double x1=m_rawImageSize.x/2, y1=m_rawImageSize.y/2;
    foreach(Image *image,m_images){
        image->spots.clear();image->cell=cv::Mat::zeros(m_rawImageSize.y,m_rawImageSize.x,CV_16UC1);

        QString spotsPath=path+"/"+QFileInfo(image->filePath[0]).fileName()+".csv";
        image->spotsPath=QFile(spotsPath).exists()?spotsPath:"";
        if(image->spotsPath.isEmpty()){return;}QFile file(image->spotsPath);if(!file.open(QIODevice::ReadOnly|QIODevice::Text)){return;}
        cv::Rect imgROI=image->roi;double x0=imgROI.x+imgROI.width/2;double y0=imgROI.y+imgROI.height/2;double offsetX=x1-x0, offsetY=y1-y0;

        QTextStream in(&file);
        while(!in.atEnd()){
            QStringList line=in.readLine().split(",");if(line.length()!=8){continue;}
            if(line[4]!="Spot"||line[5]!="Position"){continue;}
//            bool bOK;int64_t spotId=line[7].toLongLong(&bOK);if(!bOK){continue;}
            Point *point=new Point();point->p1[0]=line[0].toDouble()+offsetX;point->p1[1]=line[1].toDouble()+offsetY;point->p1[2]=line[2].toDouble();
            if(point->p1[0]<0||point->p1[0]>m_rawImageSize.x||point->p1[1]<0||point->p1[1]>m_rawImageSize.y){continue;}
            image->spots.append(point);image->cell.at<ushort>(int(point->p1[1]),int(point->p1[0]));
        }
    }
}

void LoadSliceImages::onLoadProject(){
    QString path=QFileDialog::getOpenFileName(nullptr,"Open project file","","(*.json *.visor *.flsm)");if(path.isEmpty()){return;}
    delete m_projectPathPtr.exchange(new QString(path));
}

bool LoadSliceImages::loadProject(){
    QString *pathPtr=m_projectPathPtr.exchange(nullptr);if(nullptr==pathPtr){return false;}
    m_imageFolderPath=*pathPtr;delete pathPtr;QString pathSuffix=QFileInfo(m_imageFolderPath).suffix();
    if(pathSuffix=="json"){return loadJsonFile(m_imageFolderPath);}
    else if(pathSuffix=="visor"){return loadVisorFile(m_imageFolderPath);}
    else {return false;}
}


bool LoadSliceImages::loadJsonFile(QString path){
    Common *c=Common::i();LoadBrainAtlases *m=LoadBrainAtlases::i();BuildWarpingField *w=BuildWarpingField::i();
    QVariantMap info;if(!Common::loadJson(path,info)){emit c->showMessage("Unable to load project file "+path);return false;}

    m_groupSize=info["group_size"].toInt();m_imageFolderPath=info["image_path"].toString();m_projectPath=path;
    m_voxelSizes[0]=info["voxel_size"].toString().split(" ")[0].toInt();m_voxelSizes[1]=info["voxel_size"].toString().split(" ")[1].toInt();
    m_voxelSizes[2]=info["voxel_size"].toString().split(" ")[2].toInt();
    m_rawImageSize.x=info["image_size"].toString().split(" ")[0].toInt();m_rawImageSize.y=info["image_size"].toString().split(" ")[1].toInt();
    m_rawImageSize.z=info["image_size"].toString().split(" ")[2].toInt();
    items.clear();items.append("Model");items.append("Slice");QVariantList channelList=info["channel"].toList();
    if(channelList.length()>1){foreach(QVariant item,channelList){items.append(item.toString());}}

    qDeleteAll(m_images);m_images.clear();
    foreach(QVariant v,info["images"].toList()){
        QVariantMap v1=v.toMap();Image *image=new Image;
        foreach(QVariant item,channelList){
            cv::Mat m;QVariantMap v2=v1[item.toString()].toMap();
            image->index=v2["index"].toInt();image->groupIndex=(image->index-1)/m_groupSize+1;
            image->size.append(QSize(v1["width"].toInt(),v1["height"].toInt()));
            QString filePath=v2["file_name"].toString();image->filePath.append(filePath);

            if(filePath.endsWith(".flsm")||filePath.endsWith(".tar")){loadTarBundle(filePath,m);}
            else{m=cv::imread(filePath.toStdString(),-1);}
            if(m.empty()){emit c->showMessage("Failed to load image "+filePath);return false;}
            if(m.type()==CV_8UC3){cv::cvtColor(m,m,CV_BGR2GRAY);}
            else if(m.type()==CV_8UC4){cv::cvtColor(m,m,CV_BGRA2GRAY);}
            if(m.type()==CV_8UC1){m.convertTo(m,CV_16UC1,4);}
            if(m.type()!=CV_16UC1){continue;}
            flip(m,m,0);cv::Size size=m.size();image->data.append(m);
            image->size.append(QSize(size.width,size.height));
        }
        m_images.append(image);
    }
    if(m_images.empty()){emit c->showMessage("No image");return false;}

    QVariantMap params=info["freesia_project"].toMap();
    if(!params.empty()){
        QVariantMap transformParam3d=params["transform_3d"].toMap();
        QStringList rotations=transformParam3d["rotation"].toString().split(" "),offsets=transformParam3d["translation"].toString().split(" "),scales=transformParam3d["scale"].toString().split(" ");
        if(rotations.length()==2){m->m_transform3d->rotation[0]=rotations[0].toDouble();m->m_transform3d->rotation[1]=rotations[1].toDouble();}
        if(offsets.length()==3){m->m_transform3d->offset[0]=offsets[0].toDouble();m->m_transform3d->offset[1]=offsets[1].toDouble();m->m_transform3d->offset[2]=offsets[2].toDouble();}
        if(scales.length()==3){m->m_transform3d->scale[0]=scales[0].toDouble();m->m_transform3d->scale[1]=scales[1].toDouble();m->m_transform3d->scale[2]=scales[2].toDouble();}
        TransformParameters *params3d=new TransformParameters;params3d->copy(*m->m_transform3d);emit c->transform3dChanged(params3d);

        qDeleteAll(m_groups);m_groups.clear();
        QVariantList slices=params["transform_2d"].toList();
        foreach(QVariant v,slices){
            QVariantMap v1=v.toMap();Group *slice=new Group;int index=v1["group_index"].toInt();slice->param.sliceIndex=index;m_groups[index]=slice;
            QStringList rotations=v1["rotation"].toString().split(" "),offsets=v1["translation"].toString().split(" "),scales=params["scale"].toString().split(" "),roi=params["roi"].toString().split(" ");
            if(rotations.length()==1){slice->param.rotation[2]=rotations[0].toDouble();}
            if(offsets.length()==2){slice->param.offset[0]=offsets[0].toDouble();slice->param.offset[1]=offsets[1].toDouble();}
            if(scales.length()==2){slice->param.scale[0]=scales[0].toDouble();slice->param.scale[1]=scales[1].toDouble();}
            if(roi.length()==3){slice->param.roi.x=roi[0].toInt();slice->param.roi.y=roi[1].toInt();slice->param.roi.width=roi[2].toInt();slice->param.roi.height=roi[3].toInt();}
        }
        w->importAllMarkers(params["warp_markers"].toList());
    }
    loadImages();return true;
}

bool LoadSliceImages::loadVisorFile(QString path){
    Common *c=Common::i();QVariantMap info;
    if(!Common::loadJson(path,info)){emit c->showMessage("Unable to load flsm file");return false;}
    QFileInfo fileInfo(path);QString imageFolderPath=fileInfo.dir().absolutePath();
    m_multiFilePaths.clear();items.clear();int i=0;
    foreach(QVariant v,info["Acquisition Results"].toList()){
        QVariantMap v1=v.toMap();
        foreach(QVariant s,v1["FlsmList"].toList()){
            if(s.toString()==""){continue;}
            QString imagePath=imageFolderPath+"/"+s.toString();
            if(!QFile::exists(imagePath)){emit c->showMessage("Unable to load flsm file");return false;}
            m_multiFilePaths[i].append(imagePath);
        }i++;
    }
    items.append("Model");items.append("Slice");
    if(info["Channels"].toList().length()>1){
        foreach(QVariant v,info["Channels"].toList()){
            QVariantMap v1=v.toMap();items.append(v1["ChannelName"].toString());
        }
    }
    m_voxelSizes[0]=8;m_voxelSizes[1]=8;m_voxelSizes[2]=300;m_groupSize=1;
    importImages();return true;
}

bool LoadSliceImages::importImages(){
    Common *c=Common::i();int maxWidth=0,maxHeight=0;
    int count=0;qDeleteAll(m_images);m_images.clear();
    foreach(QStringList multiFilePath,m_multiFilePaths){
        Image *image=new Image;
        foreach(QString filePath, multiFilePath){cv::Mat m;
            if(filePath.endsWith(".flsm")||filePath.endsWith(".tar")){loadTarBundle(filePath,m);}
            else{m=cv::imread(filePath.toStdString(),-1);}
            if(m.empty()){emit c->showMessage("Failed to load image "+filePath);return false;}
            if(m.type()==CV_8UC3){cv::cvtColor(m,m,CV_BGR2GRAY);}
            else if(m.type()==CV_8UC4){cv::cvtColor(m,m,CV_BGRA2GRAY);}
            if(m.type()==CV_8UC1){m.convertTo(m,CV_16UC1,4);}
            if(m.type()!=CV_16UC1){continue;}
            flip(m,m,0);cv::Size size=m.size();maxWidth=max(size.width,maxWidth);maxHeight=max(size.height,maxHeight);
            image->data.append(m);image->size.append(QSize(size.width,size.height));
        }
        count++;image->filePath=multiFilePath;image->index=count;image->groupIndex=(count-1)/m_groupSize+1;
        float thickness=m_voxelSizes[2]/c->m_voxelSize; image->modelIndex=int((count-1)*thickness+1);
        m_images.append(image);
    }
    if(m_images.empty()){m_bEdited=0;emit c->showMessage("Unable to load images in directory");return false;}
    m_rawImageSize=cv::Point3i(maxWidth,maxHeight,m_images.length());
    loadImages();return true;
}

void LoadSliceImages::loadTarBundle(const QString &filePath,cv::Mat &dst){
    std::string projectionPath=filePath.toStdString();
    if(filePath.endsWith(".flsm")){
        FlsmReader flsmReader(projectionPath);
        projectionPath=flsmReader.path("projection");
    }
    SlideReader reader(projectionPath);

    std::vector<float> rect=reader.region();
    int cols = int(floor(rect[2] / m_voxelSizes[0]) + 1),rows = int(floor(rect[3] / m_voxelSizes[0]) + 1);
    float scale=reader.pixelSize()[1]/m_voxelSizes[0];const size_t num=reader.imageNumber();
    dst=cv::Mat::zeros(rows,cols,CV_16UC1);

    for(size_t i=0;i<num;i++){
        flsm::Image *ptr=reader.thumbnail(i);if(nullptr==ptr){continue;}
        int h=ptr->m_size.height,w=ptr->m_size.width;
        h=int(floor(h * scale) + 1);w=int(floor(w * scale) + 1);
        if(w>5&&h>5){
            int x = int(floor((ptr->m_position.x*1000 - rect[0]) / m_voxelSizes[0])),y = int(floor((ptr->m_position.y*1000 - rect[1]) / m_voxelSizes[0]));
            ptr->decode();
            cv::Mat src(ptr->m_size.height,ptr->m_size.width,CV_16UC1,ptr->data());
            cv::Mat src1;resize(src,src1,cv::Size(w,h));
            if(x<0){x=0;}if(y<0){y=0;}
            if(x+w>dst.cols){w=dst.cols-x;}if(y+h>dst.rows){h=dst.rows-y;}
            if(w>5&&h>5){src1.copyTo(dst(cv::Rect(x,y,w,h)));}
        }
        ptr->release();
    }
}

void LoadSliceImages::loadImages(){
    Common *c=Common::i();qDeleteAll(m_groups);m_groups.clear();
    int width=m_rawImageSize.x,height=m_rawImageSize.y;int channel=max(1,items.length()-2);
    size_t bufferSize=width*height*2;int number=m_rawImageSize.z,count=0;
    QMap<int,char*>buffer, pBuffer;
    for(int i=0;i<channel;i++){
        buffer[i]=((char*)malloc(bufferSize*number)); pBuffer[i]=buffer[i];
    }

    cv::Rect roi;
    foreach(Image *image,m_images){
        count++;emit c->showMessage(QString("Loading image %1/%2").arg(QString::number(count),QString::number(number)));
        if(!m_groups.contains(image->groupIndex))
        {   cv::Mat m1=image->data[0];roi=autoCrop(m1);
            Group *p=new Group;p->param.sliceIndex=image->groupIndex;
            p->param.roi=cv::Rect((width-roi.width)/2,(height-roi.height)/2,roi.width,roi.height);
            m_groups[image->groupIndex]=p;
        }
        image->roi=roi;image->cell=cv::Mat::zeros(height,width,CV_16UC1);
        for(int i=0;i<channel;i++){
            cv::Mat m1=image->data[i];
            cv::Mat m2=cv::Mat::zeros(height,width,CV_16UC1);
            int l=max(0,int(0.5*(width-roi.width)-roi.x));
            int t=max(0,int(0.5*(height-roi.height)-roi.y));
            int l1=max(0,int(roi.x-0.5*(width-roi.width)));
            int t1=max(0,int(roi.y-0.5*(height-roi.height)));
            int w=min(width-l,m1.size().width-l1);
            int h=min(height-t,m1.size().height-t1);
            cv::Rect m2Rect(l,t,w,h);cv::Rect m1Rect(l1,t1,w,h);
            m1(m1Rect).copyTo(m2(m2Rect));
            memcpy(pBuffer[i],m2.data,bufferSize);
            pBuffer[i]+=bufferSize;
        }
    }
    cv::Point3f imageVoxelSize(m_voxelSizes[0],m_voxelSizes[1],m_voxelSizes[2]);
    for(int i=0;i<channel;i++){
        setBuffer(buffer[i],m_rawImageSize,imageVoxelSize,i);free(buffer[i]);
    }

    if(c->m_modelType==2){c->m_maxImageNumber=m_rawImageSize.z;m_imageIndex=m_rawImageSize.z/2;}
    else if(c->m_modelType==1){c->m_maxImageNumber=m_rawImageSize.y;m_imageIndex=m_rawImageSize.y/2;}
    else if(c->m_modelType==0){c->m_maxImageNumber=m_rawImageSize.x;m_imageIndex=m_rawImageSize.x/2;}
    emit addComboBoxItems(items);emit c->comboBoxIndex(1);emit c->showMessage("Done");m_bEdited=1;
}

void LoadSliceImages::onContrastChanged(int v){
    if(!m_bEdited){return;}m_grayLevel=v;
    m_colorTable->SetRange(0,v);m_colorTable->Build();m_colorMap2->Update();
    Common::i()->m_imageViewer->GetRenderWindow()->Render();
}

void LoadSliceImages::onSaveProject(){
    QString projectPath;
    if(m_bEdited){//m_projectPath.isEmpty()
        QString path=QFileDialog::getSaveFileName(nullptr,"Save as project","","(*.json)");
        if(!path.isEmpty()){projectPath=path;}else{return;}
    }
    delete m_projectPath2Save.exchange(new QString(projectPath));
}

void LoadSliceImages::saveProject(){
    LoadBrainAtlases *m=LoadBrainAtlases::i();
    QString *pathPtr=m_projectPath2Save.exchange(nullptr);if(nullptr==pathPtr){return;}
    QString path=*pathPtr;delete pathPtr;if(!path.isEmpty()){m_projectPath=path;}

    QFileInfo fileInfo(m_projectPath);QDir rootDir=fileInfo.dir(),imageDir(m_imageFolderPath);

    QVariantMap info;QVariantList images;
    info["group_size"]=m_groupSize;info["image_path"]=rootDir.relativeFilePath(m_imageFolderPath);
    info["voxel_size"]=QString("%1 %2 %3").arg(QString::number(m_voxelSizes[0]),QString::number(m_voxelSizes[1]),QString::number(m_voxelSizes[2]));
    info["image_size"]=QString("%1 %2 %3").arg(QString::number(m_rawImageSize.x),QString::number(m_rawImageSize.y),QString::number(m_rawImageSize.z));
    int channel=0;
    foreach(Image *image,m_images){
        QVariantMap vlist; channel=image->filePath.length();
        if(channel<=1){
            QVariantMap v;v["index"]=image->index;v["width"]=image->size[0].width();v["height"]=image->size[0].height();
            v["file_name"]=image->filePath[0];vlist["Slice"]=v;
        }
        else{
            for(int i=0;i<channel;i++){
                QVariantMap v;v["index"]=image->index;v["width"]=image->size[i].width();v["height"]=image->size[i].height();
                v["file_name"]=image->filePath[i];vlist[items[i+2]]=v;
            }
        }
        images.append(vlist);
    }
    info["images"]=images;
    QVariantList chanelList;
    if(channel<=1){chanelList.append("Slice");}
    else{for(int i=0;i<channel;i++){chanelList.append(items[i+2]);}}
    info["channel"]=chanelList;
    QVariantMap params,transformParam3d;
    transformParam3d["rotation"]=QString("%1 %2").arg(QString::number(m->m_transform3d->rotation[0]),QString::number(m->m_transform3d->rotation[1]));
    transformParam3d["translation"]=QString("%1 %2 %3").arg(QString::number(m->m_transform3d->offset[0]),QString::number(m->m_transform3d->offset[1]),QString::number(m->m_transform3d->offset[2]));
    transformParam3d["scale"]=QString("%1 %2 %3").arg(QString::number(m->m_transform3d->scale[0]),QString::number(m->m_transform3d->scale[1]),QString::number(m->m_transform3d->scale[2]));
    params["transform_3d"]=transformParam3d;

    QVariantList localInfos;
    foreach(Group *p,m_groups){
        QVariantMap v;v["rotation"]=QString("%1").arg(QString::number(p->param.rotation[2]));
        v["translation"]=QString("%1 %2").arg(QString::number(p->param.offset[0]),QString::number(p->param.offset[1]));
        v["scale"]=QString("%1 %2").arg(QString::number(p->param.scale[0]),QString::number(p->param.scale[1]));
        v["roi"]=QString("%1 %2 %3 %4").arg(QString::number(p->param.roi.x),QString::number(p->param.roi.y),QString::number(p->param.roi.width),QString::number(p->param.roi.height));
        v["group_index"]=p->param.sliceIndex;localInfos.append(v);
    }

    params["warp_markers"]=exportAllMarkers();

    params["transform_2d"]=localInfos;info["freesia_project"]=params;

    bool bOK=Common::saveJson(m_projectPath,info);Common *c=Common::i();
    if(bOK){
        emit c->showMessage("Project has been saved at "+fileInfo.fileName());
        foreach(Group *p,m_groups){p->paramSaved.copy(p->param);}
    }else{emit c->showMessage("Unable to save project to "+m_projectPath);}
}

QVariantList LoadSliceImages::exportAllMarkers(){
    BuildWarpingField *w=BuildWarpingField::i();
    QVariantList list;QMutexLocker locker(&w->m_nodesMutex);

    QMapIterator<int,QList<Node*>*> iter1(w->m_allNodes);
    while(iter1.hasNext()){
        iter1.next();int groupIndex=iter1.key();
        QListIterator<Node*> iter2(*iter1.value());
        while(iter2.hasNext()){
            Node *p=iter2.next();
            QVariantMap v;v["image_point"]=QString::number(p->p1[0])+" "+QString::number(p->p1[1]);
            v["atlas_point"]=QString::number(p->p2[0])+" "+QString::number(p->p2[1]);
            v["image_index"]=p->imageIndex;v["group_index"]=groupIndex;list.append(v);
        }
    }
    return list;
}

void LoadSliceImages::onExportCellCounting(){
    if(!m_bEdited){return;}
    QString path=QFileDialog::getExistingDirectory(nullptr,"Select a directory for cell counting results","",
                                                   QFileDialog::ShowDirsOnly|QFileDialog::DontResolveSymlinks);
    if(!path.isEmpty()){delete m_exportPath.exchange(new QString(path));}
}


void LoadSliceImages::exportCellCounting(){
    QString *pathPtr=m_exportPath.exchange(nullptr);if(nullptr==pathPtr){return;}
    QString path=*pathPtr;delete pathPtr;
    Common *c=Common::i();LoadBrainAtlases *m=LoadBrainAtlases::i();c->enableMouseEvent=true;

    QMap<int,cv::Mat> allWarpFields;int index=0,imageNumber=m_images.length();size_t colorCounts[65536];
    foreach(Image *image,m_images){
        emit c->showMessage("Export cell counting result for image "+QString::number(index+1)+"/"+QString::number(imageNumber));

        int index1=image->modelIndex;getSliceByIndex(index,2);m->getModelByIndex(index1,2);index++;
        if(nullptr==imageData||nullptr==m->modelData||image->spots.isEmpty()){continue;}

        int pSizeModel[3]={0};m->modelData->GetDimensions(pSizeModel);
        int w=pSizeModel[0],h=pSizeModel[1];if(w<=0||h<=0){continue;}
        uint16_t *pDataModel=(uint16_t*)m->modelData->GetScalarPointer();
        double *originModel=m->modelData->GetOrigin(),*spacingModel=m->modelData->GetSpacing();

        int pSizeImage[3]={0};imageData->GetDimensions(pSizeImage);
        double *originImage=imageData->GetOrigin(),*spacingImage=imageData->GetSpacing(),rotationImage=0;
        double spacingImageRaw[2]={spacingImage[0],spacingImage[1]};

        int groupIndex=image->groupIndex;Group *slice=m_groups.value(groupIndex,nullptr);
        if(nullptr!=slice){
            for(int k=0;k<2;k++){
                originImage[k]+=slice->param.offset[k]-pSizeImage[k]*(slice->param.scale[k]-1)*spacingImage[k]/2;
                spacingImage[k]*=slice->param.scale[k];
            }
            rotationImage=slice->param.rotation[2];
        }
        cv::Mat warpField;int w1=pSizeImage[0],h1=pSizeImage[1];
        if(allWarpFields.contains(groupIndex)){warpField=allWarpFields[groupIndex];}
        else{buildWarpField(groupIndex,warpField,false);allWarpFields[groupIndex]=warpField;}
        bool bWarpValid=(warpField.cols==w1&&warpField.rows==h1&&warpField.type()==CV_32FC2);

        double center[3]={originImage[0]+w1*spacingImage[0]/2,originImage[1]+h1*spacingImage[1]/2,0};
        memset(colorCounts,0,65536*8);

        cv::Mat modelImage=cv::Mat(cv::Size(w,h),CV_16UC1,pDataModel).clone();

        foreach(Point *p,image->spots){
            double px=p->p1[0],py=p->p1[1];
            if(bWarpValid){
                int x1=round(px),y1=round(py);if(x1<0||x1>=w1||y1<0||y1>=h1){continue;}
                float *pOffset=(float*)warpField.data+(y1*w1+x1)*2;px=pOffset[0];py=pOffset[1];
            }

            double p1[3]={px*spacingImage[0]+originImage[0],py*spacingImage[1]+originImage[1],0},p2[3];
            rotatePts(p1,center,rotationImage,p2);

            int x=round((p2[0]-originModel[0])/spacingModel[0]),y=round((p2[1]-originModel[1])/spacingModel[1]);
            if(x>=0&&x<w&&y>=0&&y<h){colorCounts[*(y*w+x+pDataModel)]++;modelImage.at<ushort>(y,x)=1000;}
        }

        QVariantList regions;
        for(int i=0;i<=65535;i++){
            size_t count=colorCounts[i];if(0==count){continue;}
            QVariantMap v;v["color"]=i;v["number"]=count;regions.append(v);
        }
        QVariantMap info;info["regions"]=regions;//qDebug()<<regions.length()<<"regions";
        c->saveJson(path+"/"+QFileInfo(image->filePath[0]).baseName()+".json",info);

        cv::flip(modelImage,modelImage,0);QString tifPath=path+"/images/";QDir().mkpath(tifPath);
        imwrite((tifPath+QFileInfo(image->filePath[0]).baseName()+".tif").toStdString(),modelImage);
    }
    emit mergeCellCounting(path);
}


