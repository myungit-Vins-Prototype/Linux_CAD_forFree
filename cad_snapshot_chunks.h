#ifndef FORGECAD_SNAPSHOT_CHUNKS_H
#define FORGECAD_SNAPSHOT_CHUNKS_H

#include <QByteArray>
#include <QDataStream>
#include <QHash>
#include <QVector>
#include <limits>

namespace ForgeCad {

// Condivisione senza perdita dei blocchi ripetuti fra gli snapshot della
// storia. I confini dipendono dai byte, cosi' un inserimento non disallinea
// tutti i blocchi successivi. La geometria binaria ricostruita resta identica.
class SnapshotChunkWriter {
public:
    explicit SnapshotChunkWriter(QDataStream &stream) : stream_(stream) {}
    void write(const QByteArray &data);
private:
    QDataStream &stream_;
    QHash<QByteArray, qint32> chunks_;
};

class SnapshotChunkReader {
public:
    explicit SnapshotChunkReader(QDataStream &stream, qsizetype maximumBytes = std::numeric_limits<qint32>::max())
        : stream_(stream), remainingBytes_(maximumBytes) {}
    QByteArray read();
private:
    QDataStream &stream_;
    QVector<QByteArray> chunks_;
    qsizetype remainingBytes_;
};

}
#endif
