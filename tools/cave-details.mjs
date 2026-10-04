// Scene conversion helpers: state-aware geometry and offline RGB light baking.
export const caveAtlasHeight = 768;
export const animatedLavaFlag = 0x10000;
export const isLava = s => !!s && /:lava(\[|$)/.test(s);
export const parameter = (s, key, fallback) => new RegExp(`${key}=([^,\\]]+)`).exec(s ?? '')?.[1] ?? fallback;
const air = s => !s || /:(air|cave_air|void_air|barrier)(\[|$)/.test(s);
export const lightPasses = s => air(s) || /lava|torch|slab|fence|rail|glass|cobweb|poppy|dandelion|sign/.test(s)
    || /:grass(\[|$)/.test(s);
export function liquidHeight(s) {
    const level = Math.max(0, Math.min(15, Number(parameter(s, 'level', '0'))));
    // 8..15 encode falling fluid, not a negative-height liquid surface.
    return (8 - (level >= 8 ? 0 : level)) / 9;
}
export function lavaCornerHeight(at, cx, y, cz) {
    let sum = 0, weight = 0;
    // Use world corner coordinates: every incident block gets the exact same
    // height, including diagonals. This closes shared edges without side caps.
    for (let dz = -1; dz <= 0; ++dz) for (let dx = -1; dx <= 0; ++dx) {
        const s = at(cx + dx, y, cz + dz);
        if (isLava(s)) {
            if (isLava(at(cx + dx, y + 1, cz + dz))) return 1;
            const height = liquidHeight(s), w = height >= .8 ? 10 : 1;
            sum += height * w; weight += w;
        } else if (air(s)) ++weight;
    }
    return weight ? sum / weight : 0;
}
export function lavaCorners(at, x, y, z, corners) {
    return corners.map(([cx, cy, cz]) => [cx, cy ? lavaCornerHeight(at, x + cx, y, z + cz) : 0, cz]);
}
// Heights use top-face order: (x0,z1),(x1,z1),(x1,z0),(x0,z0).
// Texture animation travels toward increasing V, which must point downhill.
export function lavaTopUV(heights, uv) {
    const slopeX=heights[1]+heights[2]-heights[0]-heights[3];
    const slopeZ=heights[0]+heights[1]-heights[2]-heights[3];
    const rotation=Math.abs(slopeX)>Math.abs(slopeZ)?(slopeX>0?1:3):(slopeZ>0?2:0);
    return uv.map((_,i)=>uv[(i+rotation)%4]);
}
export function torchTransform(state, p) {
    if (!state.includes('wall_torch')) return p;
    const [dx, dz] = {north:[0,-1], south:[0,1], west:[-1,0], east:[1,0]}[parameter(state, 'facing', 'north')];
    const sin = .5, cos = Math.sqrt(3)/2;
    const x = p[0] - .5, z = p[2] - .5, axial = dx*x + dz*z;
    // Facing points away from the supporting wall. Anchor the base at that
    // wall and rotate the entire rod/head, preserving its rectangular shape.
    return [.5 - dx*.48 + x + (cos-1)*dx*axial + sin*dx*p[1],
        .2 + cos*p[1] - sin*axial,
        .5 - dz*.48 + z + (cos-1)*dz*axial + sin*dz*p[1]];
}
function emission(state) {
    if (isLava(state)) return [15,9,1];
    if (state?.includes('soul_') && state.includes('torch')) return [1,11,15];
    if (state?.includes('torch')) return [15,11,0];
    if (state?.includes('glowstone')) return [15,15,15];
    if (state?.includes('furnace') && state.includes('lit=true')) return [13,9,0];
    return null;
}
export function collectPointLights(nbt, states, camera = [31,13,6]) {
    const {Width:w,Height:h,Length:l}=nbt, lights=[];
    for(let y=0;y<h;++y)for(let z=0;z<l;++z)for(let x=0;x<w;++x) {
        const state=states[x+z*w+y*w*l];
        let color, position=[x+.5,y+.5,z+.5], attenuation;
        if(state?.includes('torch')) {
            color=state.includes('soul_')?[10/255,86/255,165/255]:[204/255,102/255,0];
            position[1]=y+.75; attenuation=[.12,.20,.256];
        } else if(state?.includes('glowstone')) { color=[1,1,1]; attenuation=[1,.045,.0075]; }
        else if(state?.includes('furnace')&&state.includes('lit=true')) { color=[204/255,102/255,0]; attenuation=[.12,.20,.256]; }
        else if(isLava(state)&&(x%4===0||y%4===0||z%4===0)) { color=[1,.44,.05]; attenuation=[1,.35,.44]; }
        if(color) lights.push({position,color,attenuation});
    }
    const score=p=>(p[1]-camera[1])**2*100+(p[0]-camera[0])**2+(p[2]-camera[2])**2;
    return lights.sort((a,b)=>score(a.position)-score(b.position)).slice(0,48);
}
export function pointDiffuse(lights, p) {
    const total=[0,0,0];
    for(const light of lights) {
        const d=Math.hypot(...light.position.map((v,i)=>v-p[i]));
        const [c,l,q]=light.attenuation, gain=1/(c+l*d+q*d*d);
        for(let ch=0;ch<3;++ch) total[ch]+=light.color[ch]*gain;
    }
    return total;
}
export function resolveLegacyLight(nbt, states) {
    const {Width:w,Height:h,Length:l}=nbt, size=w*h*l, plane=w*l;
    const levels=new Int16Array(size), queued=new Uint8Array(size), queue=[];
    const embedded=nbt.BlockLight?.length===size?nbt.BlockLight:null;
    const source=i=>states[i]?.includes('furnace')&&states[i].includes('lit=true')?13:emission(states[i])?15:0;
    for(let i=0;i<size;++i) {
        levels[i]=embedded?(embedded[i]>127?embedded[i]-256:embedded[i]):Math.max(source(i),lightPasses(states[i])?1:0);
        if(!embedded||levels[i]<0) {queue.push(i);queued[i]=1;}
    }
    for(let head=0;head<queue.length;++head) {
        const i=queue[head];queued[i]=0;
        const x=i%w,z=Math.floor(i/w)%l,y=Math.floor(i/plane), neighbors=[];
        if(x)neighbors.push(i-1);if(x+1<w)neighbors.push(i+1);
        if(z)neighbors.push(i-w);if(z+1<l)neighbors.push(i+w);
        if(y)neighbors.push(i-plane);if(y+1<h)neighbors.push(i+plane);
        let level=Math.max(source(i),lightPasses(states[i])?1:0);
        for(const j of neighbors)level=Math.max(level,levels[j]-1);
        if(level!==levels[i]) {
            levels[i]=level;
            for(const j of neighbors)if(!queued[j]){queue.push(j);queued[j]=1;}
        }
    }
    return levels;
}
export function bakeLighting(nbt, states) {
    const {Width:w,Height:h,Length:l}=nbt;
    // The upstream modern decode overwrites all RGB fields with the resolved
    // default grey field; color is then added by its 48-light fragment shader.
    const levels=resolveLegacyLight(nbt,states), lights=collectPointLights(nbt,states);
    return {
        levels, lights,
        sample(p, normal) {
            const base=p.map(Math.floor); let sum=0,count=0;
            for(let dy=-1;dy<=0;++dy)for(let dz=-1;dz<=0;++dz)for(let dx=-1;dx<=0;++dx) {
                const x=base[0]+dx,y=base[1]+dy,z=base[2]+dz;
                const valid=x>=0&&y>=0&&z>=0&&x<w&&y<h&&z<l, i=x+z*w+y*w*l;
                if(!valid||lightPasses(states[i])||emission(states[i])) {
                    sum+=valid?Math.max(0,levels[i]):0; ++count;
                } else if(dy===0) ++count;
            }
            const legacy=Math.min(1,(.06*(count?sum/count:0)+.1)*2);
            const shading=normal[0]!==0?.6:normal[1]!==0?1:.8;
            const diffuse=lights.length?pointDiffuse(lights,p):[1,1,1];
            return diffuse.map(v=>Math.max(0,v*legacy*shading));
        }
    };
}
