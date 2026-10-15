#ifndef FORGECAD_FK_HISTORY_TOOLS_H
#define FORGECAD_FK_HISTORY_TOOLS_H

#include <algorithm>
#include <stdexcept>
#include "fk_history.h"

namespace ForgeCad::Kernel::detail {

// Trasporta una storia attraverso piu' stadi che hanno gli stessi operandi
// originali. Una sola branca non dichiarata lascia scoperta l'entita'.
inline BodyHistory transportHistory(const BodyHistory &stage, const std::vector<const BodyHistory *> &sources) {
    if (sources.empty() || !sources.front()) throw std::invalid_argument("storia: mancano gli stadi sorgenti");
    const BodyHistory &root = *sources.front();
    std::vector<int> sizes[3];
    for (int k=0;k<3;++k) for (int o=0;o<root.operandCount();++o) sizes[k].push_back(root.operandSize(o,EntityKind(k)));
    if (stage.operandCount()!=int(sources.size())) throw std::invalid_argument("storia: numero di stadi incoerente");
    for (const auto *source:sources) {
        if (!source || source->operandCount()!=root.operandCount()) throw std::invalid_argument("storia: operandi incoerenti");
        for (int k=0;k<3;++k) for (int o=0;o<root.operandCount();++o)
            if(source->operandSize(o,EntityKind(k))!=sizes[k][std::size_t(o)]) throw std::invalid_argument("storia: dimensioni incoerenti");
    }
    BodyHistory result(root.operandCount(),sizes[2],sizes[1],sizes[0],stage.resultSize(EntityKind::Face),stage.resultSize(EntityKind::Edge),stage.resultSize(EntityKind::Vertex));
    for(int k=0;k<3;++k)for(int i=0;i<stage.resultSize(EntityKind(k));++i) {
        const auto kind=EntityKind(k);
        if(!stage.isDeclared(kind,i))continue;
        bool complete=true;std::vector<HistoryLink> links;
        for(const auto &link:stage.origins(kind,i)) {
            const auto &source=*sources.at(std::size_t(link.operand));
            if(!source.isDeclared(link.kind,link.index)){complete=false;break;}
            for(auto origin:source.origins(link.kind,link.index)) {
                origin.relation=std::max(origin.relation,link.relation);links.push_back(origin);
            }
        }
        if(complete)result.declareOrigins(kind,i,std::move(links));
    }
    return result;
}

inline BodyHistory indexHistory(const Body &input, const Body &result, HistoryRelation relation=HistoryRelation::Modified) {
    BodyHistory history=sameIndexHistory(input,result);
    for(int k=0;k<3;++k)for(int i=0;i<history.resultSize(EntityKind(k));++i) {
        auto links=history.origins(EntityKind(k),i);
        if(links.empty())continue;
        for(auto &link:links)link.relation=relation;
        history.declareOrigins(EntityKind(k),i,std::move(links));
    }
    return history;
}

inline BodyHistory generatedFaces(const Body &input,const Body &result,std::vector<HistoryLink> origins) {
    BodyHistory history=makeHistory({&input},result);
    for(FaceId f:result.faces())history.declareOrigins(EntityKind::Face,f.index,origins);
    return history;
}

inline FaceAliases transportLabels(const FaceAliases &labels,const BodyHistory &history) {
    FaceAliases result(std::size_t(history.resultSize(EntityKind::Face)));
    for(std::size_t i=0;i<result.size();++i) {
        if(!history.isDeclared(EntityKind::Face,int(i)))continue;
        for(const auto &link:history.origins(EntityKind::Face,int(i)))
            if(link.operand==0&&link.kind==EntityKind::Face&&link.relation!=HistoryRelation::Generated&&link.index<int(labels.size()))
                result[i].insert(result[i].end(),labels[std::size_t(link.index)].begin(),labels[std::size_t(link.index)].end());
        std::sort(result[i].begin(),result[i].end());result[i].erase(std::unique(result[i].begin(),result[i].end()),result[i].end());
    }
    return result;
}
}
#endif
