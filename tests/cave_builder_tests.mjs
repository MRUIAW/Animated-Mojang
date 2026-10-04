import assert from 'node:assert/strict';
import fs from 'node:fs';
import { gzipSync } from 'node:zlib';
import { buildCaveMesh, convertCave, tileForFace } from '../tools/build-cave.mjs';
import { readCaveNBT, decodeBlocks } from '../tools/read-cave-nbt.mjs';
import {blocks as blockPixels,lavaExposure,blockExposure} from '../tools/cave-textures.mjs';
import { caveAtlasHeight, animatedLavaFlag, liquidHeight, lavaCorners, lavaCornerHeight, lavaTopUV, torchTransform, bakeLighting, lightPasses, resolveLegacyLight, pointDiffuse, collectPointLights } from '../tools/cave-details.mjs';

assert.deepEqual([...decodeBlocks(Buffer.from([0,127,128,1]),3)],[0,127,128]);
for(const data of [[128],[255,255,255,255,255,1],[0,1]])
    assert.throws(()=>decodeBlocks(Buffer.from(data),1));
assert.throws(()=>readCaveNBT(gzipSync(Buffer.from([10,0]))));
const fixture={Width:2,Height:1,Length:1,Palette:{'minecraft:air':0,'minecraft:stone':1},BlockData:Buffer.from([1,1])};
const two=buildCaveMesh(fixture);
assert.equal(two.stats.exposedFaces,10,'adjacent solid cubes remove both internal faces');
assert.equal(two.stats.triangles,20);
const one=buildCaveMesh({...fixture,BlockData:Buffer.from([1,0])});
assert.equal(one.stats.exposedFaces,6);
for(let at=24;at<one.bytes.length;at+=108) {
    const vertices=[0,36,72].map(o=>[0,4,8].map(n=>one.bytes.readFloatLE(at+o+n)));
    const [a,b,c]=vertices;
    const u=b.map((v,i)=>v-a[i]),v=c.map((n,i)=>n-a[i]);
    const normal=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];
    const center=a.map((n,i)=>(n+b[i]+c[i])/3-.5);
    assert(normal.reduce((sum,n,i)=>sum+n*center[i],0)>0,'exterior normals face outside the block');
}
assert.throws(()=>buildCaveMesh({...fixture,BlockData:Buffer.from([2,0])}));
assert.throws(()=>buildCaveMesh({...fixture,Width:0}));
const source=fs.readFileSync('resources/data/normal_cave.schem');
const cave=convertCave(source);
assert(cave.bytes.equals(fs.readFileSync('resources/data/normal_cave.mesh')),'actual shipped model matches pinned source');
assert.equal(cave.stats.vertices,98094);
assert.equal(cave.bytes.subarray(0,8).toString(),'AMCV0005');
assert.equal(cave.bytes.length,24+98094*36);
assert.deepEqual(tileForFace('minecraft:grass_block',4),[0,0]);
assert.deepEqual(tileForFace('minecraft:grass_block',5),[2,0]);
assert.deepEqual(tileForFace('minecraft:furnace[facing=east,lit=true]',2),[13,3]);
assert.deepEqual(tileForFace('minecraft:furnace[facing=east,lit=true]',3),[13,2]);
let lavaVertices=0;
for(let at=24;at<cave.bytes.length;at+=36) {
    const texture=cave.bytes.readUInt32LE(at+32);
    assert.equal(texture&0xffff,0,'block and animated lava share one texture');
    for(const offset of [24,28]) assert(cave.bytes.readFloatLE(at+offset)>0&&cave.bytes.readFloatLE(at+offset)<1);
    if(texture&animatedLavaFlag) {
        ++lavaVertices;
        assert(cave.bytes.readFloatLE(at+24)<16/256&&cave.bytes.readFloatLE(at+28)>256/caveAtlasHeight
            &&cave.bytes.readFloatLE(at+28)<272/caveAtlasHeight,'lava base frame is inside the new atlas');
    }
}
assert(lavaVertices>0,'cave contains lava texture geometry');
const nbt=readCaveNBT(source),blocks=decodeBlocks(nbt.BlockData,nbt.Width*nbt.Height*nbt.Length);
assert.equal(blocks[31+6*nbt.Width+13*nbt.Width*nbt.Length],nbt.Palette['minecraft:air'],'menu camera starts in air');

// Verify rail directions against the source PNG's opaque edge pixels, not a
// duplicate of the UV rotation table. Inspect the generated mesh UVs directly.
const railDirections={north_south:['north','south'],east_west:['east','west'],
    south_east:['south','east'],south_west:['south','west'],north_west:['north','west'],north_east:['north','east']};
