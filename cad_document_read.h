#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QList>
#include <QStringList>
#include <QPointF>
#include <QVector3D>
#include <QtEndian>
#include <limits>
#include <type_traits>
#include <utility>

namespace ForgeCad::DocumentReadDetail {

// Solo lettura Qt_6_0 su un buffer finito. Il lettore standard di QList
// riserva subito il conteggio dichiarato: qui si aggiungono solo elementi
// interamente letti. Anche i vettori annidati passano da questo controllo.
class DataStream : public QDataStream {
public:
    using QDataStream::QDataStream;

    bool readCount(quint32 &count, quint32 minimumBytes = 1) {
        if (status() != Ok) return false;
        QDataStream::operator>>(count);
        if (status() != Ok || !device() || minimumBytes == 0
            || device()->bytesAvailable() < 0 || count > quint32(std::numeric_limits<int>::max())
            || quint64(count) > quint64(device()->bytesAvailable()) / minimumBytes) {
            setStatus(ReadCorruptData);
            return false;
        }
        return true;
    }

    template<class T> DataStream &operator>>(T &value) {
        if (status() == Ok) static_cast<QDataStream &>(*this) >> value;
        return *this;
    }
    DataStream &operator>>(QByteArray &value) {
        if (checkBytes(false)) static_cast<QDataStream &>(*this) >> value;
        return *this;
    }
    DataStream &operator>>(QString &value) {
        if (checkBytes(true)) static_cast<QDataStream &>(*this) >> value;
        return *this;
    }
    DataStream &operator>>(QStringList &values) {
        return readList(static_cast<QList<QString> &>(values));
    }
    // Le mesh hanno milioni di scalari: una lettura a blocchi evita una
    // chiamata virtuale al dispositivo per ogni coordinata o indice.
    DataStream &operator>>(QList<QVector3D> &values);
    DataStream &operator>>(QList<int> &values);
    template<class T> DataStream &operator>>(QList<T> &values) {
        return readList(values);
    }
    template<class T> DataStream &readList(QList<T> &values) {
        quint32 count = 0;
        values.clear();
        // La dimensione serializzata dipende dalla precisione dello stream,
        // non da sizeof dei tipi geometrici in memoria.
        // Riserva una volta i vettori numerici/mesh validati; i contenitori
        // e gli oggetti complessi continuano invece a crescere per record.
        constexpr bool fixed = std::is_arithmetic_v<T> || std::is_same_v<T,QPointF>
            || std::is_same_v<T,QVector3D> || std::is_same_v<T,QPair<QPointF,QPointF>>;
        constexpr unsigned floatingValues = std::is_floating_point_v<T> ? 1
            : std::is_same_v<T,QPointF> ? 2 : std::is_same_v<T,QVector3D> ? 3
            : std::is_same_v<T,QPair<QPointF,QPointF>> ? 4 : 0;
        const quint32 minimumBytes = floatingValues ? floatingValues * (floatingPointPrecision() == SinglePrecision ? 4 : 8)
            : fixed ? quint32(sizeof(T)) : 1;
        if (!readCount(count, minimumBytes)) return *this;
        if constexpr (fixed) values.reserve(count);
        readRecords(values, count, [this](T &value) {
            *this >> value;
            return status() == Ok;
        });
        return *this;
    }
    template<class T, class Read>
    bool readRecords(QList<T> &values, quint32 count, Read read) {
        values.clear();
        for (quint32 index = 0; index < count; ++index) {
            T value{};
            if (status() != Ok || !read(value) || status() != Ok) {
                setStatus(ReadCorruptData);
                return false;
            }
            values.append(std::move(value));
        }
        return true;
    }

private:
    bool checkBytes(bool text) {
        if (status() != Ok) return false;
        char prefix[4];
        if (device() && device()->peek(prefix, 4) == 4) {
            const auto size = qFromBigEndian<quint32>(prefix);
            if (size == 0xffffffffu) return true; // valore nullo Qt_6_0
            if ((!text || size % 2 == 0) && quint64(size) <= quint64(device()->bytesAvailable() - 4)) return true;
        }
        setStatus(ReadCorruptData);
        return false;
    }
};

// Formato qCompress invariato; non alloca la dimensione indicata nell'header
// e interrompe inflate anche se quell'header dichiara meno dei byte reali.
inline constexpr quint8 kZlibCompression = 1, kZstdCompression = 2;
QByteArray compress(const QByteArray &data, quint8 method = kZstdCompression, int level = 9);
bool uncompress(const QByteArray &compressed, qsizetype maximumBytes, QByteArray &result,
                quint8 method = kZlibCompression);

}
