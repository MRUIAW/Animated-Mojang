#pragma once
#include "scene/CaveAnimation.h"

namespace animated_mojang::scene {
namespace cave_occlusion_detail {
struct Plane {
    double x, y, c;
    double at(glm::vec2 p) const { return x*p.x+y*p.y+c; }
};
inline Plane depthPlane(ProjectedTriangle const& t) {
    const auto a=t.points[0],b=t.points[1]-a,c=t.points[2]-a;
    const double area=static_cast<double>(b.x)*c.y-static_cast<double>(b.y)*c.x;
    const double db=static_cast<double>(t.inverseW[1])-t.inverseW[0],dc=static_cast<double>(t.inverseW[2])-t.inverseW[0];
    const double x=(db*c.y-dc*b.y)/area,y=(dc*b.x-db*c.x)/area;
    return {x,y,t.inverseW[0]-x*a.x-y*a.y};
}
struct Vertex {
    glm::vec2 point;
    float inverseW;
    glm::vec2 uvOverW;
    glm::vec3 colorOverW;
};
using Polygon=std::vector<Vertex>;
inline Polygon clip(Polygon const& input,Plane plane) {
    Polygon result;result.reserve(input.size()+1);
    if(input.empty())return result;
    auto previous=input.back();auto d0=plane.at(previous.point);
    for(auto const& current:input) {
        const auto d1=plane.at(current.point);
        if((d0>=0)!=(d1>=0)) {
            const auto f=static_cast<float>(d0/(d0-d1));
            result.push_back({previous.point+(current.point-previous.point)*f,
                previous.inverseW+(current.inverseW-previous.inverseW)*f,
                previous.uvOverW+(current.uvOverW-previous.uvOverW)*f,
                previous.colorOverW+(current.colorOverW-previous.colorOverW)*f});
        }
        if(d1>=0)result.push_back(current);
        previous=current;d0=d1;
    }
    return result;
}
inline double area(Polygon const& p) {
    double sum=0;
    for(std::size_t i=0;i<p.size();++i) {
        auto a=p[i].point,b=p[(i+1)%p.size()].point;
        sum+=static_cast<double>(a.x)*b.y-static_cast<double>(a.y)*b.x;
    }
    return sum*.5;
}
inline Polygon polygon(ProjectedTriangle const& t) {
    Polygon result;result.reserve(3);
    for(std::size_t i=0;i<3;++i)result.push_back({t.points[i],t.inverseW[i],t.uv[i]*t.inverseW[i],t.colors[i]*t.inverseW[i]});
    return result;
}
struct Bounds {
    glm::vec2 min,max;
    bool overlaps(Bounds const& b) const {
        return max.x>b.min.x+.0001f&&b.max.x>min.x+.0001f&&max.y>b.min.y+.0001f&&b.max.y>min.y+.0001f;
    }
};
inline Bounds bounds(ProjectedTriangle const& t) {
    return {glm::min(t.points[0],glm::min(t.points[1],t.points[2])),glm::max(t.points[0],glm::max(t.points[1],t.points[2]))};
}
inline Bounds bounds(Polygon const& p) {
    Bounds result{p[0].point,p[0].point};
    for(auto const& v:p){result.min=glm::min(result.min,v.point);result.max=glm::max(result.max,v.point);}
    return result;
}
// Partition a convex polygon into the portions OUTSIDE a convex occluder.
// The remaining intersection is hidden. Pieces stay convex and preserve UV/W.
inline bool subtract(Polygon const& subject,Polygon const& cutter,std::vector<Polygon>& output) {
    if(!bounds(subject).overlaps(bounds(cutter))) {output.push_back(subject);return false;}
    auto inside=subject;
    std::vector<Polygon> outsidePieces;
    const double orientation=area(cutter)>=0?1:-1;
    for(std::size_t i=0;i<cutter.size()&&inside.size()>=3;++i) {
        const auto a=cutter[i].point,b=cutter[(i+1)%cutter.size()].point-a;
        // Clipping through an existing corner can round the intersection to
        // that corner. A zero-length edge has no inside/outside half-plane;
        // treating both as inclusive would incorrectly retain the full piece.
        if(glm::dot(b,b)<1.e-10f)continue;
        const Plane edge{-b.y*orientation,b.x*orientation,
            (static_cast<double>(b.y)*a.x-static_cast<double>(b.x)*a.y)*orientation};
        auto outside=clip(inside,{-edge.x,-edge.y,-edge.c});
        if(outside.size()>=3&&std::abs(area(outside))>.0001)outsidePieces.push_back(std::move(outside));
        inside=clip(inside,edge);
    }
    if(inside.size()<3||std::abs(area(inside))<=.0001) {output.push_back(subject);return false;}
    for(auto& piece:outsidePieces)output.push_back(std::move(piece));
    return true;
}
} // namespace cave_occlusion_detail
struct CaveOcclusionStats { std::size_t changed=0, fragments=0, fallbacks=0; };
// Resolve depth conflicts involving opaque lava. General geometry still uses
// painter order. Use the affine 1/W depth planes, not triangle-average Z.
inline CaveOcclusionStats resolveCaveOcclusion(std::vector<ProjectedTriangle>& triangles) {
    using namespace cave_occlusion_detail;
    CaveOcclusionStats stats;
    std::vector<std::size_t> lava,terrain;
    std::vector<Bounds> boxes;std::vector<Plane> depths;
    boxes.reserve(triangles.size());depths.reserve(triangles.size());
    for(std::size_t i=0;i<triangles.size();++i) {
        boxes.push_back(bounds(triangles[i]));depths.push_back(depthPlane(triangles[i]));
        if(triangles[i].texture&0x20000u)continue; // Alpha sprites cannot act as solid cutters.
        terrain.push_back(i);
        if(triangles[i].texture&animatedLavaFlag)lava.push_back(i);
    }
    if(lava.empty())return stats;
    const auto nearFirst=[&](auto a,auto b){return triangles[a].distance<triangles[b].distance;};
    std::sort(lava.begin(),lava.end(),nearFirst);std::sort(terrain.begin(),terrain.end(),nearFirst);
    // Most terrain triangles do not touch any lava on screen. Build each
    // overlapping pair once and avoid polygon allocations for those triangles.
    std::vector<std::vector<std::size_t>> relevant(triangles.size());
    // Broad phase: query only screen tiles touched by each lava face. Exact
    // bounds/depth clipping below is unchanged; sprites remain excluded.
    constexpr int gridSize=16;
    std::array<std::vector<std::size_t>,gridSize*gridSize> tiles;
    glm::vec2 extent{1};
    for(auto i:terrain)extent=glm::max(extent,boxes[i].max);
    const auto tileRange=[&](Bounds box) {
        const auto cell=[&](glm::vec2 point) {
            const auto scaled=point/extent*static_cast<float>(gridSize);
            return std::array<int,2>{std::clamp(static_cast<int>(scaled.x),0,gridSize-1),
                std::clamp(static_cast<int>(scaled.y),0,gridSize-1)};
        };
        const auto a=cell(box.min),b=cell(box.max);
        return std::array<int,4>{a[0],a[1],b[0],b[1]};
    };
    std::vector<std::size_t> rank(triangles.size()),seen(triangles.size(),triangles.size());
    for(std::size_t order=0;order<terrain.size();++order) {
        const auto i=terrain[order];rank[i]=order;
        const auto range=tileRange(boxes[i]);
        for(int y=range[1];y<=range[3];++y)for(int x=range[0];x<=range[2];++x)
            tiles[y*gridSize+x].push_back(i);
    }
    std::vector<std::size_t> candidates;
    for(auto j:lava) {
        candidates.clear();const auto range=tileRange(boxes[j]);
        for(int y=range[1];y<=range[3];++y)for(int x=range[0];x<=range[2];++x)
            for(auto i:tiles[y*gridSize+x])if(seen[i]!=j) {
                seen[i]=j;
                if(i!=j&&boxes[i].overlaps(boxes[j]))candidates.push_back(i);
            }
        // Preserve the original candidate order, including equal-depth ties.
        std::sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return rank[a]<rank[b];});
        for(auto i:candidates) {
            relevant[i].push_back(j);
            if(!(triangles[i].texture&animatedLavaFlag))relevant[j].push_back(i);
        }
    }
    for(auto& list:relevant)std::sort(list.begin(),list.end(),nearFirst);
    std::vector<ProjectedTriangle> result;result.reserve(triangles.size()+256);
    for(std::size_t i=0;i<triangles.size();++i) {
        auto const& source=triangles[i];
        if(relevant[i].empty()) { result.push_back(source);continue; }
        auto const& candidates=relevant[i];
        std::vector<Polygon> pieces{polygon(source)};bool changed=false,fallback=false;
        for(auto j:candidates) {
            const Plane closer{depths[j].x-depths[i].x,depths[j].y-depths[i].y,depths[j].c-depths[i].c-1.e-7};
            if(std::none_of(triangles[j].points.begin(),triangles[j].points.end(),[&](auto p){return closer.at(p)>0;}))continue;
            auto cutter=clip(polygon(triangles[j]),closer);
            if(cutter.size()<3||std::abs(area(cutter))<.0001)continue;
            std::vector<Polygon> next;
            bool cut=false;
            for(auto const& piece:pieces)cut=subtract(piece,cutter,next)||cut;
            // Bound pathological fragmentation without unbounded menu work.
            if(next.size()>96) {fallback=true;++stats.fallbacks;break;}
            pieces=std::move(next);changed=changed||cut;
            if(pieces.empty())break;
        }
        if(!changed||fallback) {result.push_back(source);continue;}
        ++stats.changed;
        for(auto const& piece:pieces)for(std::size_t j=1;j+1<piece.size();++j) {
            const std::array<cave_occlusion_detail::Vertex,3> vertices{piece[0],piece[j],piece[j+1]};
            const auto a=vertices[1].point-vertices[0].point,b=vertices[2].point-vertices[0].point;
            if(std::abs(a.x*b.y-a.y*b.x)<.0001f)continue;
            auto triangle=source;triangle.distance=0;
            for(std::size_t k=0;k<3;++k) {
                auto const& v=vertices[k];triangle.points[k]=v.point;triangle.inverseW[k]=v.inverseW;
                triangle.uv[k]=v.uvOverW/v.inverseW;triangle.colors[k]=v.colorOverW/v.inverseW;
                triangle.distance+=1.f/v.inverseW/3.f;
            }
            triangle.color=triangle.colors[0];result.push_back(triangle);++stats.fragments;
        }
    }
    triangles=std::move(result);return stats;
}
} // namespace animated_mojang::scene