function railEdges(vertices,x=0,z=0) {
    const corners=new Map(vertices.map(v=>[`${Math.round(v.x-x)},${Math.round(v.z-z)}`,[v.u,v.v]]));
    assert.equal(corners.size,4,'rail is a complete top-face quad');
    const result=[];
    for(const direction of ['north','south','east','west']) {
        let opaque=0;
        for(let i=0;i<16;++i) {
            const t=(i+.5)/16;
            const [px,pz]={north:[t,0],south:[t,1],east:[1,t],west:[0,t]}[direction];
            const uv=[0,0];
            for(const [a,b]of [[0,0],[1,0],[0,1],[1,1]]) {
                const weight=(a?px:1-px)*(b?pz:1-pz),corner=corners.get(`${a},${b}`);
                uv[0]+=weight*corner[0];uv[1]+=weight*corner[1];
            }
            const tx=Math.floor(uv[0]*256),ty=Math.floor(uv[1]*caveAtlasHeight);
            assert(tx>=0&&tx<256&&ty>=0&&ty<256,'rail UV remains in the block atlas');
            opaque+=blockPixels.pixels[(ty*256+tx)*4+3]>=128;
        }
        if(opaque)result.push(direction);
    }
    return result.sort();
}
function meshVertices(bytes) {
    const vertices=[];
    for(let a=24;a<bytes.length;a+=36)vertices.push({x:bytes.readFloatLE(a),y:bytes.readFloatLE(a+4),
        z:bytes.readFloatLE(a+8),u:bytes.readFloatLE(a+24),v:bytes.readFloatLE(a+28)});
    return vertices;
}
for(const [shape,ends]of Object.entries(railDirections)) {
    const rail=buildCaveMesh({Width:1,Height:1,Length:1,Palette:{[`minecraft:rail[shape=${shape},waterlogged=false]`]:0},BlockData:Buffer.from([0])});
    const vertices=meshVertices(rail.bytes);
    assert.equal(vertices.length,6,'each rail emits exactly two top-face triangles');
    assert(vertices.every(v=>(v.x===0||v.x===1)&&(v.z===0||v.z===1)&&Math.abs(v.y-.04)<1e-6),
        'rail reaches both shared block edges without adding side geometry');
    assert.deepEqual(railEdges(vertices),[...ends].sort(),`${shape} texture exits the correct two world edges`);
}
const actualRailFaces=new Map();
const railPalette=Object.fromEntries(Object.entries(nbt.Palette).filter(([s])=>s.startsWith('minecraft:rail[')).map(([s,i])=>[i,s]));
// Inspect triangles individually and group rail faces by their real source cell.
for(let a=24;a<cave.bytes.length;a+=108) {
    const vertices=[0,36,72].map(o=>({x:cave.bytes.readFloatLE(a+o),y:cave.bytes.readFloatLE(a+o+4),
        z:cave.bytes.readFloatLE(a+o+8),u:cave.bytes.readFloatLE(a+o+24),v:cave.bytes.readFloatLE(a+o+28)}));
    const x=Math.min(...vertices.map(v=>v.x)),z=Math.min(...vertices.map(v=>v.z)),y=Math.floor(vertices[0].y);
    if(!Number.isInteger(x)||!Number.isInteger(z)||!vertices.every(v=>Math.abs(v.y-y-.04)<1e-6))continue;
    const id=blocks[x+z*nbt.Width+y*nbt.Width*nbt.Length];
    const state=railPalette[id];
    if(!state)continue;
    const key=`${x},${y},${z}`;
    if(!actualRailFaces.has(key))actualRailFaces.set(key,{x,y,z,state,vertices:[]});
    actualRailFaces.get(key).vertices.push(...vertices);
}
const sourceRailCount=Array.from(blocks).filter(id=>railPalette[id]).length;
assert.equal(actualRailFaces.size,sourceRailCount,'all real cave rails reach the generated mesh');
for(const rail of actualRailFaces.values()) {
    const shape=/shape=([^,\]]+)/.exec(rail.state)[1];
    assert.equal(rail.vertices.length,6,'real rail has exactly one quad');
    assert.deepEqual(railEdges(rail.vertices,rail.x,rail.z),[...railDirections[shape]].sort(),`real rail ${rail.x},${rail.y},${rail.z} has correct exits`);
    for(const end of railDirections[shape]) {
        const [dx,dz]={north:[0,-1],south:[0,1],east:[1,0],west:[-1,0]}[end];
        const other=actualRailFaces.get(`${rail.x+dx},${rail.y},${rail.z+dz}`);
        if(!other)continue; // Intentional track endpoints are not connections.
        const reverse={north:'south',south:'north',east:'west',west:'east'}[end];
        assert(railEdges(other.vertices,other.x,other.z).includes(reverse),'neighbor rails expose matching ends');
    }
}
console.log(`Rail checks passed: all six flat shapes and ${sourceRailCount} real cave rails; source-pixel exits, full shared edges and reciprocal connections.`);

