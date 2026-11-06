#pragma once
#include "cad_drawing_dimension.h"
#include <stdexcept>

namespace ForgeCad {
// Transazione locale: nessuna mutazione del foglio prima della conferma.
// Il chiamante valida la misura completa prima di accettare l'ultimo aggancio.
class DrawingDimensionRepair {
public:
    DrawingDimensionRepair(DrawingDimension original,const DrawingView &view,
        const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves,
        const DrawingAttachmentResolver &resolver,int reference=-1):original_(std::move(original)) {
        const int count=original_.references.empty()?1:int(original_.references.size());
        if(reference < -1 || reference>=count)throw std::invalid_argument("Riferimento della quota non disponibile.");
        if(reference>=0)targets_.push_back(reference);
        else {
            for(int i=0;i<count;++i)if(original_.references.empty() ||
                resolveDrawingAttachment(view,model,curves,original_.references[std::size_t(i)],resolver).status!=DrawingDimensionStatus::Ready)
                targets_.push_back(i);
            if(targets_.empty())for(int i=0;i<count;++i)targets_.push_back(i);
        }
    }
    const DrawingDimension &original()const{return original_;}
    bool complete()const{return picked_.size()==targets_.size();}
    int nextReference()const{return complete()?-1:targets_[picked_.size()];}
    bool requiresCurve()const {
        const auto m=original_.measure;
        return original_.references.empty() || m==DrawingMeasure::Angle || m==DrawingMeasure::Radius ||
            m==DrawingMeasure::Diameter || m==DrawingMeasure::ProfileRadius || m==DrawingMeasure::SectionLength;
    }
    void pick(DrawingAttachment reference) {
        if(complete())throw std::invalid_argument("Confermare prima il ricollegamento.");
        if(requiresCurve() && reference.role!=DrawingPointRole::Curve)
            throw std::invalid_argument("Scegli una curva per questo riferimento della quota.");
        if(original_.references.empty() && (!reference.axisId.isNull() || drawingDatum(reference.role)))
            throw std::invalid_argument("La quota su curva richiede un bordo del modello.");
        picked_.push_back(std::move(reference));
    }
    void back(){if(!picked_.empty())picked_.pop_back();}
    DrawingDimension candidate()const {
        auto dimension=original_;
        if(dimension.references.empty()) {
            if(!picked_.empty())dimension.section=picked_.front().curve;
        }else {
            for(std::size_t i=0;i<picked_.size();++i)dimension.references[std::size_t(targets_[i])]=picked_[i];
            dimension.section={};
            for(const auto &ref:dimension.references)if(ref.axisId.isNull() && !drawingDatum(ref.role)) {
                dimension.section=ref.curve;break;
            }
        }
        return dimension;
    }
private:
    DrawingDimension original_;
    std::vector<int> targets_;
    std::vector<DrawingAttachment> picked_;
};
}
