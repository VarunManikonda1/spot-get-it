#ifndef MAPDATA_H
#define MAPDATA_H

#include <QString>
#include <QVector>

struct MapRect {
    QString id;
    float xMin = 0.0f;
    float xMax = 0.0f;
    float yMin = 0.0f;
    float yMax = 0.0f;
    float z = 0.0f;
    float clearance = 0.0f;
};

struct MapPoint {
    QString id;
    float x = 0.0f;
    float y = 0.0f;
    float theta = 0.0f;
};

class MapConfig
{
public:
    bool loadFromYaml(const QString &path, QString *error = nullptr);
    bool isValid() const { return m_valid; }

    const QVector<MapRect> &areas() const { return m_areas; }
    const QVector<MapRect> &obstacles() const { return m_obstacles; }
    const QVector<MapPoint> &starts() const { return m_starts; }
    MapPoint endPoint() const { return m_end; }
    bool hasEndPoint() const { return m_hasEnd; }

    float minX() const { return m_minX; }
    float maxX() const { return m_maxX; }
    float minY() const { return m_minY; }
    float maxY() const { return m_maxY; }
    float centerX() const { return (m_minX + m_maxX) * 0.5f; }
    float centerY() const { return (m_minY + m_maxY) * 0.5f; }
    float widthMeters() const { return m_maxX - m_minX; }
    float heightMeters() const { return m_maxY - m_minY; }

private:
    void clear();
    void includePoint(float x, float y);
    void includeRect(const MapRect &rect);

    bool m_valid = false;
    QVector<MapRect> m_areas;
    QVector<MapRect> m_obstacles;
    QVector<MapPoint> m_starts;
    MapPoint m_end;
    bool m_hasEnd = false;
    float m_minX = 0.0f;
    float m_maxX = 0.0f;
    float m_minY = 0.0f;
    float m_maxY = 0.0f;
    bool m_haveBounds = false;
};

#endif
