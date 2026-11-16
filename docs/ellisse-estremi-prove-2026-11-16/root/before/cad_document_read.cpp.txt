#include "cad_document_read.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <zlib.h>
#include <zstd.h>
#include <memory>
#include <stdexcept>

namespace ForgeCad::DocumentReadDetail {

namespace {
// La lunghezza viene validata sui byte disponibili prima della prenotazione.
// Buffer fisso, indipendente dalle lunghezze dichiarate nel documento.
template<class T, class Decode>
DataStream &readFixedList(DataStream &stream, QList<T> &values, quint32 width, Decode decode) {
    values.clear();
    quint32 count=0;
    if(!stream.readCount(count,width))return stream;
    values.reserve(count);
    std::array<char,65536> buffer;
    const quint32 perBlock=quint32(buffer.size())/width;
    for(quint32 offset=0;offset<count;) {
        const quint32 items=std::min(count-offset,perBlock);
        const int bytes=int(items*width);
        if(stream.readRawData(buffer.data(),bytes)!=bytes) {
            values.clear();stream.setStatus(QDataStream::ReadPastEnd);return stream;
        }
        for(quint32 i=0;i<items;++i)values.append(decode(buffer.data()+i*width));
        offset+=items;
    }
    return stream;
}

template<class Number, class Bits>
Number readFloating(const char *data,bool bigEndian) {
    const Bits bits=bigEndian?qFromBigEndian<Bits>(data):qFromLittleEndian<Bits>(data);
    Number value;static_assert(sizeof(value)==sizeof(bits));
    std::memcpy(&value,&bits,sizeof(value));return value;
}
}

DataStream &DataStream::operator>>(QList<QVector3D> &values) {
    if(version()!=QDataStream::Qt_6_0)return readList(values);
    const bool bigEndian=byteOrder()==QDataStream::BigEndian;
    if(floatingPointPrecision()==QDataStream::SinglePrecision)
        return readFixedList(*this,values,12,[=](const char *p){
            return QVector3D(readFloating<float,quint32>(p,bigEndian),
                readFloating<float,quint32>(p+4,bigEndian),readFloating<float,quint32>(p+8,bigEndian));
        });
    return readFixedList(*this,values,24,[=](const char *p){
        return QVector3D(float(readFloating<double,quint64>(p,bigEndian)),
            float(readFloating<double,quint64>(p+8,bigEndian)),float(readFloating<double,quint64>(p+16,bigEndian)));
    });
}

DataStream &DataStream::operator>>(QList<int> &values) {
    if(version()!=QDataStream::Qt_6_0)return readList(values);
    const bool bigEndian=byteOrder()==QDataStream::BigEndian;
    return readFixedList(*this,values,4,[=](const char *p){
        return int(bigEndian?qFromBigEndian<qint32>(p):qFromLittleEndian<qint32>(p));
    });
}

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
