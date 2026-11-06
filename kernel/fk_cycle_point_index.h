#ifndef FORGECAD_FK_CYCLE_POINT_INDEX_H
#define FORGECAD_FK_CYCLE_POINT_INDEX_H

#include "fk_math.h"
#include "fk_cancel.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ForgeCad::Kernel::detail {

// Indice dei medesimi vertici UV gia' usati dall'ordinamento dei cicli.
// Non approssima curve: restituisce il minimo discreto esatto, escludendo
// il ciclo interrogato. La traslazione conserva il trattamento dei periodi.
class CyclePointIndex {
public:
    struct Point { Vec2 uv; int cycle; };
    explicit CyclePointIndex(std::vector<Point> points):points_(std::move(points)) {
        for(const auto &p:points_)if(!isFinite(p.uv))throw std::domain_error("CyclePointIndex: punto UV non finito");
        if(!points_.empty())build(0,points_.size());
    }
    double nearest(const Vec2 &point,int excluded,const Vec2 &shift={}) const {
        if(nodes_.empty())return 1e300;
        double best2=std::numeric_limits<double>::infinity();
        std::vector<int> pending{0};
        while(!pending.empty()) {
            cancellationPoint();const auto &node=nodes_[std::size_t(pending.back())];pending.pop_back();
            if((node.cycle>=0&&node.cycle==excluded)||lowerBound2(node,point,shift)>best2)continue;
            if(node.left<0) {
                for(std::size_t i=node.begin;i<node.end;++i)if(points_[i].cycle!=excluded)
                    best2=std::min(best2,squaredNorm(point-(points_[i].uv+shift)));
            } else {
                const double a=lowerBound2(nodes_[std::size_t(node.left)],point,shift);
                const double b=lowerBound2(nodes_[std::size_t(node.right)],point,shift);
                // L'ultimo inserito e' visitato per primo.
                if(a<=b){pending.push_back(node.right);pending.push_back(node.left);}
                else {pending.push_back(node.left);pending.push_back(node.right);}
            }
        }
        return std::min(1e300,std::sqrt(best2));
    }
private:
    struct Node { Vec2 lo,hi;std::size_t begin,end;int left=-1,right=-1,cycle=-1; };
    int build(std::size_t begin,std::size_t end) {
        cancellationPoint();Node node;node.begin=begin;node.end=end;node.lo=node.hi=points_[begin].uv;node.cycle=points_[begin].cycle;
        for(std::size_t i=begin+1;i<end;++i) {
            for(int a=0;a<2;++a){node.lo[a]=std::min(node.lo[a],points_[i].uv[a]);node.hi[a]=std::max(node.hi[a],points_[i].uv[a]);}
            if(points_[i].cycle!=node.cycle)node.cycle=-1;
        }
        const int index=int(nodes_.size());nodes_.push_back(node);
        if(end-begin>16) {
            const int axis=node.hi.x()-node.lo.x()>=node.hi.y()-node.lo.y()?0:1;
            const auto middle=begin+(end-begin)/2;
            std::nth_element(points_.begin()+begin,points_.begin()+middle,points_.begin()+end,[&](const Point&a,const Point&b){return a.uv[axis]<b.uv[axis];});
            const int left=build(begin,middle),right=build(middle,end);
            nodes_[std::size_t(index)].left=left;nodes_[std::size_t(index)].right=right;
        }
        return index;
    }
    static double lowerBound2(const Node &node,const Vec2 &point,const Vec2 &shift) {
        const double magnitude=std::max({1.,std::fabs(point.x()),std::fabs(point.y()),std::fabs(shift.x()),std::fabs(shift.y()),
            std::fabs(node.lo.x()),std::fabs(node.lo.y()),std::fabs(node.hi.x()),std::fabs(node.hi.y())});
        const double margin=128*std::numeric_limits<double>::epsilon()*magnitude;
        Vec2 outside;
        for(int a=0;a<2;++a)outside[a]=std::max({node.lo[a]+shift[a]-point[a]-margin,0.,point[a]-(node.hi[a]+shift[a])-margin});
        return squaredNorm(outside);
    }
    std::vector<Point> points_;
    std::vector<Node> nodes_;
};
}
#endif
