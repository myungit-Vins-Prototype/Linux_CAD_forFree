#include "cad_document_read.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <zlib.h>

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
