#include "cad_document_read.h"

#include <algorithm>
#include <array>
#include <zlib.h>
#include <zstd.h>
#include <memory>
#include <stdexcept>

namespace ForgeCad::DocumentReadDetail {

QByteArray compress(const QByteArray &data, quint8 method, int level) {
    if (method == kZlibCompression) return qCompress(data, std::clamp(level, 0, 9));
    if (method != kZstdCompression || quint64(data.size()) > std::numeric_limits<quint32>::max())
        throw std::runtime_error("Metodo o dimensione della compressione non validi.");
    std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> context(ZSTD_createCCtx(), ZSTD_freeCCtx);
    if (!context) throw std::bad_alloc();
    ZSTD_CCtx_setParameter(context.get(), ZSTD_c_compressionLevel, std::clamp(level, 1, 9));
    ZSTD_CCtx_setParameter(context.get(), ZSTD_c_checksumFlag, 1);
    // Limita anche la memoria richiesta dal lettore streaming.
    ZSTD_CCtx_setParameter(context.get(), ZSTD_c_windowLog, 23);
    QByteArray encoded(qsizetype(ZSTD_compressBound(size_t(data.size()))) + 4, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(data.size()), encoded.data());
    const auto size = ZSTD_compress2(context.get(), encoded.data()+4, size_t(encoded.size()-4),
                                     data.constData(), size_t(data.size()));
    if (ZSTD_isError(size)) throw std::runtime_error(ZSTD_getErrorName(size));
    encoded.resize(qsizetype(size)+4);
    return encoded;
}

bool uncompress(const QByteArray &compressed, qsizetype maximumBytes, QByteArray &result, quint8 method) {
    if (maximumBytes < 0 || compressed.size() < 4
        || quint64(compressed.size() - 4) > std::numeric_limits<uInt>::max()) return false;
    const auto declared = qFromBigEndian<quint32>(compressed.constData());
    if (quint64(declared) > quint64(maximumBytes)) return false;
    if (compressed.size() == 4) {
        if (method != kZlibCompression || declared != 0) return false;
        result.clear();return true;
    }
    if (method == kZstdCompression) {
        const auto contentSize = ZSTD_getFrameContentSize(compressed.constData()+4, size_t(compressed.size()-4));
        if (contentSize == ZSTD_CONTENTSIZE_ERROR || contentSize == ZSTD_CONTENTSIZE_UNKNOWN || contentSize != declared)
            return false;
        std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), ZSTD_freeDCtx);
        if (!context || ZSTD_isError(ZSTD_DCtx_setParameter(context.get(), ZSTD_d_windowLogMax, 23))) return false;
        ZSTD_inBuffer input{compressed.constData()+4, size_t(compressed.size()-4), 0};
        QByteArray prepared;
        std::array<char, 65536> output;
        for (;;) {
            const auto remaining = qsizetype(declared) - prepared.size();
            ZSTD_outBuffer block{output.data(), size_t(std::min<qsizetype>(output.size(), remaining+1)), 0};
            const auto before = input.pos;
            const auto status = ZSTD_decompressStream(context.get(), &block, &input);
            if (ZSTD_isError(status) || block.pos > size_t(remaining)) return false;
            prepared.append(output.data(), qsizetype(block.pos));
            if (status == 0) {
                if (input.pos != input.size || prepared.size() != declared) return false;
                result = std::move(prepared);
                return true;
            }
            if (input.pos == before && block.pos == 0) return false;
        }
    }
    if (method != kZlibCompression) return false;
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
