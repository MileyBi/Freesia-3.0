#ifndef BUILDWARPINGFIELD_H
#define BUILDWARPINGFIELD_H
#include <QWidget>
#include <QMap>
#include <QMutexLocker>
#include <QLineF>
#include <vtkSmartPointer.h>
#include <vtkRenderer.h>

QT_BEGIN_NAMESPACE
struct Node{
    double p1[3],p2[3],p1Raw[3],p2Raw[3];int num,imageIndex;
    vtkSmartPointer<vtkActor> actors[3];
    Node():num(0),imageIndex(-1){p1[2]=0;p2[2]=1;p1Raw[2]=0;p2Raw[2]=1;}
};
QT_END_NAMESPACE

class BuildWarpingField : public QObject
{
    Q_OBJECT
    Node *m_currentNode,*m_candidateNode;
    QList<Node*> *m_nodes;

    BuildWarpingField();
    void addCandidateNode();
    void changeCurrentNode();

public:
    QMap<int,QList<Node*>*> m_allNodes;QMutex m_nodesMutex;

    static BuildWarpingField *i(){static BuildWarpingField w;return &w;}
    void addPoint(int x,int y, int flag);void selectPoint(int x, int y);
    void updateNodesPoisition(QList<Node*> *);void hideNodeMarkers();
    void removeCandidateNode();void removeCurrentNode();
    void getMarkers(int groupIndex, double offsets[2],double scales[2],double rotation,QList<QLineF> &lines);
    void importAllMarkers(const QVariantList &list);
};

#endif // BUILDWARPINGFIELD_H
