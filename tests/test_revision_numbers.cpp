#include "cad_revision_store.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;
namespace {
void require(bool b,const char *why){if(!b)throw std::runtime_error(why);}
void ok(const QString &e){if(!e.isEmpty())throw std::runtime_error(e.toStdString());}
RevisionCheckinRequest prepare(RevisionStore &store,const QUuid &doc,const QString &label={}){RevisionCheckinRequest r;ok(store.checkout(doc,"autore",r.checkout));r.checkinId=QUuid::createUuid();r.payload="geometria";r.technicalRevision=label;return r;}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
 QTemporaryDir temporary;RevisionStore store(temporary.filePath("archive"));ok(store.initialize());QUuid doc;ok(store.createDocument("modello","model",doc));QString next;ok(store.nextTechnicalRevision(doc,next));require(next=="1","prima revisione automatica");
 auto first=prepare(store,doc);RevisionRecord a,retry;ok(store.checkin(first,a));ok(store.checkin(first,retry));require(a.technicalRevision=="1"&&retry.version.checkinId==a.version.checkinId,"ritentativo automatico non crea un numero nuovo");
 require(!store.validateTechnicalRevision(doc," 001 ").isEmpty(),"client rifiuta il duplicato numerico normalizzato prima del pendente");
 auto duplicate=prepare(store,doc," 001 ");RevisionRecord recovered;ok(store.checkin(duplicate,recovered));require(recovered.technicalRevision=="2"&&recovered.version.checkinId==duplicate.checkinId&&recovered.sha256==a.sha256,"vecchio pendente duplicato recuperato con numero nuovo e snapshot esatto");ok(store.checkin(duplicate,retry));require(retry.version.checkinId==recovered.version.checkinId&&retry.technicalRevision=="2","ritentativo del vecchio pendente conserva la ricevuta rinumerata");
 auto letter=prepare(store,doc,"A");RevisionRecord b;ok(store.checkin(letter,b));require(!store.validateTechnicalRevision(doc," a ").isEmpty(),"duplicato letterale maiuscolo/minuscolo");
 auto collision=prepare(store,doc,"5");RevisionRecord c;ok(store.checkin(collision,c));ok(store.nextTechnicalRevision(doc,next));require(next=="6","anteprima salta le etichette storiche gia' assegnate");
 auto simultaneous=prepare(store,doc);auto other=simultaneous;other.checkinId=QUuid::createUuid();RevisionRecord x,y;auto f=std::async(std::launch::async,[&]{return store.checkin(simultaneous,x);});const auto secondError=store.checkin(other,y);const auto firstError=f.get();require(firstError.isEmpty()!=secondError.isEmpty(),"due pubblicazioni concorrenti non duplicano il numero o il check-in");
 QVector<RevisionRecord> records;ok(store.history(doc,records));require(records.size()==5&&records.back().technicalRevision=="6","un solo nuovo check-in e numerazione univoca");const auto &winning=firstError.isEmpty()?simultaneous:other;ok(store.checkin(winning,retry));require(retry.version.checkinId==records.back().version.checkinId,"ritentativo della pubblicazione vincente");
 QUuid another;ok(store.createDocument("secondo","model",another));auto fresh=prepare(store,another);RevisionRecord one;ok(store.checkin(fresh,one));require(one.technicalRevision=="1","numerazione indipendente per documento");
 std::cout<<"PASS automatic revision numbers, legacy labels, normalized duplicates, concurrent publication and idempotent retries\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
