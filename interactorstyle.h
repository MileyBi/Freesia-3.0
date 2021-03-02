#ifndef INTERACTORSTYLE_H
#define INTERACTORSTYLE_H
#include "common.h"
#include "loadbrainatlases.h"
#include "buildwarpingfield.h"
#include "pickcellpoints.h"
#include <vtkInteractorStyleImage.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkLookupTable.h>

class ImageInteractorStyle:public vtkInteractorStyleImage{
    ImageInteractorStyle(){}
    void getMousePos(int &x,int &y){x=this->Interactor->GetEventPosition()[0];y=this->Interactor->GetEventPosition()[1];}
public:
    static ImageInteractorStyle* New();int flag=-1;
    vtkTypeMacro(ImageInteractorStyle, vtkInteractorStyleImage)
protected:  
    void OnChar() override{}

    void OnMouseMove() override{
        if(Common::i()->enableMouseEvent){return;}
        if(Common::i()->isSliceWarping&&Common::i()->m_modelType==2){
            if(flag==1){int x,y;getMousePos(x,y);BuildWarpingField::i()->addPoint(x,y,flag);}
            else{int x,y;getMousePos(x,y);BuildWarpingField::i()->selectPoint(x,y);}
        }
        else if(Common::i()->isCellPicking&&Common::i()->m_modelType==2){int x,y;getMousePos(x,y);PickCellPoints::i()->selectPoint(x,y);}
        else if(Interactor->GetShiftKey()){vtkInteractorStyleImage::OnMouseMove();return;}
        else{if(!LoadBrainAtlases::i()->m_isAtlasShow){int x,y;getMousePos(x,y);LoadBrainAtlases::i()->hoverRegion(x,y);Common *c=Common::i();emit c->showMessage(LoadBrainAtlases::i()->hoverRegionName);}
        }
    }

    void OnLeftButtonUp() override{if(VTKIS_PAN==State){EndPan();}}
    void OnLeftButtonDown() override{
        if(Common::i()->enableMouseEvent){return;}
        if(Common::i()->isSliceWarping&&Common::i()->m_modelType==2){flag=-flag;int x,y;getMousePos(x,y);BuildWarpingField::i()->addPoint(x,y,flag);}
        else if(Common::i()->isCellPicking&&Common::i()->m_modelType==2){int x,y;getMousePos(x,y);PickCellPoints::i()->addPoint(x,y);}
        else if(1==GetInteractor()->GetRepeatCount()){emit Common::i()->toggleFullscreen();}
        else if(Interactor->GetShiftKey()){StartPan();}
        else if(Interactor->GetControlKey()&&!LoadBrainAtlases::i()->m_isAtlasShow){int x,y;getMousePos(x,y);LoadBrainAtlases::i()->selectMultiRegion(x,y);}
        else{if(!LoadBrainAtlases::i()->m_isAtlasShow){int x,y;getMousePos(x,y);LoadBrainAtlases::i()->selectRegion(x,y);}}
    }
    void OnRightButtonDown() override{
        if(Common::i()->enableMouseEvent){return;}
        if(Common::i()->isSliceWarping&&Common::i()->m_modelType==2){
            if(flag==1){BuildWarpingField::i()->removeCandidateNode();flag=-1;}
            else{BuildWarpingField::i()->removeCurrentNode();}
        }
        else if(Common::i()->isCellPicking&&Common::i()->m_modelType==2){PickCellPoints::i()->removeCurrentPoint();}
        else if(1==GetInteractor()->GetRepeatCount()){Common::i()->resetViewer();}
        else{vtkInteractorStyleImage::OnRightButtonDown();}
    }
};
vtkStandardNewMacro(ImageInteractorStyle);

#endif // INTERACTORSTYLE_H
