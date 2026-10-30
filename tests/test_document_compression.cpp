#include "cad_document_read.h"
#include <QtEndian>
#include <cstdio>

int main() {
    using namespace ForgeCad::DocumentReadDetail;
    int failures = 0;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { ++failures; std::fprintf(stderr, "FALLITO: %s\n", message); }
    };
    QByteArray plain;
    for (int i=0; i<300000; ++i) plain.append(char((i*31) ^ (i>>7)));
    for (quint8 method : {kZlibCompression, kZstdCompression}) {
        for (const auto &data : {QByteArray{}, QByteArray("CAD\0esatto", 10), plain}) {
            const auto encoded = compress(data, method, 9);
            QByteArray output("conserva");
            check(uncompress(encoded, data.size(), output, method) && output == data, "round trip e limite esatto");
            for (const auto &bad : {encoded.left(encoded.size()-1), encoded+QByteArray("x"), encoded+encoded}) {
                output="conserva";
                check(!uncompress(bad, data.size(), output, method) && output=="conserva", "troncamento/code/frame extra atomici");
            }
            if (data.isEmpty()) continue;
            output="conserva";
            check(!uncompress(encoded,data.size()-1,output,method) && output=="conserva", "budget insufficiente");
            for (quint32 length : {0u, 1u, 0x7fffffffu}) {
                auto forged=encoded; qToBigEndian(length,forged.data()); output="conserva";
                check(!uncompress(forged,data.size(),output,method) && output=="conserva", "header falso non aggira il limite");
            }
            auto damaged=encoded; damaged[damaged.size()-1]^=char(1); output="conserva";
            check(!uncompress(damaged,data.size(),output,method) && output=="conserva", "checksum corrotto");
        }
    }
    QByteArray result("conserva");
    check(!uncompress(compress(plain),plain.size(),result,99) && result=="conserva", "metodo sconosciuto");
    return failures ? 1 : 0;
}
