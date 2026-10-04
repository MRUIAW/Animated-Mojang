import assert from 'node:assert/strict';
import fs from 'node:fs';
import { readCaveNBT, decodeBlocks } from '../tools/read-cave-nbt.mjs';
import { collectPointLights, pointDiffuse } from '../tools/cave-details.mjs';

// This file deliberately reads physical eyes emitted by the compiled C++
// camera, rather than reimplementing its interpolation or view math in JS.
const rows = fs.readFileSync('build/cave-camera-path.csv', 'utf8').trim().split(/\r?\n/).slice(1);
assert.equal(rows.length, 4 * (4001 + 30 * 501), 'all six-page paths sampled every millisecond at four motion clock offsets');
const nbt = readCaveNBT(fs.readFileSync('resources/data/normal_cave.schem'));
const blocks = decodeBlocks(nbt.BlockData, nbt.Width * nbt.Height * nbt.Length);
const palette = Object.fromEntries(Object.entries(nbt.Palette).map(([state, id]) => [id, state]));
let outside = 0, inside = 0;
for (const row of rows) {
    const [kind, time, ...values] = row.split(',');
    const pose = values.map(Number);
    assert(pose.every(Number.isFinite));
    const [x, y, z] = pose.slice(6).map(Math.floor);
    if (x < 0 || y < 0 || z < 0 || x >= nbt.Width || y >= nbt.Height || z >= nbt.Length) {
        assert.equal(kind, 'opening', 'page transitions stay inside cave bounds');
        ++outside;
        continue;
    }
    const state = palette[blocks[x + z * nbt.Width + y * nbt.Width * nbt.Length]];
    assert(['minecraft:air', 'minecraft:cave_air', 'minecraft:void_air'].includes(state),
        `${kind} at ${time}ms puts physical eye in ${state} at ${x},${y},${z}`);
    ++inside;
}
const states = Array.from(blocks, id => palette[id]);
const keys = camera => collectPointLights(nbt, states, camera).map(p => p.position.join(',')).sort();
const baked = keys([31, 13, 6]);
for (const camera of [[23, 17, 15], [23, 13, 15], [18, 13, 18]])
    assert.deepEqual(keys(camera), baked, 'new idle page camera selects the same 48 point lights as baked mesh');
// The entrance camera swaps one distant, low-level lava light at the cap.
// Retain baked lighting only if that selection difference stays below one
// 8-bit channel step on the requested ledge and nearby lava wall.
const bakedLights = collectPointLights(nbt, states, [31,13,6]);
const entranceLights = collectPointLights(nbt, states, [20,13,37]);
let maxLightDifference = 0;
for (const p of [[15.5,12.5,42], [15.5,13.5,42], [15.5,14.5,42], [24.5,14.5,42.5]]) {
    const a = pointDiffuse(bakedLights,p), b = pointDiffuse(entranceLights,p);
    for (let i=0;i<3;++i) maxLightDifference = Math.max(maxLightDifference,Math.abs(a[i]-b[i]));
}
assert(maxLightDifference < 1/255, 'entrance lighting selection differs by less than one channel step at the target surfaces');
console.log(`Physical camera paths passed: ${inside} air samples, ${outside} intentional exterior opening samples; all page paths in air; entrance lighting difference=${maxLightDifference}.`);
