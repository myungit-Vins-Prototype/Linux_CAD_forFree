#pragma once
#include "../cad_document_read.h"
#ifdef Q_OS_LINUX
#include <sys/resource.h>
#endif

namespace {
QByteArray documentReadFixture(const QByteArray &payload, quint16 version = 50, const QByteArray &cache = {}) {
    const auto compressed = qCompress(payload, 9);
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out.writeRawData("FCAD", 4); out << version << quint8(1);
    if (version >= 14) out << quint32(compressed.size());
    out.writeRawData(compressed.constData(), compressed.size());
    out.writeRawData(cache.constData(), cache.size());
    return bytes;
}
QByteArray documentCorruptCountFixture() {
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << quint32(0) << quint32(16384);
    payload.append(QByteArray(16384, '\0'));
    return documentReadFixture(payload);
}
void documentReadMemoryTest() {
    const auto corrupt = documentCorruptCountFixture();
    DocumentState destination;
    const auto identity = destination.version.documentId;
#ifdef Q_OS_LINUX
    rusage before{}, after{};
    require(getrusage(RUSAGE_SELF, &before) == 0, "misura memoria iniziale");
#endif
    const auto error = ForgeCad::decodeDocument(corrupt, destination);
    require(!error.isEmpty() && destination.version.documentId == identity && destination.extrusions.isEmpty(),
            "conteggio corrotto rifiutato senza modificare la destinazione");
#ifdef Q_OS_LINUX
    require(getrusage(RUSAGE_SELF, &after) == 0, "misura memoria finale");
    std::cout << "parser corrupt: " << corrupt.size() << " byte, maxRSS delta "
              << after.ru_maxrss - before.ru_maxrss << " KiB" << std::endl;
    // Il codice precedente alloca circa 50 MiB per questi 58 byte. Il margine
    // lascia spazio al runtime senza accettare l'allocazione anticipata.
    require(after.ru_maxrss - before.ru_maxrss < 16 * 1024, "nessuna allocazione massiva dai conteggi corrotti");
#endif
}
void documentSketchReferenceNameTests() {
    using namespace ForgeCad;
    DocumentState original;
    SketchObject sketch;
    sketch.segments = {{{0, 0}, {1, 0}}};
    sketch.constraints = {-1};
    sketch.segmentLengths = {0};
    sketch.segmentAngles = {-1};
    sketch.support = std::make_shared<GeometryRef>();
    sketch.support->kind = 5;
    sketch.support->index = 0;
    sketch.support->featureId = 17;
    sketch.support->point.subshape = 3;
    sketch.support->point.name = QStringLiteral("appoggio/faccia/à");
    SketchConstraint constraint;
    constraint.type = ConstraintType::Pierce;
    constraint.first = {0, 0, 0};
    auto pierce = std::make_shared<SketchPierceData>();
    pierce->surface = *sketch.support;
    pierce->surface.point.name = QStringLiteral("pierce/faccia/Ω");
    pierce->name = QStringLiteral("Etichetta visibile");
    constraint.pierce = pierce;
    sketch.geometricConstraints.append(constraint);
    original.sketches.append(sketch);
    // Presente anche uno schizzo senza appoggio: nessun nome extra va
    // consumato quando il riferimento opzionale non esiste.
    original.sketches.append(SketchObject{});
    QByteArray definition;
    for (bool cache : {false, true}) {
        QByteArray bytes;
        require(encodeDocument(original, bytes, cache).isEmpty(), "codifica nomi topologici degli schizzi");
        DocumentState restored;
        require(decodeDocument(bytes, restored).isEmpty() && restored.sketches.size() == 2,
                "lettura nomi topologici degli schizzi");
        const auto &saved = restored.sketches.front();
        require(saved.support && saved.support->point.name == sketch.support->point.name
                && saved.support->featureId == 17 && saved.support->point.subshape == 3
                && saved.geometricConstraints.size() == 1 && saved.geometricConstraints.front().pierce,
                "appoggio conserva identita' e nome topologico al round trip");
        const auto &savedPierce = *saved.geometricConstraints.front().pierce;
        require(savedPierce.surface.point.name == pierce->surface.point.name && savedPierce.name == pierce->name
                && !restored.sketches.back().support,
                "Pierce conserva nome topologico distinto dall'etichetta e appoggio assente resta assente");
        QDataStream header(bytes);
        header.skipRawData(4); quint16 version = 0; header >> version;
        require(version == 54, "nuovi nomi protetti dal formato 54");
        header.skipRawData(1); quint32 size = 0; header >> size;
        definition = qUncompress(bytes.mid(11, size));
    }
    for (const auto &name : {sketch.support->point.name, pierce->surface.point.name}) {
        QByteArray encodedName;
        QDataStream nameStream(&encodedName, QIODevice::WriteOnly);
        nameStream.setVersion(QDataStream::Qt_6_0); nameStream << name;
        auto corrupt = definition;
        const auto at = corrupt.indexOf(encodedName);
        require(at >= 0, "nome presente nella definizione serializzata");
        qToBigEndian<quint32>(0x7ffffffe, corrupt.data() + at);
        DocumentState destination;
        const auto identity = destination.version.documentId;
        require(!decodeDocument(documentReadFixture(corrupt, 54), destination).isEmpty()
                && destination.version.documentId == identity && destination.sketches.isEmpty(),
                "nome topologico malformato rifiutato senza stato parziale");
    }
    // Fixture prodotta dal writer 53 precedente: supporto e Pierce avevano
    // soltanto l'identita' numerica, mentre l'etichetta Pierce era presente.
    const auto legacy = QByteArray::fromBase64(
        "RkNBRAA1AQAAAIUAAAJIeNpjYGBg/A8EDBDAyIAG7D+gi8DVI6tl3P8BhwHYABZD8aqDORBkuASygv9IAK9JjFDM+h8NEHABI5paCYYUhkyGYoYChhyGRIZKBl2GNCCdzJCKbBa6+Wj2MGJxG6XuIjlIYWA/Dn1bl5uw3LP0VZi6aHrGdeP1BwiZCwAILnmm");
    DocumentState restored;
    require(decodeDocument(legacy, restored).isEmpty() && restored.sketches.size() == 1
            && restored.sketches.front().support && restored.sketches.front().support->featureId == 1
            && restored.sketches.front().support->point.name.isEmpty()
            && restored.sketches.front().geometricConstraints.size() == 1,
            "appoggio del formato 53 ancora leggibile senza inventare nomi");
    const auto &legacyPierce = restored.sketches.front().geometricConstraints.front().pierce;
    require(legacyPierce && legacyPierce->surface.point.name.isEmpty() && legacyPierce->name == "display-face",
            "Pierce del formato 53 conserva i campi storici");
    QByteArray legacyAgain;
    require(encodeDocumentDefinition53ForHash(restored, legacyAgain).isEmpty() && legacyAgain == legacy,
            "encoder storico per hash riproduce esattamente i byte del writer 53 precedente");
    QByteArray retained("non modificare");
    require(!encodeDocumentDefinition53ForHash(original, retained).isEmpty() && retained == "non modificare",
            "encoder storico non scarta i nuovi nomi per produrre un falso hash equivalente");
}
// Oracolo indipendente: QDataStream scalare, due endian e due precisioni.
// I vettori attraversano piu' blocchi; inclusi bit speciali e indici negativi.
void documentBulkMeshReadTests() {
    using ForgeCad::DocumentReadDetail::DataStream;
    QVector<QVector3D> points;QVector<int> indices;
    for(int i=0;i<20003;++i) {
        points.append(QVector3D(float(i)*.125f,float(-i)*.25f,float(i%17)));
        indices.append(i%2?-i:i);
    }
    for(float value:{0.0f,-0.0f,std::numeric_limits<float>::denorm_min(),
            std::numeric_limits<float>::max(),std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        points.append(QVector3D(value,value,value));
    indices << std::numeric_limits<int>::min() << std::numeric_limits<int>::max();
    for(auto order:{QDataStream::BigEndian,QDataStream::LittleEndian})
        for(auto precision:{QDataStream::SinglePrecision,QDataStream::DoublePrecision}) {
            const auto configure=[&](QDataStream &stream){stream.setVersion(QDataStream::Qt_6_0);
                stream.setByteOrder(order);stream.setFloatingPointPrecision(precision);};
            QByteArray bytes;QDataStream out(&bytes,QIODevice::WriteOnly);configure(out);
            out << points << indices << QVector<QVector<QVector3D>>{points,{},points} << qint32(12345);
            QVector<QVector3D> expected,actual;QVector<int> expectedIds,actualIds;
            QVector<QVector<QVector3D>> expectedNested,actualNested;qint32 expectedTail=0,actualTail=0;
            QDataStream reference(bytes);configure(reference);
            reference >> expected >> expectedIds >> expectedNested >> expectedTail;
            DataStream reader(bytes);configure(reader);
            reader >> actual >> actualIds >> actualNested >> actualTail;
            require(reference.status()==QDataStream::Ok && reader.status()==QDataStream::Ok
                && reader.atEnd() && actualTail==expectedTail,"lettura a blocchi: confini e sentinella");
            QByteArray left,right;QDataStream a(&left,QIODevice::WriteOnly),b(&right,QIODevice::WriteOnly);
            configure(a);configure(b);a << expected << expectedIds << expectedNested;
            b << actual << actualIds << actualNested;
            require(left==right,"lettura a blocchi identica a Qt inclusi NaN, zeri con segno ed endian");
            for(int cut:{0,3,4,5,65535,65536}) {
                DataStream truncated(bytes.left(cut));configure(truncated);QVector<QVector3D> result;
                truncated >> result;
                require(truncated.status()!=QDataStream::Ok && result.isEmpty(),"vettore troncato rifiutato prima della prenotazione");
            }
            QByteArray empty;QDataStream emptyOut(&empty,QIODevice::WriteOnly);configure(emptyOut);
            emptyOut << QVector<QVector3D>{} << QVector<int>{};
            DataStream emptyIn(empty);configure(emptyIn);emptyIn >> actual >> actualIds;
            require(emptyIn.status()==QDataStream::Ok && emptyIn.atEnd() && actual.isEmpty() && actualIds.isEmpty(),
                "vettori vuoti cancellano la destinazione precedente");
        }
}

void documentReadTests() {
    using namespace ForgeCad;
    using DocumentReadDetail::DataStream;
    documentSketchReferenceNameTests();
    documentBulkMeshReadTests();
    const auto rejected = [](const QByteArray &bytes, const DocumentReadLimits &limits = {}) {
        DocumentState destination;
        destination.sketches.append(SketchObject{}); destination.sketches[0].name = "da conservare";
        const auto identity = destination.version.documentId;
        require(!decodeDocument(bytes, destination, limits).isEmpty(), "documento corrotto o oltre budget rifiutato");
        require(destination.version.documentId == identity && destination.sketches.size() == 1
                && destination.sketches[0].name == "da conservare", "destinazione atomica su errore");
    };
    rejected(documentCorruptCountFixture());
    // QList annidati, stringhe e blob non possono prenotare le lunghezze
    // indicate da quattro byte se il contenuto non esiste.
    for (quint32 length : {quint32(1024), quint32(0x7fffffff), quint32(0xfffffffe)}) {
        QByteArray data; QDataStream out(&data, QIODevice::WriteOnly); out << length;
        data.append(QByteArray(32, '\0'));
        { DataStream in(data); QVector<int> values; in >> values;
          require(in.status() != QDataStream::Ok && values.isEmpty(), "vettore enorme e corto rifiutato"); }
        { DataStream in(data); QString text; in >> text;
          require(in.status() != QDataStream::Ok, "stringa troncata rifiutata prima dell'allocazione"); }
        { DataStream in(data); QByteArray bytes; in >> bytes;
          require(in.status() != QDataStream::Ok, "blob troncato rifiutato prima dell'allocazione"); }
        QByteArray nested; QDataStream nestedOut(&nested, QIODevice::WriteOnly);
        nestedOut << quint32(1); nested.append(data);
        DataStream in(nested); QVector<QVector<QVector3D>> lines; in >> lines;
        require(in.status() != QDataStream::Ok && lines.isEmpty(), "vettori annidati controllati");
    }
    {
        QByteArray data; QDataStream out(&data, QIODevice::WriteOnly); out << quint32(1) << quint8('x');
        DataStream in(data); QString text; in >> text;
        require(in.status() != QDataStream::Ok, "UTF-16 con numero dispari di byte rifiutato");
    }
    {
        const QVector<QVector<QVector3D>> lines{{{1,2,3},{4,5,6}}, {}, {{7,8,9}}};
        for (const auto precision : {QDataStream::SinglePrecision, QDataStream::DoublePrecision}) {
            QByteArray data; QDataStream out(&data, QIODevice::WriteOnly);
            out.setVersion(QDataStream::Qt_6_0); out.setFloatingPointPrecision(precision);
            out << lines << QStringList{QString{}, QStringLiteral("à Ω"), QStringLiteral("")} << QByteArray{};
            DataStream in(data); in.setVersion(QDataStream::Qt_6_0); in.setFloatingPointPrecision(precision);
            QVector<QVector<QVector3D>> restored; QStringList names; QByteArray blob("non nullo");
            in >> restored >> names >> blob;
            require(in.status() == QDataStream::Ok && in.atEnd() && restored == lines
                    && names.size() == 3 && names[0].isNull() && names[1] == QStringLiteral("à Ω")
                    && names[2].isEmpty() && !names[2].isNull() && blob.isNull(), "formato Qt preservato, float/double e null");
        }
    }
    {
        QByteArray data; QDataStream out(&data, QIODevice::WriteOnly); out << quint32(100) << QByteArray(100, '\0');
        DataStream in(data); quint32 count = 0;
        require(!in.readCount(count, 24), "record fissi richiedono tutti i byte prima del resize");
    }
    {
        const QByteArray plain(1024 * 1024, 'x');
        const auto compressed = qCompress(plain, 9);
        QByteArray result("immutato");
        require(!DocumentReadDetail::uncompress(compressed, plain.size()-1, result) && result == "immutato", "budget della decompressione");
        require(DocumentReadDetail::uncompress(compressed, plain.size(), result) && result == plain, "budget esatto accettato");
        for (quint32 hint : {quint32(0), quint32(16), quint32(0xffffffff)}) {
            auto forged = compressed; qToBigEndian<quint32>(hint, forged.data()); result = "immutato";
            require(!DocumentReadDetail::uncompress(forged, 64, result) && result == "immutato", "limite reale anche con header falso");
        }
        for (const auto &broken : {compressed.left(compressed.size()-1), compressed + QByteArray("extra"), QByteArray("bad")})
            require(!DocumentReadDetail::uncompress(broken, plain.size(), result), "zlib troncato o con dati extra rifiutato");
    }
    // Tutti i formati storici: i campi assenti restano assenti, nessuna
    // dipendenza dal nuovo limite o dal layout in memoria delle strutture.
    for (quint16 version = 1; version <= kDocumentFormatVersion; ++version) {
        QByteArray payload; QDataStream out(&payload, QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_6_0);
        out << quint32(0) << quint32(0);
        if (version >= 4) for (double value : {1.,0.,0.,0.,0.,1.,0.,-1.,0.}) out << value;
        if (version >= 19) out << quint32(0);
        if (version >= 26) out << qint32(LengthUnit::Millimeter);
        if (version >= 50) out << QUuid::createUuid() << QUuid{} << QUuid{};
        DocumentState restored; require(decodeDocument(documentReadFixture(payload, version), restored).isEmpty(), "formato storico vuoto leggibile");
    }
    for (int extras : {0,1,2}) {
        QByteArray payload; QDataStream out(&payload, QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_6_0);
        out << quint32(0) << quint32(1) << QStringLiteral("primitiva formato 5")
            << qint32(-1) << qint32(0) << double(1) << true << qint32(-1) << qint32(BodyFeature::Primitive) << qint32(0) << double(360)
            << qint32(PrimitiveKind::Box) << qint32(0);
        for (double value : {0.,0.,0.,1.,2.,3.}) out << value;
        out << false << double(.2) << quint32(0) << qint32(-1) << qint32(-1)
            << qint32(0) << double(0) << double(0) << double(0) << true;
        if (extras >= 1) out << double(2) << qint32(0) << double(0) << double(0) << double(0);
        if (extras >= 2) out << qint32(0) << double(.3) << false;
        for (double value : {1.,0.,0.,0.,0.,1.,0.,-1.,0.}) out << value;
        DocumentState restored; const auto error = decodeDocument(documentReadFixture(payload,5),restored);
        require(error.isEmpty() && restored.extrusions.size() == 1 && restored.extrusions[0].primitive.size[2] == 3,
                "tutte le varianti storiche del formato 5 leggibili");
    }
    DocumentState original;
    SketchObject sketch; sketch.name = "schizzo à"; sketch.segments = {{{0,0},{3,0}},{{3,0},{3,4}}};
    original.sketches.append(sketch);
    ExtrusionObject body; body.feature = BodyFeature::Primitive; body.primitive.kind = PrimitiveKind::Box;
    body.forgeBody = std::make_shared<const Kernel::Body>(Kernel::makeBox(Kernel::Frame3{},3,4,5));
    forgeTessellate(*body.forgeBody,0,body.display); original.extrusions.append(body);
    QByteArray encoded; require(encodeDocument(original,encoded,true).isEmpty(), "codifica moderna con B-rep e mesh");
    DocumentState restored;
    require(decodeDocument(encoded,restored).isEmpty() && restored.sketches[0].segments == sketch.segments
            && restored.extrusions[0].forgeBody && !restored.extrusions[0].display.vertices.isEmpty()
            && Kernel::writeBodyBinary(*restored.extrusions[0].forgeBody) == Kernel::writeBodyBinary(*body.forgeBody),
            "round trip di definizione, geometria e mesh senza perdite");
    QDataStream header(encoded); header.skipRawData(7); quint32 length = 0; header >> length;
    const auto payload = qUncompress(encoded.mid(11,length));
    DocumentReadLimits limits;
    limits.definitionBytes = payload.size()-1; rejected(encoded,limits);
    limits.definitionBytes = payload.size();
    require(decodeDocument(encoded,restored,limits).isEmpty(), "definizione sul limite leggibile");
    limits.cacheBytes = 0;
    require(decodeDocument(encoded,restored,limits).isEmpty() && !restored.extrusions[0].forgeBody
            && restored.extrusions[0].feature == BodyFeature::Primitive, "cache oltre budget ignorata, definizione conservata");
    limits.fileBytes = encoded.size()-1; rejected(encoded,limits);
    QTemporaryDir directory; require(directory.isValid(), "directory test lettura");
    const auto path = directory.filePath("lettura.prt"); QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(encoded) == encoded.size(), "fixture file"); file.close();
    require(!loadDocumentFile(path,restored,limits).isEmpty(), "limite controllato prima della lettura del file");
    limits.fileBytes = encoded.size(); limits.cacheBytes = DocumentReadLimits{}.cacheBytes;
    require(loadDocumentFile(path,restored,limits).isEmpty() && restored.extrusions[0].forgeBody, "file esattamente al limite leggibile");
    for (qsizetype missing : {qsizetype(1),qsizetype(4),qsizetype(8)}) rejected(encoded.left(11+length-missing));
    // La regressione RSS gira in un processo nuovo: non eredita il picco dei
    // test precedenti e non usa soglie sul consumo totale dell'applicazione.
    QProcess process; process.start(QCoreApplication::applicationFilePath(), {"--document-read-memory"});
    require(process.waitForFinished(30000), "probe memoria terminato");
    std::cout << process.readAllStandardOutput().constData();
    if (process.exitCode() != 0) std::cerr << process.readAllStandardError().constData();
    require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "regressione memoria end-to-end");
}
}
