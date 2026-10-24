#pragma once
#include "cad_drawing_revisions.h"
namespace ForgeCad {
QString inspectDrawingRelease(RevisionStore &store,const RevisionReleaseRequest &request,RevisionReleaseManifest &manifest);
QString publishDrawingRelease(RevisionStore &store,const RevisionReleaseRequest &request,RevisionReleaseManifest &manifest);
QString readDrawingRelease(RevisionStore &store,const QUuid &id,RevisionReleaseManifest &manifest);
}
