#include "Geometry.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Geo {

double polygonArea(const QPolygonF &poly)
{
    const int n = poly.size();
    if (n < 3)
        return 0.0;
    double a = 0.0;
    for (int i = 0; i < n; ++i) {
        const QPointF &p = poly[i];
        const QPointF &q = poly[(i + 1) % n];
        a += p.x() * q.y() - q.x() * p.y();
    }
    return std::abs(a) * 0.5;
}

double polylineLength(const QPolygonF &pts, bool closed)
{
    double len = 0.0;
    for (int i = 1; i < pts.size(); ++i)
        len += QLineF(pts[i - 1], pts[i]).length();
    if (closed && pts.size() > 2)
        len += QLineF(pts.last(), pts.first()).length();
    return len;
}

QPointF polygonCentroid(const QPolygonF &poly)
{
    const int n = poly.size();
    if (n == 0)
        return {};
    double a = 0, cx = 0, cy = 0;
    for (int i = 0; i < n; ++i) {
        const QPointF &p = poly[i];
        const QPointF &q = poly[(i + 1) % n];
        const double cross = p.x() * q.y() - q.x() * p.y();
        a  += cross;
        cx += (p.x() + q.x()) * cross;
        cy += (p.y() + q.y()) * cross;
    }
    if (std::abs(a) < 1e-9) {
        QPointF s;
        for (const QPointF &p : poly) s += p;
        return s / n;
    }
    a *= 0.5;
    return {cx / (6.0 * a), cy / (6.0 * a)};
}

// Distance along the ray (origin + t*dir) to the segment, or +inf.
static double raySegment(const QPointF &o, const QPointF &d, const QLineF &s)
{
    const QPointF a = s.p1();
    const QPointF e = s.p2() - s.p1();
    const double denom = d.x() * e.y() - d.y() * e.x();
    if (std::abs(denom) < 1e-12)
        return std::numeric_limits<double>::infinity();
    const QPointF ao = a - o;
    const double t = (ao.x() * e.y() - ao.y() * e.x()) / denom;   // along ray
    const double u = (ao.x() * d.y() - ao.y() * d.x()) / denom;   // along segment
    if (t < 1e-6 || u < -1e-9 || u > 1.0 + 1e-9)
        return std::numeric_limits<double>::infinity();
    return t;
}

QPolygonF visibilityPolygon(const QPointF &src, double radius,
                            const QVector<QLineF> &obstacles, int circleSamples)
{
    QPolygonF result;
    if (radius <= 0)
        return result;

    // Keep only the obstacles that can matter
    QVector<QLineF> segs;
    segs.reserve(obstacles.size());
    const QRectF box(src.x() - radius, src.y() - radius, 2 * radius, 2 * radius);
    for (const QLineF &l : obstacles) {
        const QRectF lb = QRectF(l.p1(), l.p2()).normalized().adjusted(-1, -1, 1, 1);
        if (lb.intersects(box))
            segs.append(l);
    }

    std::vector<double> angles;
    angles.reserve(circleSamples + segs.size() * 6);
    for (int i = 0; i < circleSamples; ++i)
        angles.push_back(-M_PI + 2.0 * M_PI * i / circleSamples);
    const double eps = 1e-4;
    for (const QLineF &l : segs) {
        for (const QPointF &p : {l.p1(), l.p2()}) {
            const QPointF v = p - src;
            if (v.x() * v.x() + v.y() * v.y() > radius * radius * 1.0001)
                continue;
            const double a = std::atan2(v.y(), v.x());
            angles.push_back(a - eps);
            angles.push_back(a);
            angles.push_back(a + eps);
        }
    }
    for (double &a : angles) {
        while (a < -M_PI) a += 2 * M_PI;
        while (a >= M_PI) a -= 2 * M_PI;
    }
    std::sort(angles.begin(), angles.end());

    result.reserve(int(angles.size()));
    for (double a : angles) {
        const QPointF d(std::cos(a), std::sin(a));
        double best = radius;
        for (const QLineF &l : segs) {
            const double t = raySegment(src, d, l);
            if (t < best)
                best = t;
        }
        result << src + d * best;
    }
    return result;
}

QPolygonF attenuatedPolygon(const QPointF &src, double reach, double lossPerFace,
                            const QVector<QLineF> &obstacles, int samples)
{
    QPolygonF result;
    if (reach <= 0)
        return result;
    QVector<QLineF> segs;
    const QRectF box(src.x() - reach, src.y() - reach, 2 * reach, 2 * reach);
    for (const QLineF &l : obstacles)
        if (QRectF(l.p1(), l.p2()).normalized().adjusted(-1, -1, 1, 1).intersects(box))
            segs.append(l);
    std::vector<double> hits;
    for (int i = 0; i < samples; ++i) {
        const double a = -M_PI + 2.0 * M_PI * i / samples;
        const QPointF d(std::cos(a), std::sin(a));
        hits.clear();
        for (const QLineF &l : segs) {
            const double t = raySegment(src, d, l);
            if (t < reach)
                hits.push_back(t);
        }
        std::sort(hits.begin(), hits.end());
        double r = reach;
        for (double t : hits) {
            if (t >= r)
                break;
            r -= lossPerFace;               // the signal is weaker behind every wall face
            if (r <= t) {
                r = t;
                break;
            }
        }
        result << src + d * r;
    }
    return result;
}

double lightRadius(double beamDeg, double mountHeightCm, double workPlaneCm)
{
    const double h = std::max(1.0, mountHeightCm - workPlaneCm);
    const double half = qDegreesToRadians(std::clamp(beamDeg, 1.0, 178.0) / 2.0);
    return h * std::tan(half);
}

QColor colorFromKelvin(int kelvin)
{
    // Tanner Helland approximation
    const double t = std::clamp(kelvin, 1000, 40000) / 100.0;
    double r, g, b;
    if (t <= 66) {
        r = 255;
        g = 99.4708025861 * std::log(t) - 161.1195681661;
        b = t <= 19 ? 0 : 138.5177312231 * std::log(t - 10) - 305.0447927307;
    } else {
        r = 329.698727446 * std::pow(t - 60, -0.1332047592);
        g = 288.1221695283 * std::pow(t - 60, -0.0755148492);
        b = 255;
    }
    auto c = [](double v) { return int(std::clamp(v, 0.0, 255.0)); };
    return QColor(c(r), c(g), c(b));
}

QString formatMeters(double cm, int decimals)
{
    return QString::number(cm / kCmPerM, 'f', decimals) + " m";
}

QString formatArea(double cm2)
{
    return QString::number(cm2 / (kCmPerM * kCmPerM), 'f', 2) + QStringLiteral(" m²");
}

QPointF constrainAngle(const QPointF &from, const QPointF &to)
{
    const QLineF l(from, to);
    const double len = l.length();
    if (len < 1e-9)
        return to;
    const double ang = std::round(l.angle() / 45.0) * 45.0;
    QLineF r(from, from + QPointF(len, 0));
    r.setAngle(ang);
    return r.p2();
}

} // namespace Geo
