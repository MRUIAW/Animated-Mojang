#include "scene/CaveCamera.h"
#include "scene/CaveParticles.h"
#include "scene/UiCaveBatch.h"
#include <chrono>
#include <iostream>
using namespace animated_mojang::scene;
void require(bool ok, char const* message) { if (!ok) throw std::runtime_error(message); }
void fixture(std::filesystem::path const& path, bool wall) {
    std::ofstream out(path,std::ios::binary);
    out.write("AMCP0001",8);
    const std::array<std::uint32_t,4> dimensions{4,2,4,1};
    out.write(reinterpret_cast<char const*>(dimensions.data()),16);
    std::array<std::uint16_t,32> grid{};
    grid[1+2*4]=wall?1:0;
    const CaveEmitter source{2,0,2,2,3,0};
    out.write(reinterpret_cast<char const*>(grid.data()),sizeof(grid));
    out.write(reinterpret_cast<char const*>(&source),sizeof(source));
}
int main() {
    try {
        CaveParticles scene; scene.load("resources/data/normal_cave.particles");
        require(scene.sources().size()==10,"actual scene has ten particle sources");
        unsigned souls=0,furnaces=0;
        for(auto const& e:scene.sources()) {souls+=(e.kind&255)==2;furnaces+=(e.kind&255)==3;}
        require(souls==9&&furnaces==1,"nine soul torches and one lit furnace");
        for(auto facing:{1u,2u,3u,4u}) {
            const auto p=CaveParticles::emitterPosition({0,0,0,2,facing,0});
            require(std::abs(p.y-(.2f+.625f*.8660254f+.02f))<.00001f,"emitter sits above tilted rod tip");
            require(facing>=3?std::abs(std::abs(p.x-.5f)-.1675f)<.00001f:std::abs(std::abs(p.z-.5f)-.1675f)<.00001f,"emitter follows wall facing");
        }
        require(CaveParticles::contains(1,{.2,.9,.2}),"full block occludes");
        require(CaveParticles::contains(2,{.2,.2,.2})&&!CaveParticles::contains(2,{.2,.8,.2}),"bottom slab only occludes lower half");
        require(!CaveParticles::contains(4,{.1,.6,.1})&&CaveParticles::contains(4,{.5,.6,.5}),"fence gaps and post");
        require(CaveParticles::contains(4|256,{.5,.45,.1}),"connected fence rail occludes");
        require(CaveParticles::contains(5,{.4,.5,.4})&&!CaveParticles::contains(5,{.2,.5,.6}),"cobweb crossed planes");
        const auto path=std::filesystem::path{"build/particle-test.bin"};
        fixture(path,true);CaveParticles wall;wall.load(path);
        require(wall.occluded({.5,.75,2.5},CaveParticles::emitterPosition(wall.sources()[0]),0),"wall blocks source");
        fixture(path,false);wall.load(path);
        require(!wall.occluded({.5,.75,2.5},CaveParticles::emitterPosition(wall.sources()[0]),0),"clear ray reaches source");
        scene.update(0,{31,13,6});require(scene.particles().size()==20,"initial particles seeded");
        scene.update(0,{31,13,6});require(scene.particles().size()==20,"repeated draw does not tick twice");
        scene.update(200,{31,13,6});
        int maxAge=0;for(auto const& p:scene.particles())maxAge=std::max(maxAge,p.age);
        require(maxAge==4,"normal four tick advance");
        scene.update(100000,{31,13,6});
        for(auto const& p:scene.particles())require(p.age<=8,"pause catches up at most four ticks");
        scene.update(0,{31,13,6});require(scene.particles().size()==20,"rewind resets simulation");
        scene.reset();scene.update(0,{999,999,999});require(scene.particles().empty(),"remote sources culled");
        scene.reset();scene.update(0,{31,13,6});
        std::size_t maxLive=0,maxTriangles=0;double sum=0;
        for(int frame=1;frame<=1200;++frame) {
            const auto time=frame*50.; const auto view=caveView(time);
            const auto start=std::chrono::steady_clock::now();
            scene.update(time,glm::vec3{glm::inverse(view)[3]});
            std::vector<ProjectedTriangle> projected;
            scene.append(view,caveProjection(1920,1080),1920,1080,projected);
            sum+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            maxLive=std::max(maxLive,scene.particles().size());maxTriangles=std::max(maxTriangles,projected.size());
            require(scene.particles().size()<=CaveParticles::limit,"particle count bounded");
            for(auto const& p:scene.particles())require(p.age<p.lifetime&&p.smokeFrame()<8,"lifetimes and smoke frame bounded");
            for(auto const& t:projected)for(auto uv:t.uv)
                require(uv.x>=32.f/256&&uv.x<96.f/256&&uv.y>=256.f/768&&uv.y<296.f/768,"sprite UV inside particle sub-atlas");
        }
        require(maxTriangles>0,"actual cave emits visible particle geometry");
        const std::array<std::uint32_t,4> abgr{0xff0000ff,0xff00ff00,0xffff0000,0xff000000};
        const auto packing=ColorPacking::fromSamples(abgr);
        require(packing.pack({1,0,0},.5f)==0x800000ff,"particle alpha is packed in native channel");
        std::cout<<"Particle checks passed: sources, torch tip/facing, occlusion, pause/rewind, alpha, 1200 frames; max live="
            <<maxLive<<", max visible triangles="<<maxTriangles<<", mean simulation+occlusion+projection="<<sum/1200<<"ms\n";
        return 0;
    } catch(std::exception const& e) {std::cerr<<e.what()<<'\n';return 1;}
}
