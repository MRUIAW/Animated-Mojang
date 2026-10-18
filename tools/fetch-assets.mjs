import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import https from 'node:https';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const commit = 'ef68ffb6d6cdbdfab4db960497719416e8b7a6bc';
const verifyOnly = process.argv.includes('--verify-only');
const assets = [
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/mojang_animation.png",
    "dest": "resources/textures/animated_mojang/intro/mojang_animation.png",
    "sha": "b466e9b50b727353a82602755fe50cf44fd5ca6e",
    "bytes": 1349277
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/studios.png",
    "dest": "resources/textures/animated_mojang/intro/studios.png",
    "sha": "f216dbc87185d1b9abe879c3dda296c11055b6f9",
    "bytes": 23031
  },
  {
    "source": "src/main/resources/assets/labymod_menu/sounds/startup.ogg",
    "dest": "resources/sounds/animated_mojang/startup.ogg",
    "sha": "b37095b271ba0671906bf09f114e2a10d150ac5c",
    "bytes": 208669
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/cave_particles_clean.png",
    "dest": "resources/textures/animated_mojang/cave/particles.png",
    "sha": "1bd1d1e83715650c95ca3d6facae8db23720a49e"
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/cave_blocks.png",
    "dest": "resources/textures/animated_mojang/cave/blocks.png",
    "sha": "ad02c048e7caad5f2b04eacc9764211477eee84d",
    "bytes": 41378
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/cave_lava_flow.png",
    "dest": "resources/textures/animated_mojang/cave/lava.png",
    "sha": "ce0a46d3593d79e78394cd99e75f022bef966771",
    "bytes": 3474
  },
  {
    "source": "src/main/resources/assets/labymod_menu/data/normal_cave.schem",
    "dest": "resources/data/normal_cave.schem",
    "sha": "fa6df51e9120b4af4c9655cf4bd82efa2b6523be",
    "bytes": 30960
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/minecraft_animation/minecraft_0.png",
    "dest": "resources/textures/animated_mojang/title/minecraft_animation/minecraft_0.png",
    "sha": "b392ceff5bad68ceb07777a12f1c6b5de5f0c23a",
    "bytes": 99567
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/minecraft_animation/minecraft_1.png",
    "dest": "resources/textures/animated_mojang/title/minecraft_animation/minecraft_1.png",
    "sha": "5aca54e36a5869d2e21c0bef0b9b88e9162bacd9",
    "bytes": 174532
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/minecraft_animation/minecraft_2.png",
    "dest": "resources/textures/animated_mojang/title/minecraft_animation/minecraft_2.png",
    "sha": "286062e12f95590dcabbf7cb0d5e794109135301",
    "bytes": 220619
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/minecraft_animation/minecraft_3.png",
    "dest": "resources/textures/animated_mojang/title/minecraft_animation/minecraft_3.png",
    "sha": "6345aa6881b0caf260d11630d712d6962269a666",
    "bytes": 249821
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/minecraft_animation/minecraft_4.png",
    "dest": "resources/textures/animated_mojang/title/minecraft_animation/minecraft_4.png",
    "sha": "97b3e6d3351661500c88bc4bd61a1b7b09c0816d",
    "bytes": 269606
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/minecraft_animation/minecraft_5.png",
    "dest": "resources/textures/animated_mojang/title/minecraft_animation/minecraft_5.png",
    "sha": "15b51876ef9893659feca3b450f39c50723abef4",
    "bytes": 293592
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/minecraft_animation/minecraft_6.png",
    "dest": "resources/textures/animated_mojang/title/minecraft_animation/minecraft_6.png",
    "sha": "63dba38f05fc4360cbfad6cbf685bc553a4d83de",
    "bytes": 272167
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/minecraft_animation/minecraft_7.png",
    "dest": "resources/textures/animated_mojang/title/minecraft_animation/minecraft_7.png",
    "sha": "f4f74e9aa66cc4ac557af93fee5540d13e7d6ea9",
    "bytes": 258788
  },
  {
    "source": "src/main/resources/assets/labymod_menu/textures/gui/title/minecraft_animation/minecraft_8.png",
    "dest": "resources/textures/animated_mojang/title/minecraft_animation/minecraft_8.png",
    "sha": "9c0e33aab779fd9422c831c515b0f634bcc84f6e",
    "bytes": 179273
  }
];
function blobHash(bytes) {
    return createHash('sha1').update(Buffer.from('blob ' + bytes.length + '\0')).update(bytes).digest('hex');
}
function download(url) {
    return new Promise((resolveDownload, reject) => {
        const req = https.get(url, { headers: { 'User-Agent': 'AnimatedMojang-asset-builder' } }, res => {
            if (res.statusCode !== 200) { res.resume(); reject(new Error('HTTP ' + res.statusCode)); return; }
            const chunks = [];
            res.on('data', chunk => chunks.push(chunk));
            res.on('error', reject);
            res.on('end', () => resolveDownload(Buffer.concat(chunks)));
        });
        req.setTimeout(20000, () => req.destroy(new Error('download timeout')));
        req.on('error', reject);
    });
}
for (const asset of assets) {
    const target = resolve(root, asset.dest);
    let bytes;
    try { bytes = await readFile(target); } catch {}
    if (!bytes || blobHash(bytes) !== asset.sha) {
        if (verifyOnly) throw new Error('Missing or modified asset: ' + asset.dest);
        let failure;
        for (let attempt = 0; attempt < 3; ++attempt) {
            try {
                bytes = await download('https://raw.githubusercontent.com/chara2015/animated_mojang/' + commit + '/' + asset.source);
                if (blobHash(bytes) !== asset.sha) throw new Error('asset integrity mismatch');
                failure = undefined;
                break;
            } catch (error) { failure = error; }
        }
        if (failure) throw failure;
        await mkdir(dirname(target), { recursive: true });
        await writeFile(target, bytes);
    }
    const dimensions = bytes.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]))
        ? ' ' + bytes.readUInt32BE(16) + 'x' + bytes.readUInt32BE(20) : '';
    process.stdout.write(asset.dest + dimensions + '\n');
}
process.stdout.write('Verified ' + assets.length + ' pinned upstream assets.\n');
