import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {readCaveNBT,decodeBlocks} from './read-cave-nbt.mjs';
import {parameter} from './cave-details.mjs';

// Compact voxel occluders and emitters: runtime never needs to parse NBT.
export function buildParticleData(nbt) {
    const {Width:w,Height:h,Length:l}=nbt;
    const palette=[];for(const [s,i]of Object.entries(nbt.Palette))palette[i]=s;
    const ids=decodeBlocks(nbt.BlockData,w*h*l),states=Array.from(ids,i=>palette[i]);
    const grid=Buffer.alloc(w*h*l*2),emitters=[];
    for(let y=0;y<h;++y)for(let z=0;z<l;++z)for(let x=0;x<w;++x) {
        const i=x+z*w+y*w*l,s=states[i];let shape=1;
        if(/:(air|cave_air|void_air|barrier|grass|poppy|dandelion|oak_wall_sign)(\[|$)/.test(s)||/torch|rail|lava|water/.test(s))shape=0;
        else if(s.includes('slab')&&!s.includes('type=double'))shape=s.includes('type=top')?3:2;
        else if(s.includes('fence')) {
            shape=4;
            for(const [bit,dir]of ['north','south','west','east'].entries())if(parameter(s,dir,'false')==='true')shape|=1<<(8+bit);
        } else if(s.includes('cobweb'))shape=5;
        grid.writeUInt16LE(shape,i*2);
        const kind=s.includes('torch')?(s.includes('soul_')?2:1):s.includes('furnace')&&s.includes('lit=true')?3:0;
        if(kind) {
            const facing={north:1,south:2,west:3,east:4}[parameter(s,'facing','up')]??0;
            emitters.push([x,y,z,kind,facing,((x*31+z)*73428767^(y*17+73)*912931)>>>0]);
        }
    }
    for(const e of emitters) {
        for(let y=Math.max(0,e[1]-2);y<=Math.min(h-1,e[1]+2);++y)
        for(let z=Math.max(0,e[2]-2);z<=Math.min(l-1,e[2]+2);++z)
        for(let x=Math.max(0,e[0]-2);x<=Math.min(w-1,e[0]+2);++x) {
            const kind=grid.readUInt16LE((x+z*w+y*w*l)*2)&255;
            if(kind===4||kind===5)e[3]|=256;
        }
    }
    const header=Buffer.alloc(24);header.write('AMCP0001');
    [w,h,l,emitters.length].forEach((v,i)=>header.writeUInt32LE(v,8+i*4));
    const records=Buffer.alloc(emitters.length*24);
    emitters.forEach((e,i)=>e.forEach((v,j)=>records.writeUInt32LE(v,i*24+j*4)));
    return {bytes:Buffer.concat([header,grid,records]),emitters};
}
const source=fs.readFileSync('resources/data/normal_cave.schem');
if(createHash('sha1').update(Buffer.from('blob '+source.length+'\0')).update(source).digest('hex')!=='fa6df51e9120b4af4c9655cf4bd82efa2b6523be')throw Error('Cave source integrity mismatch');
const {bytes,emitters}=buildParticleData(readCaveNBT(source)),target='resources/data/normal_cave.particles';
if(process.argv.includes('--verify-only')) {if(!fs.readFileSync(target).equals(bytes))throw Error('Particle data differs');}
else fs.writeFileSync(target,bytes);
console.log(`Particle data: ${emitters.length} emitters, ${bytes.length} bytes, source-matched voxel occlusion.`);
