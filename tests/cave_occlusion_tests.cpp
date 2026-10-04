#include "scene/CaveCamera.h"
#include "scene/CaveModel.h"
#include "scene/CaveOcclusion.h"
#include "scene/UiCaveBatch.h"
#include <chrono>
#include <iostream>
using namespace animated_mojang::scene;
void require(bool ok,char const* message) {if(!ok)throw std::runtime_error(message);}
struct Hit {float depth=0;bool lava=false;bool found=false;};
Hit at(ProjectedTriangle const& t,glm::vec2 p) {
    const auto a=t.points[0],b=t.points[1]-a,c=t.points[2]-a,q=p-a;
    const double determinant=static_cast<double>(b.x)*c.y-static_cast<double>(b.y)*c.x;
    if(std::abs(determinant)<.000001)return {};
    const auto v=(static_cast<double>(q.x)*c.y-static_cast<double>(q.y)*c.x)/determinant;
    const auto w=(static_cast<double>(b.x)*q.y-static_cast<double>(b.y)*q.x)/determinant;
    if(v<0||w<0||v+w>1)return {};
    return {static_cast<float>((1-v-w)*t.inverseW[0]+v*t.inverseW[1]+w*t.inverseW[2]),bool(t.texture&animatedLavaFlag),true};
}
Hit nearest(std::span<ProjectedTriangle const> scene,glm::vec2 p) {
    Hit result;
    for(auto const& t:scene) {auto hit=at(t,p);if(hit.found&&hit.depth>result.depth)result=hit;}
    return result;
}
Hit painter(std::span<ProjectedTriangle const> scene,glm::vec2 p) {
    Hit result;
    for(auto const& t:scene) {auto hit=at(t,p);if(hit.found)result=hit;}
    return result;
}
bool wrong(Hit reference,Hit result) {
    return reference.found&&(!result.found||((reference.lava||result.lava)&&result.depth<reference.depth-2.e-7f));
}
int main() {
    try {
        ProjectedTriangle wall{{glm::vec2{0,0},{10,0},{0,10}},{1,1,1},1};
        wall.inverseW={1,1,1};wall.uv={glm::vec2{0,0},{1,0},{0,1}};
        auto duplicate=cave_occlusion_detail::polygon(wall);
        duplicate.insert(duplicate.begin()+2,duplicate[1]);
        std::vector<cave_occlusion_detail::Polygon> difference;
        require(cave_occlusion_detail::subtract(cave_occlusion_detail::polygon(wall),duplicate,difference)&&difference.empty(),
            "duplicate clip corner cannot retain occluded subject");
        auto lava=wall;lava.texture=animatedLavaFlag;lava.inverseW={.5,2,2};lava.distance=1;
        std::vector<ProjectedTriangle> crossing{wall,lava};sortBackToFront(crossing);
        const auto reference=crossing;
        require(wrong(nearest(reference,{.5,.5}),painter(crossing,{.5,.5})),"fixture reproduces average-depth failure");
        const auto stats=resolveCaveOcclusion(crossing);sortBackToFront(crossing);
        require(stats.changed==2&&stats.fallbacks==0,"mutual depth conflict clips both surfaces");
        for(int y=1;y<19;++y)for(int x=1;x<19-y;++x) {
            const glm::vec2 p{x*.5f,y*.5f};
            require(!wrong(nearest(reference,p),painter(crossing,p)),"crossing surfaces match independent per-pixel depth");
        }
        for(auto const& t:crossing)for(std::size_t i=0;i<3;++i) {
            const auto& original=(t.texture&animatedLavaFlag)?lava:wall;
            const auto q=t.points[i];
            const glm::vec2 expected=glm::vec2{q.x*original.inverseW[1],q.y*original.inverseW[2]}/(10*t.inverseW[i]);
            require(glm::length(t.uv[i]-expected)<.0001f,"depth clipping preserves perspective-weighted UV");
        }
        auto alpha=lava;alpha.texture=0x20000;alpha.opacity=.4f;
        crossing={wall,alpha};resolveCaveOcclusion(crossing);
        require(crossing.size()==2&&crossing[1].opacity==.4f,"alpha particles excluded from opaque subtraction");
        const auto model=loadCaveModel("resources/data/normal_cave.mesh");
        std::size_t before=0,after=0,orderBefore=0,orderAfter=0,samples=0,maxExtra=0,maxChanged=0,maxTriangles=0,maxBatches=0;double time=0;
        for(auto size:{glm::vec2{640,346},glm::vec2{1920,1080},glm::vec2{1080,1920},glm::vec2{3840,2160}}) {
            for(int frame=0;frame<16;++frame) {
                std::vector<ProjectedTriangle> original;
                projectTriangles(model,caveView(frame*1700.),caveProjection(size.x,size.y),size.x,size.y,original);
                sortBackToFront(original);auto clipped=original;
                const auto start=std::chrono::steady_clock::now();
                const auto s=resolveCaveOcclusion(clipped);sortBackToFront(clipped);
                time+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                require(s.fallbacks==0,"actual scene does not hit fragment guard");
                maxTriangles=std::max(maxTriangles,clipped.size());
                std::size_t batches=0;
                for(std::size_t begin=0;begin<clipped.size();++batches)begin=caveBatchEnd(clipped,begin);
                maxBatches=std::max(maxBatches,batches);
                for(auto const& d:clipped)for(auto uv:d.uv)require(std::isfinite(uv.x)&&std::isfinite(uv.y)&&uv.x>=0&&uv.x<=1&&uv.y>=0&&uv.y<=1,"clipped UVs stay valid for native batch");
                maxChanged=std::max(maxChanged,s.changed);
                if(clipped.size()>original.size())maxExtra=std::max(maxExtra,clipped.size()-original.size());
                auto reverseOriginal=original,reverseClipped=clipped;
                std::reverse(reverseOriginal.begin(),reverseOriginal.end());std::reverse(reverseClipped.begin(),reverseClipped.end());
                for(auto const& t:original)if(t.texture&animatedLavaFlag) {
                    // Independently compare depth at interior and near-edge points
                    // of every visible lava triangle, not just sort-key order.
                    for(auto bary:{glm::vec2{.02,.02},glm::vec2{.96,.02},glm::vec2{.02,.96},
                        glm::vec2{.2,.2},glm::vec2{.6,.2},glm::vec2{.2,.6},glm::vec2{.48,.48}}) {
                        const auto p=t.points[0]*(1-bary.x-bary.y)+t.points[1]*bary.x+t.points[2]*bary.y;
                        const auto ref=nearest(original,p);
                        before+=wrong(ref,painter(original,p));after+=wrong(ref,painter(clipped,p));++samples;
                        orderBefore+=wrong(ref,painter(reverseOriginal,p));
                        const auto reverseHit=painter(reverseClipped,p);
                        orderAfter+=wrong(ref,reverseHit);
                    }
                }
            }
        }
        std::cout<<"Lava depth: samples="<<samples<<", wrong before="<<before<<", wrong after="<<after
            <<", reversed order before/after="<<orderBefore<<'/'<<orderAfter<<", max changed="<<maxChanged
            <<", max extra triangles="<<maxExtra<<", max triangles/batches="<<maxTriangles<<'/'<<maxBatches
            <<", mean depth clipping+sort="<<time/64<<"ms\n";
        require(after==0,"actual cave lava samples match per-pixel reference depth");
        require(orderBefore>0&&orderAfter==0,"real cave lava occlusion survives changed draw order");
        std::cout<<"Cave occlusion checks passed: crossing planes, weighted UV, alpha preservation, 64 real cave cases.\n";
        return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
