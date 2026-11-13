#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <QColor>
#include <QLineF>
#include <QPolygonF>
#include <QString>
#include <QVector>

namespace Geo {

// Scene unit is 1 cm.
constexpr double kCmPerM = 100.0;

double polygonArea(const QPolygonF &poly);          // [cm^2], always >= 0
double polylineLength(const QPolygonF &pts, bool closed = false);
QPointF polygonCentroid(const QPolygonF &poly);

// Visibility ("light / sound does not pass through walls") polygon seen from 'source'
// within 'radius', blocked by 'obstacles' (wall edges, scene coordinates).
QPolygonF visibilityPolygon(const QPointF &source, double radius,
                            const QVector<QLineF> &obstacles, int circleSamples = 180);

// Radio coverage (Wi-Fi): like visibilityPolygon, but walls do not stop the ray, they shorten it.
// Every wall face crossed costs lossPerFace (cm of range). 'reach' = range for the wanted signal level.
QPolygonF attenuatedPolygon(const QPointF &source, double reach, double lossPerFace,
                            const QVector<QLineF> &obstacles, int samples = 240);

// Light cone radius on the calculation plane: (mount - plane) * tan(beam/2)
double lightRadius(double beamDeg, double mountHeightCm, double workPlaneCm);

// Approximate RGB of a black-body color temperature (1000..10000 K)
QColor colorFromKelvin(int kelvin);

QString formatMeters(double cm, int decimals = 2);
QString formatArea(double cm2);

// Orthogonal / 45 degree constraint relative to 'from'
QPointF constrainAngle(const QPointF &from, const QPointF &to);

// Open wall centre line with the last point pushed 'by' further along the last segment.
// Stroked with a flat cap: the wall starts exactly at the first point and ends with a square end.
QPolygonF extendWallEnd(QPolygonF pts, double by);

} // namespace Geo

#endif // GEOMETRY_H
