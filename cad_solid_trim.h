#pragma once
#include "cad_types.h"
namespace ForgeCad {
void buildSolidTrim(ExtrusionObject &feature,int owner,const QVector<ExtrusionObject> &bodies);
}