// A mixed source/flow fixture must slope, share both common-edge endpoints,
// and never interpret falling-state bits as a negative liquid height.
const states=new Map([['0,0,0','minecraft:lava[level=0]'],['1,0,0','minecraft:lava[level=6]']]);
const at=(x,y,z)=>states.get(`${x},${y},${z}`)??'minecraft:air';
const corners=[[0,0,0],[1,0,0],[1,1,0],[0,1,0],[0,0,1],[1,0,1],[1,1,1],[0,1,1]];
const sourceCorners=lavaCorners(at,0,0,0,corners),flowCorners=lavaCorners(at,1,0,0,corners);
assert.equal(sourceCorners[2][1],flowCorners[3][1]);
assert.equal(sourceCorners[6][1],flowCorners[7][1]);
assert(flowCorners[3][1]>flowCorners[2][1],'flowing top slopes away from source');
for(let level=0;level<16;++level)assert(liquidHeight(`minecraft:lava[level=${level}]`)>0);
assert.equal(liquidHeight('minecraft:lava[level=14]'),liquidHeight('minecraft:lava[level=0]'));
states.set('1,1,0','minecraft:lava[level=10]');
assert.equal(lavaCornerHeight(at,1,0,0),1,'stacked lava reaches the upper block bottom');

for(const [facing,dir]of Object.entries({east:[1,0],west:[-1,0],north:[0,-1],south:[0,1]})) {
    const state=`minecraft:soul_wall_torch[facing=${facing}]`;
    const base=torchTransform(state,[.5,0,.5]),tip=torchTransform(state,[.5,.625,.5]);
    assert((tip[0]-base[0])*dir[0]+(tip[2]-base[2])*dir[1]>.3,'torch leans away from support');
    assert(Math.abs(Math.hypot(...tip.map((p,i)=>p-base[i]))-.625)<1e-9,'rotation preserves rod length');
    assert((base[0]-.5)*dir[0]+(base[2]-.5)*dir[1]<-.45,'rod base anchors at supporting wall');
}
const lightFixture={Width:5,Height:1,Length:1,
    BlockLight:Buffer.from([255,255,255,255,255]),
    BlockLightRed:Buffer.from([255,255,255,255,255]),
    BlockLightGreen:Buffer.from([255,255,255,255,255]),
    BlockLightBlue:Buffer.from([255,255,255,255,255])};
