#include "common.h"
#include "buildwarpingfield.h"
#include "loadsliceimages.h"
#include "loadbrainatlases.h"
#include <vtkLineSource.h>
#include <math.h>

static double s_colorCandidate[3]={255,0,0};
static double s_colorNode[3]={0,255,0};
static double s_colorSelected[3]={255,0,0};

BuildWarpingField::BuildWarpingField():m_currentNode(nullptr),m_candidateNode(nullptr){}

void BuildWarpingField::addPoint(int x, int y, int flag){
    Common *c=Common::i();LoadBrainAtlases *m=LoadBrainAtlases::i();LoadSliceImages *s=LoadSliceImages::i();
    if(!s->m_bEdited||nullptr==m_nodes||nullptr==s->m_imageActors[0]||nullptr==m->m_modelActors[0]){return;}
    double picked1[3];c->getPointPosition(x,y,picked1);
    if(nullptr==m_candidateNode){m_candidateNode=new Node;}
    if(0==m_candidateNode->num){
        changeCurrentNode();
        vtkSmartPointer<vtkImageActor> act=s->m_imageActors[0];
        double *pos=act->GetPosition(),*spacing=act->GetInput()->GetSpacing(),*origin=act->GetOrigin();
        double origin2[2]={pos[0]+origin[0],pos[1]+origin[1]};
        double unRotPoint[3];rotatePts(picked1,origin2,-act->GetOrientation()[2],unRotPoint);
        double picked2[2]={(unRotPoint[0]-pos[0])/spacing[0],(unRotPoint[1]-pos[1])/spacing[1]};
        m_candidateNode->p1Raw[0]=picked1[0];m_candidateNode->p1Raw[1]=picked1[1];
        m_candidateNode->p1[0]=picked2[0];m_candidateNode->p1[1]=picked2[1];
        m_candidateNode->num=1;m_candidateNode->imageIndex=s->m_imageIndex;
        vtkSmartPointer<vtkActor> actor0=getNodeActor(picked1,s_colorCandidate);
        c->m_renderer->AddActor(actor0);m_candidateNode->actors[0]=actor0;
    }
    else{
        if(m_candidateNode->imageIndex!=s->m_imageIndex){emit Common::i()->showMessage("Points should be contained in the same image");return;}
        c->m_renderer->RemoveActor(m_candidateNode->actors[1]);c->m_renderer->RemoveActor(m_candidateNode->actors[2]);
        m_candidateNode->p2[0]=picked1[0];m_candidateNode->p2[1]=picked1[1];m_candidateNode->num=2;
        vtkSmartPointer<vtkLineSource> line=vtkSmartPointer<vtkLineSource>::New();
        line->SetPoint1(m_candidateNode->p1Raw);line->SetPoint2(m_candidateNode->p2);
        vtkSmartPointer<vtkActor> actor1=getActor(line->GetOutputPort(),s_colorCandidate);
        c->m_renderer->AddActor(actor1);m_candidateNode->actors[1]=actor1;
        vtkSmartPointer<vtkActor> actor2=getNodeActor(picked1,s_colorCandidate);
        c->m_renderer->AddActor(actor2);m_candidateNode->actors[2]=actor2;
        if(flag==-1){addCandidateNode();}
    }
    c->m_imageViewer->GetRenderWindow()->Render();
}

void BuildWarpingField::addCandidateNode(){
    if(nullptr!=m_candidateNode&&m_candidateNode->num==2&&nullptr!=m_nodes){
        m_nodesMutex.lock();m_nodes->append(m_candidateNode);m_nodesMutex.unlock();
        m_currentNode=m_candidateNode; m_candidateNode=nullptr;}
}

void BuildWarpingField::removeCandidateNode(){
    if(nullptr==m_candidateNode){return;}Common *c=Common::i();
    c->m_renderer->RemoveActor(m_candidateNode->actors[0]);c->m_renderer->RemoveActor(m_candidateNode->actors[1]);
    c->m_renderer->RemoveActor(m_candidateNode->actors[2]);c->m_imageViewer->GetRenderWindow()->Render();m_candidateNode=nullptr;
}

void BuildWarpingField::removeCurrentNode(){
    if(nullptr==m_currentNode){return;}Common *c=Common::i();
    c->m_renderer->RemoveActor(m_currentNode->actors[0]);c->m_renderer->RemoveActor(m_currentNode->actors[1]);
    c->m_renderer->RemoveActor(m_currentNode->actors[2]);c->m_imageViewer->GetRenderWindow()->Render();
    m_nodes->removeOne(m_currentNode);m_currentNode=nullptr;
}

void BuildWarpingField::changeCurrentNode(){
    if(nullptr!=m_currentNode){for(int i=0;i<3;i++){m_currentNode->actors[i]->GetProperty()->SetColor(s_colorNode);}}
    m_currentNode=nullptr;Common::i()->m_imageViewer->GetRenderWindow()->Render();
}

void BuildWarpingField::selectPoint(int x, int y){
    Common *c=Common::i();if(nullptr==m_nodes||m_nodes->empty()){return;}
    double picked[3];c->getPointPosition(x,y,picked);double minDist=DBL_MAX;Node *closestNode=nullptr;
    QListIterator<Node*> iter(*m_nodes);
    while(iter.hasNext()){Node *p=iter.next();
        double d=calcuDistance(picked[0],picked[1],p->p1Raw[0],p->p1Raw[1],p->p2[0],p->p2[1]);
        if(d<minDist){minDist=d;closestNode=p;}}
    if(nullptr!=m_currentNode&&m_currentNode!=closestNode){for(int i=0;i<3;i++){m_currentNode->actors[i]->GetProperty()->SetColor(s_colorNode);}}
    if(nullptr!=closestNode){for(int i=0;i<3;i++){closestNode->actors[i]->GetProperty()->SetColor(s_colorSelected);}}
    m_currentNode=closestNode;c->m_imageViewer->GetRenderWindow()->Render();
}

