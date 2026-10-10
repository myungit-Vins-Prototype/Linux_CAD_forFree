#include "cad_snapshot_chunks.h"

#include <QIODevice>
#include <array>
#include <cstdint>
#include <limits>

namespace ForgeCad {
namespace {
constexpr qsizetype kMinimum = 2048;
constexpr qsizetype kMaximum = 32768;

const std::array<std::uint64_t, 256> &gear() {
    static const auto values = [] {
        std::array<std::uint64_t, 256> result{};
        std::uint64_t seed = 0x123456789abcdef;
        for (auto &value : result) {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            value = seed;
        }
        return result;
    }();
    return values;
}
}

void SnapshotChunkWriter::write(const QByteArray &data) {
    if (data.size() > std::numeric_limits<qint32>::max()) {
        stream_.setStatus(QDataStream::WriteFailed);
        return;
    }
    QVector<qsizetype> ends;
    qsizetype begin = 0;
    std::uint64_t hash = 0;
    for (qsizetype index = 0; index < data.size(); ++index) {
        hash = (hash << 1) + gear()[static_cast<unsigned char>(data[index])];
        const qsizetype size = index + 1 - begin;
        if ((size >= kMinimum && ((hash & 8191) == 0 || size >= kMaximum)) || index + 1 == data.size()) {
            ends.append(index + 1);
            begin = index + 1;
            hash = 0;
        }
    }
    stream_ << quint32(data.size()) << quint32(ends.size());
    begin = 0;
    for (qsizetype end : ends) {
        const QByteArray chunk = data.mid(begin, end - begin);
        begin = end;
        const auto found = chunks_.constFind(chunk);
        if (found != chunks_.cend()) {
            stream_ << found.value();
        } else {
            stream_ << qint32(-1) << chunk;
            chunks_.insert(chunk, qint32(chunks_.size()));
        }
    }
}

QByteArray SnapshotChunkReader::read() {
    quint32 size = 0, count = 0;
    stream_ >> size >> count;
    const auto corrupt = [&]() {
        stream_.setStatus(QDataStream::ReadCorruptData);
        return QByteArray();
    };
    if (stream_.status() != QDataStream::Ok || size > quint32(std::numeric_limits<qint32>::max())
        || count > size / kMinimum + 1 || (size == 0) != (count == 0)
        || !stream_.device() || quint64(count) * sizeof(qint32) > quint64(stream_.device()->bytesAvailable()))
        return corrupt();
    QByteArray data;
    for (quint32 index = 0; index < count; ++index) {
        qint32 reference = -2;
        stream_ >> reference;
        QByteArray chunk;
        if (reference == -1) {
            stream_ >> chunk;
            if (chunk.isEmpty() || chunk.size() > kMaximum) return corrupt();
            chunks_.append(chunk);
        } else {
            if (reference < 0 || reference >= chunks_.size()) return corrupt();
            chunk = chunks_.at(reference);
        }
        if (stream_.status() != QDataStream::Ok || chunk.size() > qsizetype(size) - data.size()) return corrupt();
        data.append(chunk);
    }
    if (data.size() != size) return corrupt();
    return data;
}
}
