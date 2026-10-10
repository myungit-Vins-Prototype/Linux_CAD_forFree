#include "cad_snapshot_chunks.h"
#include <QBuffer>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace ForgeCad;
static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        std::mt19937 random(20261010);
        QByteArray bytes(1024 * 1024, Qt::Uninitialized);
        for (char &byte : bytes) byte = char(random());
        const QVector<QByteArray> inputs = {QByteArray(), bytes, bytes, QByteArray("inserted prefix") + bytes,
                                           bytes.left(800000) + QByteArray("edited middle") + bytes.mid(800000), QByteArray("short")};
        QByteArray encoded;
        QDataStream out(&encoded, QIODevice::WriteOnly);
        SnapshotChunkWriter writer(out);
        for (const auto &input : inputs) writer.write(input);
        require(out.status() == QDataStream::Ok && encoded.size() < 2 * bytes.size(), "deduplication across shifted snapshots");
        QDataStream in(encoded);
        SnapshotChunkReader reader(in);
        for (const auto &input : inputs) require(reader.read() == input && in.status() == QDataStream::Ok, "exact binary round trip");
        require(in.atEnd(), "all records consumed");
        for (qint32 reference : {-2, 0, 100}) {
            QByteArray invalid;
            QDataStream badOut(&invalid, QIODevice::WriteOnly);
            badOut << quint32(1) << quint32(1) << reference;
            QDataStream badIn(invalid);
            SnapshotChunkReader badReader(badIn);
            require(badReader.read().isEmpty() && badIn.status() != QDataStream::Ok, "invalid reference rejected");
        }
        for (auto pair : {std::pair<quint32, quint32>{0, 1}, {1, 0}, {1, 2}, {0xffffffff, 1}}) {
            QByteArray invalid;
            QDataStream badOut(&invalid, QIODevice::WriteOnly);
            badOut << pair.first << pair.second;
            QDataStream badIn(invalid);
            SnapshotChunkReader badReader(badIn);
            require(badReader.read().isEmpty() && badIn.status() != QDataStream::Ok, "invalid lengths rejected");
        }
        QByteArray one;
        QDataStream oneOut(&one, QIODevice::WriteOnly);
        SnapshotChunkWriter oneWriter(oneOut);
        oneWriter.write(bytes);
        for (int missing : {1, 4, 8, 100}) {
            QDataStream truncated(one.left(one.size() - missing));
            SnapshotChunkReader truncatedReader(truncated);
            require(truncatedReader.read().isEmpty() && truncated.status() != QDataStream::Ok, "truncated record rejected");
        }
        std::cout << "PASS snapshot chunks: exact round trip, shifted duplicates, corrupt input" << std::endl;
    } catch (const std::exception &error) { std::cerr << error.what() << std::endl; return 1; }
}
