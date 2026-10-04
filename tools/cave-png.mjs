import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {inflateSync,deflateSync} from 'node:zlib';
const crcTable = Array.from({length:256},(_,n)=>{
    for(let i=0;i<8;++i) n=(n&1)?0xedb88320^(n>>>1):n>>>1;
    return n>>>0;
});
function crc32(bytes) {
    let crc=0xffffffff;
    for(const b of bytes) crc=crcTable[(crc^b)&255]^(crc>>>8);
    return (crc^0xffffffff)>>>0;
}
export function readPinnedPNG(path,sha) {
    const file=fs.readFileSync(path);
    if(createHash('sha1').update(Buffer.from('blob '+file.length+'\0')).update(file).digest('hex')!==sha)
        throw Error('Upstream atlas integrity mismatch: '+path);
    let header,palette,alpha; const idat=[];
    for(let at=8;at<file.length;) {
        const size=file.readUInt32BE(at),type=file.toString('ascii',at+4,at+8);
        if(at+12+size>file.length) throw Error('Truncated PNG');
        const data=file.subarray(at+8,at+8+size);
        if(crc32(file.subarray(at+4,at+8+size))!==file.readUInt32BE(at+8+size)) throw Error('PNG CRC mismatch');
        if(type==='IHDR') header=data;
        else if(type==='PLTE') palette=data;
        else if(type==='tRNS') alpha=data;
        else if(type==='IDAT') idat.push(data);
        at+=12+size;
    }
    if(!header||header[8]!==8||![3,6].includes(header[9])||header[10]||header[11]||header[12])
        throw Error('Unsupported source PNG format');
    const w=header.readUInt32BE(0),h=header.readUInt32BE(4),bpp=header[9]===6?4:1;
    if(!w||!h||w>512||h>512) throw Error('Invalid source PNG dimensions');
    const raw=inflateSync(Buffer.concat(idat),{maxOutputLength:(w*bpp+1)*h});
    if(raw.length!==(w*bpp+1)*h) throw Error('PNG row length mismatch');
    const scan=Buffer.alloc(w*h*bpp);
    for(let y=0;y<h;++y) {
        const row=y*(w*bpp+1),filter=raw[row];
        if(filter>4) throw Error('Invalid PNG filter');
        for(let x=0;x<w*bpp;++x) {
            const at=y*w*bpp+x,a=x>=bpp?scan[at-bpp]:0,b=y?scan[at-w*bpp]:0,c=y&&x>=bpp?scan[at-w*bpp-bpp]:0;
            const p=a+b-c,pa=Math.abs(p-a),pb=Math.abs(p-b),pc=Math.abs(p-c);
            const predictor=[0,a,b,Math.floor((a+b)/2),pa<=pb&&pa<=pc?a:pb<=pc?b:c][filter];
            scan[at]=(raw[row+1+x]+predictor)&255;
        }
    }
    if(bpp===4) return {w,h,pixels:scan};
    if(!palette||palette.length%3) throw Error('Invalid PNG palette');
    const pixels=Buffer.alloc(w*h*4);
    for(let i=0;i<w*h;++i) {
        const n=scan[i]; if(n*3+2>=palette.length) throw Error('PNG palette index out of range');
        palette.copy(pixels,i*4,n*3,n*3+3);pixels[i*4+3]=alpha?.[n]??255;
    }
    return {w,h,pixels};
}
function chunk(type,data) {
    const result=Buffer.alloc(data.length+12);
    result.writeUInt32BE(data.length);result.write(type,4);data.copy(result,8);
    result.writeUInt32BE(crc32(result.subarray(4,-4)),result.length-4);
    return result;
}

export function encodeRGBA(width,height,pixels) {
 const rows=Buffer.alloc((width*4+1)*height);
 for(let y=0;y<height;++y)pixels.copy(rows,y*(width*4+1)+1,y*width*4,(y+1)*width*4);
 const ihdr=Buffer.alloc(13);ihdr.writeUInt32BE(width);ihdr.writeUInt32BE(height,4);ihdr[8]=8;ihdr[9]=6;
 return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',ihdr),chunk('IDAT',deflateSync(rows,{level:9})),chunk('IEND',Buffer.alloc(0))]);
}
