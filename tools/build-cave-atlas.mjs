// One atlas: contrast-preserving headroom and unmodified particle sprites.
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {readPinnedPNG,encodeRGBA} from './cave-png.mjs';
import {blocks,lava,blockExposure,lavaExposure} from './cave-textures.mjs';
const particles=readPinnedPNG('resources/textures/animated_mojang/cave/particles.png','1bd1d1e83715650c95ca3d6facae8db23720a49e');
if(blocks.w!==256||blocks.h!==256||lava.w!==32||lava.h!==512||particles.w!==128||particles.h!==128)throw Error('Unexpected cave atlas dimensions');
const width=256,height=768,pixels=Buffer.alloc(width*height*4);
for(let y=0;y<256;++y)for(let x=0;x<256;++x) {
 const i=(y*width+x)*4,e=blockExposure(Math.floor(x/16),Math.floor(y/16));
 for(let c=0;c<3;++c)pixels[i+c]=Math.round(blocks.pixels[i+c]*e[c]);
 pixels[i+3]=blocks.pixels[i+3];
}
for(let y=0;y<512;++y)for(let x=0;x<16;++x) {
 const i=((y+256)*width+x)*4,s=(y*32+x)*4;
 for(let c=0;c<3;++c)pixels[i+c]=Math.round(lava.pixels[s+c]*lavaExposure[c]);
 pixels[i+3]=lava.pixels[s+3];
}
for(let y=0;y<128;++y)particles.pixels.copy(pixels,((y+256)*width+32)*4,y*128*4,(y+1)*128*4);
const result=encodeRGBA(width,height,pixels),target='resources/textures/animated_mojang/cave/atlas.png';
if(process.argv.includes('--verify-only')) {if(!result.equals(fs.readFileSync(target)))throw Error('Generated cave atlas differs');}
else fs.writeFileSync(target,result);
console.log('Cave atlas 256x768: unclipped tile headroom, 32 lava frames, upstream particles; SHA256='+createHash('sha256').update(result).digest('hex'));
