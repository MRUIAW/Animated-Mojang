import {readPinnedPNG} from './cave-png.mjs';
export const blocks=readPinnedPNG('resources/textures/animated_mojang/cave/blocks.png','ad02c048e7caad5f2b04eacc9764211477eee84d');
export const lava=readPinnedPNG('resources/textures/animated_mojang/cave/lava.png','ce0a46d3593d79e78394cd99e75f022bef966771');
function exposure(image,x,y,w,h) {
 const max=[0,0,0];
 for(let yy=y;yy<y+h;++yy)for(let xx=x;xx<x+w;++xx)for(let c=0;c<3;++c)
  max[c]=Math.max(max[c],image.pixels[(yy*image.w+xx)*4+c]);
 return max.map(v=>v?Math.min(2,255/v):2);
}
const exposures=Array.from({length:256},(_,n)=>exposure(blocks,n%16*16,Math.floor(n/16)*16,16,16));
export const blockExposure=(x,y)=>exposures[y*16+x];
// Common across all animation frames to avoid light flicker.
export const lavaExposure=exposure(lava,0,0,16,512);
