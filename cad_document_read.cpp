#include "cad_document_read.h"

#include <algorithm>
#include <array>
#include <zlib.h>

namespace ForgeCad::DocumentReadDetail {

bool uncompress(const QByteArray &compressed, qsizetype maximumBytes, QByteArray &result) {
    if (maximumBytes < 0 || compressed.size() <= 4
        || quint64(compressed.size() - 4) > std::numeric_limits<uInt>::max()) return false;
    const auto declared = qFromBigEndian<quint32>(compressed.constData());
    if (quint64(declared) > quint64(maximumBytes)) return false;
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) return false;
    struct End { z_stream &stream; ~End() { inflateEnd(&stream); } } end{stream};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData() + 4));
    stream.avail_in = uInt(compressed.size() - 4);
    QByteArray prepared;
    std::array<char, 65536> buffer;
    for (;;) {
        // Un byte oltre il limite consente di distinguere un flusso esatto
        // da uno che tenta di espandersi ancora, senza grandi allocazioni.
        const auto remaining = qsizetype(declared) - prepared.size();
        const uInt available = remaining >= qsizetype(buffer.size()) ? uInt(buffer.size()) : uInt(remaining + 1);
        stream.next_out = reinterpret_cast<Bytef *>(buffer.data());
        stream.avail_out = available;
        const int status = inflate(&stream, Z_NO_FLUSH);
        const auto produced = qsizetype(available - stream.avail_out);
        if (produced > remaining) return false;
        prepared.append(buffer.data(), produced);
        if (status == Z_STREAM_END) {
            if (stream.avail_in != 0 || quint64(prepared.size()) != declared) return false;
            result = std::move(prepared);
            return true;
        }
        if (status != Z_OK || (produced == 0 && stream.avail_in == 0)) return false;
    }
}

}
