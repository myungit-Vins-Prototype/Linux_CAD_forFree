#include "fk_topology.h"
#include "fk_history.h"
#include "fk_cancel.h"
#include <limits>
#include <stdexcept>

namespace ForgeCad::Kernel {
Body Body::collect(const std::vector<const Body *> &bodies, BodyHistory *history) {
    if (bodies.empty()) throw std::invalid_argument("Collezione: nessun corpo.");
    Body result;
    struct Offsets { int vertex, edge, fin, loop, face, shell, region; };
    std::vector<Offsets> offsets;
    const auto shifted=[](auto id,int offset) { if(id.valid())id.index+=offset;return id; };
    const auto size=[](const auto &v) {
        if (v.size()>std::size_t(std::numeric_limits<int>::max()/2))
            throw std::length_error("Collezione: troppe entita'.");
        return int(v.size());
    };
    for (const auto *body:bodies) {
        cancellationPoint();
        if (!body) throw std::invalid_argument("Collezione: corpo nullo.");
        const Offsets o{size(result.vertices_),size(result.edges_),size(result.fins_),size(result.loops_),
                        size(result.faces_),size(result.shells_),size(result.regions_)-1};
        offsets.push_back(o);
        for (const auto &v:body->vertices_) result.vertices_.push_back(v);
        for (auto e:body->edges_) {
            e.forward=shifted(e.forward,o.fin);e.backward=shifted(e.backward,o.fin);result.edges_.push_back(std::move(e));
        }
        for (auto f:body->fins_) {
            f.loop=shifted(f.loop,o.loop);f.edge=shifted(f.edge,o.edge);f.vertex=shifted(f.vertex,o.vertex);
            f.next=shifted(f.next,o.fin);f.previous=shifted(f.previous,o.fin);result.fins_.push_back(std::move(f));
        }
        for (auto l:body->loops_) {
            l.face=shifted(l.face,o.face);l.first=shifted(l.first,o.fin);l.isolatedVertex=shifted(l.isolatedVertex,o.vertex);result.loops_.push_back(std::move(l));
        }
        for (auto f:body->faces_) {
            f.shell=shifted(f.shell,o.shell);for(auto &l:f.loops)l=shifted(l,o.loop);result.faces_.push_back(std::move(f));
        }
        for (auto s:body->shells_) {
            if(s.region.index>0)s.region=shifted(s.region,o.region);
            for(auto &f:s.faces)f=shifted(f,o.face);
            result.shells_.push_back(std::move(s));
        }
        for (const auto shell:body->region(body->exteriorRegion()).shells)
            result.regions_[0].shells.push_back(shifted(shell,o.shell));
        for (std::size_t i=1;i<body->regions_.size();++i) {
            auto r=body->regions_[i];for(auto &s:r.shells)s=shifted(s,o.shell);result.regions_.push_back(std::move(r));
        }
    }
    if (history) {
        *history=makeHistory(bodies,result);
        for(std::size_t operand=0;operand<bodies.size();++operand) {
            const auto &o=offsets[operand];const auto &b=*bodies[operand];
            const auto add=[&](EntityKind kind,int index,int offset) {
                history->declareOrigins(kind,index+offset,{{int(operand),kind,index,HistoryRelation::Same}});
            };
            for(auto f:b.faces())add(EntityKind::Face,f.index,o.face);
            for(auto e:b.edges())add(EntityKind::Edge,e.index,o.edge);
            for(auto v:b.vertices())add(EntityKind::Vertex,v.index,o.vertex);
        }
    }
    return result;
}
}
