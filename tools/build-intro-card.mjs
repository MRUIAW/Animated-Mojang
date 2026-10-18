// The finished Mojang intro as a 16:9 picture: the opening flight dives past it, like the original outro.
// Layout matches introPlacement() in src/animation/MojangIntro.h; filtering matches the overlay's bilinear draw.
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {readPinnedPNG,encodeRGBA} from './cave-png.mjs';
const logo=readPinnedPNG('resources/textures/animated_mojang/intro/mojang_animation.png','b466e9b50b727353a82602755fe50cf44fd5ca6e',6000);
const studios=readPinnedPNG('resources/textures/animated_mojang/intro/studios.png','f216dbc87185d1b9abe879c3dda296c11055b6f9',560);
if(logo.w!==6000||logo.h!==1032||studios.w!==560||studios.h!==90)throw Error('Unexpected intro texture dimensions');
const width=3840,height=2160,background=[30,30,31],pixels=Buffer.alloc(width*height*4);
for(let i=0;i<width*height;++i){pixels[i*4]=background[0];pixels[i*4+1]=background[1];pixels[i*4+2]=background[2];pixels[i*4+3]=255;}
// Premultiplied bilinear sample, clamped to the source rectangle.
function sample(image,x0,y0,w,h,x,y){
 x=Math.min(Math.max(x,x0),x0+w-1);y=Math.min(Math.max(y,y0),y0+h-1);
 const xa=Math.floor(x),ya=Math.floor(y),xb=Math.min(xa+1,x0+w-1),yb=Math.min(ya+1,y0+h-1),fx=x-xa,fy=y-ya,out=[0,0,0,0];
 for(const [sx,sy,f] of [[xa,ya,(1-fx)*(1-fy)],[xb,ya,fx*(1-fy)],[xa,yb,(1-fx)*fy],[xb,yb,fx*fy]]){
  const i=(sy*image.w+sx)*4,a=image.pixels[i+3]/255*f;
  for(let c=0;c<3;++c)out[c]+=image.pixels[i+c]*a;out[3]+=a;
 }
 return out;
}
function draw(image,src,dst){
 for(let y=Math.floor(dst.y);y<Math.ceil(dst.y+dst.h);++y)for(let x=Math.floor(dst.x);x<Math.ceil(dst.x+dst.w);++x){
  const u=src.x+(x+.5-dst.x)/dst.w*src.w-.5,v=src.y+(y+.5-dst.y)/dst.h*src.h-.5,[r,g,b,a]=sample(image,src.x,src.y,src.w,src.h,u,v),i=(y*width+x)*4;
  pixels[i]=Math.round(r+pixels[i]*(1-a));pixels[i+1]=Math.round(g+pixels[i+1]*(1-a));pixels[i+2]=Math.round(b+pixels[i+2]*(1-a));
 }
}
const vanilla=Math.min(width*.75,height)*.25,logoW=vanilla*4,logoH=logoW*83/540,logoX=(width-logoW)/2,logoY=(height-logoH)/2-vanilla*.16;
const studiosH=vanilla*.2,studiosW=studiosH*506/56;
const last=78;
draw(logo,{x:last%10*600+31,y:Math.floor(last/10)*129+31,w:540,h:83},{x:logoX,y:logoY,w:logoW,h:logoH});
draw(studios,{x:30,y:18,w:506,h:56},{x:(width-studiosW)/2,y:logoY+logoH+vanilla*.12,w:studiosW,h:studiosH});
const result=encodeRGBA(width,height,pixels),target='resources/textures/animated_mojang/intro/mojang_card.png';
if(process.argv.includes('--verify-only')) {if(!result.equals(fs.readFileSync(target)))throw Error('Generated Mojang card differs');}
else fs.writeFileSync(target,result);
console.log('Mojang card 3840x2160: last logo frame and STUDIOS; SHA256='+createHash('sha256').update(result).digest('hex'));
