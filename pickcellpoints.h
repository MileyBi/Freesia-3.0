#ifndef PICKCELLPOINTS_H
#define PICKCELLPOINTS_H
#include <QObject>
#include <QMap>
#include <opencv2/opencv.hpp>
#include <vtkSmartPointer.h>
#include <vtkRenderer.h>

QT_BEGIN_NAMESPACE
struct Point{
    double p1[3];double p1Raw[3];vtkSmartPointer<vtkActor> actor;
    Point(){p1[2]=0;p1Raw[2]=0;}
};
QT_END_NAMESPACE

class PickCellPoints: public QObject
{
    Q_OBJECT
    Point *m_currentPoint;QList<Point*> *m_points;
    PickCellPoints();

public:
    static PickCellPoints *i(){static PickCellPoints p;return &p;}
    void addPoint(int x, int y);void selectPoint(int x, int y);
    void removeCurrentPoint();void hidePointMarkers();
    void updatePointsPoisition(QList<Point*> *points);
};
#endif // PICKCELLPOINTS_H
