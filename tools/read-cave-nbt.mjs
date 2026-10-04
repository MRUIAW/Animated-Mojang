import { gunzipSync } from 'node:zlib';

export function readCaveNBT(compressed) {
    const bytes = gunzipSync(compressed, { maxOutputLength: 16 * 1024 * 1024 });
    let at = 0;
    const need = n => { if (n < 0 || at + n > bytes.length) throw Error('Truncated cave NBT'); };
    const number = (n, reader) => { need(n); const value = bytes[reader](at); at += n; return value; };
    const string = () => { const n = number(2, 'readUInt16BE'); need(n); const s = bytes.toString('utf8', at, at + n); at += n; return s; };
    const arraySize = () => { const n = number(4, 'readInt32BE'); if (n < 0 || n > 2_000_000) throw Error('Cave NBT array limit'); return n; };
    function payload(type, depth = 0) {
        if (depth > 32) throw Error('Cave NBT nesting limit');
        switch (type) {
        case 1: return number(1, 'readInt8');
        case 2: return number(2, 'readInt16BE');
        case 3: return number(4, 'readInt32BE');
        case 4: return number(8, 'readBigInt64BE');
        case 5: return number(4, 'readFloatBE');
        case 6: return number(8, 'readDoubleBE');
        case 7: { const n = arraySize(); need(n); const b = bytes.subarray(at, at + n); at += n; return b; }
        case 8: return string();
        case 9: { const element = number(1, 'readUInt8'), n = arraySize(); return Array.from({ length: n }, () => payload(element, depth + 1)); }
        case 10: {
            const result = Object.create(null);
            for (;;) { const kind = number(1, 'readUInt8'); if (!kind) return result; const key = string(); result[key] = payload(kind, depth + 1); }
        }
        case 11: return Array.from({ length: arraySize() }, () => number(4, 'readInt32BE'));
        case 12: return Array.from({ length: arraySize() }, () => number(8, 'readBigInt64BE'));
        default: throw Error('Unsupported cave NBT tag ' + type);
        }
    }
    if (number(1, 'readUInt8') !== 10) throw Error('Cave NBT root must be a compound');
    string();
    const root = payload(10);
    return root.Schematic ?? root;
}

export function decodeBlocks(data, count) {
    const result = new Uint32Array(count);
    let value = 0, shift = 0, next = 0;
    for (const byte of data) {
        if (shift > 28 || (shift === 28 && (byte & 0x7f) > 15)) throw Error('Cave VarInt overflow');
        value = (value | ((byte & 0x7f) << shift)) >>> 0;
        if (byte & 0x80) { shift += 7; continue; }
        if (next === count) throw Error('Extra cave block data');
        result[next++] = value;
        value = shift = 0;
    }
    if (shift || next !== count) throw Error('Incomplete cave block data');
    return result;
}