void BuildWarpingField::hideNodeMarkers(){
    Common *c=Common::i();if(nullptr==m_nodes){return;}
    QListIterator<Node*> iter(*m_nodes);
    while(iter.hasNext()){Node *p=iter.next();
    c->m_renderer->RemoveActor(p->actors[0]);c->m_renderer->RemoveActor(p->actors[1]);c->m_renderer->RemoveActor(p->actors[2]);}
    m_nodes=nullptr;
}

void BuildWarpingField::updateNodesPoisition(QList<Node *> *nodes){
    Common *c=Common::i();LoadSliceImages *s=LoadSliceImages::i();
    if(nullptr!=m_nodes){
        QListIterator<Node*> iter(*m_nodes);
        while(iter.hasNext()){Node *p=iter.next();
            c->m_renderer->RemoveActor(p->actors[0]);c->m_renderer->RemoveActor(p->actors[1]);c->m_renderer->RemoveActor(p->actors[2]);}
    }
    m_nodes=nodes;if(nullptr==m_nodes){return;}
    vtkSmartPointer<vtkImageActor> act=s->m_imageActors[0];if(nullptr==act){return;}
    double *pos=act->GetPosition(),*spacing=act->GetInput()->GetSpacing(),*origin=act->GetOrigin();
    double origin2[2]={pos[0]+origin[0],pos[1]+origin[1]},rotation=-act->GetOrientation()[2];
    QListIterator<Node*> iter(*m_nodes);
    while(iter.hasNext()){
        Node *p=iter.next();
        double p1[3]={p->p1[0]*spacing[0]+pos[0],p->p1[1]*spacing[1]+pos[1],0};
        double rotPoint[3];rotatePts(p1,origin2,-rotation,rotPoint);rotPoint[2]=10;
        bool bSelected=(p==m_currentNode);
        p->actors[0]=getNodeActor(rotPoint,bSelected?s_colorSelected:s_colorNode);
        c->m_renderer->AddActor(p->actors[0]);
        vtkSmartPointer<vtkLineSource> line=vtkSmartPointer<vtkLineSource>::New();
        line->SetPoint1(rotPoint);line->SetPoint2(p->p2);
        vtkSmartPointer<vtkActor> actor1=getActor(line->GetOutputPort(),bSelected?s_colorSelected:s_colorNode);
        p->actors[1]=actor1;c->m_renderer->AddActor(actor1);
        double point2[3]={p->p2[0],p->p2[1],0};
        p->actors[2]=getNodeActor(point2,bSelected?s_colorSelected:s_colorNode);
        c->m_renderer->AddActor(p->actors[2]);
    }
}

void BuildWarpingField::getMarkers(int groupIndex, double offsets[2],double scales[2],double rotation,QList<QLineF> &lines){
    QMutexLocker locker(&m_nodesMutex);
    QList<Node*> *nodes=m_allNodes.value(groupIndex,nullptr);if(nullptr==nodes){return;}
    vtkSmartPointer<vtkImageActor> act=LoadSliceImages::i()->m_imageActors[0];if(nullptr==act){return;}

    const double *pos=act->GetPosition(),*spacing=act->GetInput()->GetSpacing(),*origin=act->GetOrigin();
    double pos1[3]={pos[0],pos[1],pos[2]},spacing1[3]={spacing[0],spacing[1],spacing[2]};
    pos1[0]+=offsets[0];pos1[1]+=offsets[1];spacing1[0]*=scales[0];spacing1[1]*=scales[1];
    double origin2[2]={pos1[0]+origin[0],pos1[1]+origin[1]};

    QListIterator<Node*> iter(*nodes);
    while(iter.hasNext()){
        Node *p=iter.next();
        double unRotPoint[3];rotatePts(p->p2,origin2,-rotation,unRotPoint);
        double p2[2]={(unRotPoint[0]-pos1[0])/spacing1[0],(unRotPoint[1]-pos1[1])/spacing1[1]};

        QLineF line;line.setP1(QPointF(p->p1[0],p->p1[1]));line.setP2(QPointF(p2[0],p2[1]));
        lines.append(line);
    }
}

inline void string2Point(const QVariant &v,double pos[3]){
    QStringList list=v.toString().split(" ");if(list.length()!=2){return;}
    pos[0]=list[0].toDouble();pos[1]=list[1].toDouble();
}

void BuildWarpingField::importAllMarkers(const QVariantList &list){
    QList<Node*> *lastNodes=nullptr;int lastGroupIndex=-1;
    foreach(QVariant v,list){
        QVariantMap v1=v.toMap();Node *p=new Node;p->imageIndex=v1["image_index"].toInt();
        string2Point(v1["image_point"],p->p1);string2Point(v1["atlas_point"],p->p2);

        int groupIndex=v1["group_index"].toInt();if(groupIndex<0){continue;}
        if(groupIndex==lastGroupIndex){lastNodes->append(p);continue;}

        lastNodes=m_allNodes.value(groupIndex,nullptr);lastGroupIndex=groupIndex;
        if(nullptr==lastNodes){lastNodes=new QList<Node*>();m_allNodes[groupIndex]=lastNodes;}
        lastNodes->append(p);
    }
}
