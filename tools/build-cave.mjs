import fs from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { readCaveNBT, decodeBlocks } from './read-cave-nbt.mjs';
import { caveAtlasHeight, animatedLavaFlag, isLava, lavaCorners, lavaTopUV, torchTransform, bakeLighting } from './cave-details.mjs';

import {blockExposure,lavaExposure} from './cave-textures.mjs';

const upstreamBlob = 'fa6df51e9120b4af4c9655cf4bd82efa2b6523be';
const faces = [[4,5,6,7],[1,0,3,2],[5,1,2,6],[0,4,7,3],[7,6,2,3],[0,1,5,4]];
const normals = [[0,0,1],[0,0,-1],[1,0,0],[-1,0,0],[0,1,0],[0,-1,0]];
const indices = [0,1,2,0,2,3];
const empty = state => !state || /:(air|cave_air|void_air|barrier)(\[|$)/.test(state);
const skipped = state => /:(glass|cobweb|grass|poppy|dandelion|oak_wall_sign)(\[|$)/.test(state);
const partial = state => /slab|fence|torch|rail|lava/.test(state);
// The atlas straight rail runs along texture V. The curved tile exits its
// south and east edges. Top-face corners below are SW, SE, NE, NW; rotate
// UVs in that coordinate system rather than copying upstream corner indices.
export function railTopUV(state, uv) {
    const shape=/shape=([^,\]]+)/.exec(state)?.[1]??'north_south';
    const turn={north_south:0,east_west:1,south_east:0,south_west:1,north_west:2,north_east:3}[shape]??0;
    return uv.map((_,i)=>uv[(i+turn)%4]);
}
// Match the upstream 16x16 block atlas, with distinct top/bottom/front tiles.
export function tileForFace(state, f) {
    if (state.includes('glowstone')) return [9,6];
    if (state.includes('torch')) return state.includes('soul_')?[3,8]:[0,5];
    if (state.includes('oak')) return [4,0];
    if (state.includes('rail')) return /shape=(north_east|north_west|south_east|south_west)/.test(state)?[0,7]:[0,8];
    if (state.includes('grass_block')) return f===4?[0,0]:f===5?[2,0]:state.includes('snowy=true')?[4,4]:[3,0];
    if (state.includes('dirt')) return [2,0];
    if (state.includes('diamond_ore')) return [2,3];
    if (state.includes('gold_ore')) return [0,2];
    if (state.includes('iron_ore')) return [1,2];
    if (state.includes('coal_ore')) return [2,2];
    if (state.includes('gravel')) return [3,1];
    if (state.includes('crafting_table')) return f===4?[11,2]:f===5?[4,0]:[11,3];
    if (state.includes('furnace')) {
        if(f===4) return [0,1];
        if(f===5) return [1,1];
        const facing=/facing=(east|west|south|north)/.exec(state)?.[1]??'north';
        const front={south:0,north:1,east:2,west:3}[facing];
        return f===front?(state.includes('lit=true')?[13,3]:[12,2]):[13,2];
    }
    if (state.includes('snow')) return [2,4];
    if (state.includes('ice')) return [3,4];
    return state.includes('cobblestone')?[0,1]:[1,0];
}
export function buildCaveMesh(nbt) {
    const {Width:w,Height:h,Length:l} = nbt;
    if (![w,h,l].every(n => Number.isInteger(n) && n > 0 && n <= 256) || w*h*l > 2_000_000) throw Error('Invalid cave dimensions');
    const palette = [];
    for (const [state,id] of Object.entries(nbt.Palette)) {
        if (!Number.isInteger(id) || id < 0 || id > 65535) throw Error('Invalid cave palette');
        palette[id] = state;
    }
    const blocks = decodeBlocks(nbt.BlockData, w*h*l);
    for (const id of blocks) if (!palette[id]) throw Error('Unknown cave palette index');
    const index = (x,y,z) => x+z*w+y*w*l;
    const at = (x,y,z) => x<0||y<0||z<0||x>=w||y>=h||z>=l ? null : palette[blocks[index(x,y,z)]];
    const lighting = bakeLighting(nbt, Array.from(blocks, id=>palette[id]));
    const vertices = [];
    let exposedFaces=0, occupied=0, skippedBlocks=0;
    function box(x,y,z,bounds,state,mask) {
        const [x0,y0,z0,x1,y1,z1]=bounds;
        let corners=[[x0,y0,z0],[x1,y0,z0],[x1,y1,z0],[x0,y1,z0],[x0,y0,z1],[x1,y0,z1],[x1,y1,z1],[x0,y1,z1]];
        if(isLava(state)) corners=lavaCorners(at,x,y,z,corners);
        if(state.includes('torch')) corners=corners.map(p=>torchTransform(state,p));
        for(let f=0;f<6;++f) {
            if(!mask[f]) continue;
            ++exposedFaces;
            const lava=isLava(state);
            const tile=tileForFace(state,f);
            let pixelU0=tile[0]*16,pixelV0=tile[1]*16,pixelU1=pixelU0+16,pixelV1=pixelV0+16;
            if(state.includes('torch')) {
                pixelU0+=7;pixelU1=pixelU0+2;
                pixelV0+=f===5?14:6;pixelV1=pixelV0+(f>=4?2:10);
            } else if(state.includes('slab')&&!state.includes('type=double')) {
                pixelV1=pixelV0+8;
            } else if(state.includes('fence')) {
                pixelU0+=6;pixelU1-=6;
                if(f>=4) {pixelV0+=6;pixelV1-=6;}
            }
            // Try an additional half pixel on stone only. Leave small torch
            // crops and other materials unchanged; native LOD is game-owned.
            const inset=/:stone(\[|$)/.test(state)?1:.5;
            const [u0,v0,u1,v1]=lava?[.5/256,256.5/caveAtlasHeight,15.5/256,271.5/caveAtlasHeight]:
                [(pixelU0+inset)/256,(pixelV0+inset)/caveAtlasHeight,(pixelU1-inset)/256,(pixelV1-inset)/caveAtlasHeight];
            let uv=[[u0,v1],[u1,v1],[u1,v0],[u0,v0]];
            if(lava&&f===4) {
                uv=lavaTopUV(faces[f].map(i=>corners[i][1]),uv);
            }
            if(state.includes('rail')&&f===4) uv=railTopUV(state,uv);
            const exposure=lava?lavaExposure:blockExposure(...tile);
            const tint=state.includes('grass_block')&&f===4?[.55,.85,.4]:[1,1,1];
            for(const i of indices) {
                const p=corners[faces[f][i]];
                const world=[p[0]+x,p[1]+y,p[2]+z];
                const rgb=lighting.sample(world,normals[f]);
                vertices.push(...world,...tint.map((c,j)=>Math.min(1,c*rgb[j]/exposure[j])),...uv[i],lava?animatedLavaFlag:0);
            }
        }
    }
    for(let y=0;y<h;++y) for(let z=0;z<l;++z) for(let x=0;x<w;++x) {
        const state=at(x,y,z);
        if(empty(state)) continue;
        if(skipped(state)) {++skippedBlocks;continue;}
        ++occupied;
        let bounds=[0,0,0,1,1,1];
        if(state.includes('slab')&&!state.includes('type=double')) bounds=state.includes('type=top')?[0,.5,0,1,1,1]:[0,0,0,1,.5,1];
        else if(state.includes('fence')) bounds=[.375,0,.375,.625,1,.625];
        else if(state.includes('torch')) bounds=[.4375,0,.4375,.5625,.625,.5625];
        // Reach the shared block edges so adjacent rail textures connect.
        else if(state.includes('rail')) bounds=[0,0,0,1,.04,1];
        const mask=normals.map(([dx,dy,dz])=>{
            const neighbor=at(x+dx,y+dy,z+dz);
            if(state.includes('lava')&&neighbor?.includes('lava')) return false;
            // Partial shapes do not seal a full neighboring block face.
            return empty(neighbor)||skipped(neighbor)||partial(neighbor);
        });
        if(state.includes('rail')) for(let f=0;f<6;++f) mask[f]=f===4;
        if(state.includes('torch')) mask.fill(true);
        box(x,y,z,bounds,state,mask);
    }
    const header=Buffer.alloc(24);
    header.write('AMCV0005'); header.writeUInt32LE(vertices.length/9,8);
    header.writeUInt32LE(w,12);header.writeUInt32LE(h,16);header.writeUInt32LE(l,20);
    const payload=Buffer.alloc(vertices.length*4);
    vertices.forEach((v,i)=>i%9===8?payload.writeUInt32LE(v,i*4):payload.writeFloatLE(v,i*4));
    return {bytes:Buffer.concat([header,payload]), stats:{width:w,height:h,length:l,occupied,skippedBlocks,exposedFaces,triangles:vertices.length/27,vertices:vertices.length/9,format:'AMCV0005',stride:36,pointLights:lighting.lights.length}};
}
export function convertCave(source) {
    const blob=createHash('sha1').update(Buffer.from('blob '+source.length+'\0')).update(source).digest('hex');
    if(blob!==upstreamBlob) throw Error('Upstream cave integrity mismatch');
    return buildCaveMesh(readCaveNBT(source));
}
if(process.argv[1]&&fileURLToPath(import.meta.url)===fs.realpathSync(process.argv[1])) {
    const source=fs.readFileSync('resources/data/normal_cave.schem');
    const {bytes,stats}=convertCave(source);
    const target='resources/data/normal_cave.mesh';
    if(process.argv.includes('--verify-only')) {
        if(!fs.readFileSync(target).equals(bytes)) throw Error('Generated cave mesh differs');
    } else fs.writeFileSync(target,bytes);
    fs.mkdirSync('build/upstream-cave', {recursive:true});
    fs.writeFileSync('build/upstream-cave/mesh-stats.json',JSON.stringify({...stats,upstreamBlob,meshSHA256:createHash('sha256').update(bytes).digest('hex')},null,2));
    console.log(JSON.stringify(stats));
}
