#include "common.h"
#include "pickcellpoints.h"
#include "loadbrainatlases.h"
#include "loadsliceimages.h"

static double s_colorPoint[3]={0,0,255};
static double s_colorCurrent[3]={255,0,0};

PickCellPoints::PickCellPoints():m_currentPoint(nullptr){}

void PickCellPoints::addPoint(int x, int y){
    Common *c=Common::i();LoadSliceImages *s=LoadSliceImages::i();
    if(!s->m_bEdited||nullptr==m_points||nullptr==s->m_imageActors[0]){return;}
    if(nullptr!=m_currentPoint){m_currentPoint->actor->GetProperty()->SetColor(s_colorPoint);m_currentPoint=nullptr;}
    m_currentPoint=new Point;
    double picked1[3];c->getPointPosition(x,y,picked1);
    vtkSmartPointer<vtkImageActor> act=s->m_imageActors[0];
    double *pos=act->GetPosition(),*spacing=act->GetInput()->GetSpacing(),*origin=act->GetOrigin();
    double origin2[2]={pos[0]+origin[0],pos[1]+origin[1]};
    double unRotPoint[3];rotatePts(picked1,origin2,-act->GetOrientation()[2],unRotPoint);
    double picked2[3]={(unRotPoint[0]-pos[0])/spacing[0],(unRotPoint[1]-pos[1])/spacing[1],0};

    vtkSmartPointer<vtkActor> actor=getNodeActor(picked1,s_colorCurrent);
    c->m_renderer->AddActor(actor);c->m_imageViewer->GetRenderWindow()->Render();
    m_currentPoint->actor=actor;m_currentPoint->p1[0]=picked2[0];m_currentPoint->p1[1]=picked2[1];
    m_currentPoint->p1Raw[0]=picked1[0];m_currentPoint->p1Raw[1]=picked1[1];
    m_points->append(m_currentPoint);s->m_currentImage->cell.at<ushort>(int(picked2[1]),int(picked2[0]))=255;
}

void PickCellPoints::selectPoint(int x, int y){
    Common *c=Common::i();if(nullptr==m_points||m_points->empty()){return;}
    double picked[3];c->getPointPosition(x,y,picked);double minDist=DBL_MAX;Point *closestNode=nullptr;
    QListIterator<Point*> iter(*m_points);
    while(iter.hasNext()){Point *p=iter.next();
        double d=calcuDistance(picked[0],picked[1],p->p1Raw[0],p->p1Raw[1],p->p1Raw[0],p->p1Raw[1]);
        if(d<minDist){minDist=d;closestNode=p;}}
    if(nullptr!=m_currentPoint&&m_currentPoint!=closestNode){m_currentPoint->actor->GetProperty()->SetColor(s_colorPoint);}
    if(nullptr!=closestNode){closestNode->actor->GetProperty()->SetColor(s_colorCurrent);}
    m_currentPoint=closestNode;c->m_imageViewer->GetRenderWindow()->Render();
}

void PickCellPoints::removeCurrentPoint(){
    if(nullptr==m_currentPoint){return;}Common *c=Common::i();LoadSliceImages *s=LoadSliceImages::i();
    c->m_renderer->RemoveActor(m_currentPoint->actor);c->m_imageViewer->GetRenderWindow()->Render();
    s->m_currentImage->cell.at<ushort>(int(m_currentPoint->p1[1]),int(m_currentPoint->p1[0]))=0;
    m_points->removeOne(m_currentPoint);m_currentPoint=nullptr;
}

void PickCellPoints::hidePointMarkers(){
    Common *c=Common::i();if(nullptr==m_points){return;}
    QListIterator<Point*> iter(*m_points);
    while(iter.hasNext()){Point *p=iter.next();c->m_renderer->RemoveActor(p->actor);}
    m_points=nullptr;
}

void PickCellPoints::updatePointsPoisition(QList<Point*> *points){
    Common *c=Common::i();LoadSliceImages *s=LoadSliceImages::i();
    if(nullptr!=m_points){
        QListIterator<Point*> iter(*m_points);
        while(iter.hasNext()){Point *p=iter.next();c->m_renderer->RemoveActor(p->actor);}
    }
    m_points=points;if(nullptr==m_points){return;}
    vtkSmartPointer<vtkImageActor> act=s->m_imageActors[0];if(nullptr==act){return;}
    double *pos=act->GetPosition(),*spacing=act->GetInput()->GetSpacing(),*origin=act->GetOrigin();
    double origin2[2]={pos[0]+origin[0],pos[1]+origin[1]},rotation=-act->GetOrientation()[2];
    QListIterator<Point*> iter(*m_points);
    while(iter.hasNext()){
        Point *ptr=iter.next();
        double p1[3]={ptr->p1[0]*spacing[0]+pos[0],ptr->p1[1]*spacing[1]+pos[1],0};
        double rotPoint[3];rotatePts(p1,origin2,-rotation,rotPoint);rotPoint[2]=0;
        ptr->actor=getNodeActor(rotPoint,s_colorPoint);
        c->m_renderer->AddActor(ptr->actor);
    }
}