assert(!lightPasses('minecraft:grass_block[snowy=false]')&&lightPasses('minecraft:grass'),'grass block is solid; plant grass passes light');
const soul=bakeLighting(lightFixture,['minecraft:soul_wall_torch[facing=east]',...Array(4).fill('minecraft:air')]);
const soulNear=pointDiffuse(soul.lights,[1,.75,.5]),soulFar=pointDiffuse(soul.lights,[4,.75,.5]);
assert(soulNear[2]>soulNear[0],'soul shader point light is blue');
assert(soulNear[2]>soulFar[2],'upstream quadratic point attenuation fades with distance');
const dark=bakeLighting(lightFixture,Array(5).fill('minecraft:air'));
assert([...dark.levels].every(v=>v===1),'signed -1 placeholders resolve to the upstream air minimum, not 255');
const lavaLight=bakeLighting(lightFixture,['minecraft:lava[level=0]',...Array(4).fill('minecraft:air')]);
assert(pointDiffuse(lavaLight.lights,[1,.5,.5])[0]>pointDiffuse(lavaLight.lights,[1,.5,.5])[2],'lava point light is warm');
const referenceLight=[{position:[0,0,0],color:[1,.44,.05],attenuation:[1,.35,.44]}];
assert(Math.abs(pointDiffuse(referenceLight,[2,0,0])[0]-1/3.46)<1e-12,'diffuse matches upstream GLSL coefficient at distance two');
assert(pointDiffuse([...referenceLight,...referenceLight],[0,0,0])[0]>1,'overlapping diffuse is not clamped before encoding');
const noColorField={...lightFixture,BlockLight:Buffer.from([15,14,13,12,11])};
const grayA=resolveLegacyLight(noColorField,Array(5).fill('minecraft:air'));
const grayB=resolveLegacyLight({...noColorField,BlockLightRed:Buffer.alloc(5,0),BlockLightBlue:Buffer.alloc(5,15)},Array(5).fill('minecraft:air'));
assert.deepEqual(grayA,grayB,'upstream modern decode uses the default light field, not the embedded RGB fields');
const faceColors=new Map();
for(let a=24;a<two.bytes.length;a+=108) {
    const p=[0,36,72].map(o=>[0,4,8].map(n=>two.bytes.readFloatLE(a+o+n)));
    const u=p[1].map((v,i)=>v-p[0][i]),v=p[2].map((n,i)=>n-p[0][i]);
    if(u[0]*v[1]-u[1]*v[0]<=0||p.some(q=>q[2]!==1))continue;
    for(let j=0;j<3;++j)if(p[j][0]===1){const key=p[j].join(',');const color=[12,16,20].map(o=>two.bytes.readFloatLE(a+j*36+o));
        if(faceColors.has(key))assert.deepEqual(color,faceColors.get(key),'coplanar stone shared edges have identical vertex light');
        faceColors.set(key,color);}
}
assert.equal(faceColors.size,2,'test covers both endpoints of a shared stone edge');
const palette=[];for(const [s,i]of Object.entries(nbt.Palette))palette[i]=s;
const actualLights=collectPointLights(nbt,Array.from(blocks,i=>palette[i]));
assert.equal(actualLights.length,48,'upstream camera selection caps the actual 56 scene lights at 48');
for(let y=0;y<256;++y)for(let x=0;x<256;++x)for(let ch=0;ch<3;++ch) {
    const source=blockPixels.pixels[(y*256+x)*4+ch],e=blockExposure(x>>4,y>>4)[ch];
    assert(e>=1&&e<=2&&source*e<=255+1e-9,'no block texture channel clips while making light headroom');
    const prepared=Math.round(source*e);
    assert(Math.abs(prepared/e-source)<=.5,'matching vertex compensation restores source detail within rounding');
}
assert(blockExposure(1,0)[0]<2,'stone maximum constrains exposure before texture clipping');
assert(lavaExposure.every(e=>e>=1&&e<=2),'common lava exposure bounds');
for(let a=24;a<one.bytes.length;a+=36) {
    assert(one.bytes.readFloatLE(a+24)>=17/256&&one.bytes.readFloatLE(a+24)<=31/256,'stone inset adds half a pixel per side');
    assert(one.bytes.readFloatLE(a+28)>=1/caveAtlasHeight-1e-9&&one.bytes.readFloatLE(a+28)<=15/caveAtlasHeight+1e-9,'stone vertical inset bounds');
}
const baseUV=[[0,1],[1,1],[1,0],[0,0]];
function downhill(heights) {
    const uv=lavaTopUV(heights,baseUV),v=uv.map(p=>p[1]);
    const sx=heights[1]+heights[2]-heights[0]-heights[3],sz=heights[0]+heights[1]-heights[2]-heights[3];
    if(Math.abs(sx)+Math.abs(sz)>1e-9)
        assert(sx*(v[1]+v[2]-v[0]-v[3])+sz*(v[0]+v[1]-v[2]-v[3])<0,'increasing animated V follows downhill height gradient');
}
for(const heights of [[0,1,1,0],[1,0,0,1],[1,1,0,0],[0,0,1,1]])downhill(heights);
const actualAt=(x,y,z)=>x<0||y<0||z<0||x>=nbt.Width||y>=nbt.Height||z>=nbt.Length?null:palette[blocks[x+z*nbt.Width+y*nbt.Width*nbt.Length]];
let actualSlopes=0;
for(let y=0;y<nbt.Height;y++)for(let z=0;z<nbt.Length;z++)for(let x=0;x<nbt.Width;x++)if(actualAt(x,y,z).includes(':lava')) {
    const heights=[[0,1],[1,1],[1,0],[0,0]].map(([dx,dz])=>lavaCornerHeight(actualAt,x+dx,y,z+dz));
    downhill(heights);++actualSlopes;
}
for(const [x,y,z]of [[24,14,42],[24,12,41]]) {
    const h=[[0,1],[1,1],[1,0],[0,0]].map(([dx,dz])=>lavaCornerHeight(actualAt,x+dx,y,z+dz));
    assert(h[0]+h[1]>h[2]+h[3],'blue-box ramps descend toward minus Z');
    assert.deepEqual(lavaTopUV(h,baseUV),[baseUV[2],baseUV[3],baseUV[0],baseUV[1]],'blue-box ramps reverse prior top-face V mapping');
}
console.log(`UV checks passed: stone inset; all four flow axes and ${actualSlopes} actual lava cells; both blue-box ramps.`);
console.log('Cave builder checks passed: liquid/t torch geometry, signed legacy light, upstream point coefficients/HDR overlap/48-light selection, grayscale decode, continuous stone-edge colors.');
