// SPDX-License-Identifier: GPL-3.0-only
// FMini-1 on the SMK-37 in an AudioWorklet: felucca.wasm is the whole firmware built for the SMK-37 (web/emu/felucca_web.c
// with BOARD_SMK37, its input from web/emu/smk_web_input.h). Each render quantum asks it for 128 frames, which runs the
// device's clock forward; between quanta the page's input goes in and, about 30 times a second (4 when the page is
// hidden), the screen, the button lights, the pads' colours and any changed flash go out. The FM-1 one (../worklet.js)
// is the pattern.

// The flash the page keeps: the SMK-37's data area (settings, projects, the autosave: 0x9D000..0xBAFFF) and the preset
// slots in the user sample area (0xC2000..0xE9FFF), only the sectors that hold something. {offset: bytes}
export const STORE_AREAS = [[0x9D000, 0xBB000], [0xC2000, 0xEA000]];
export function storeSectors(nor) {
  const out = {};
  for (const [a, b] of STORE_AREAS)
    for (let off = a; off < b; off += 4096) {
      const s = nor.subarray(off, off + 4096);
      if (s.some((v) => v !== 0xFF)) out[off] = s.slice();
    }
  return out;
}
export function restoreSectors(nor, sectors) {
  for (const [k, v] of Object.entries(sectors || {})) {
    const off = +k, b = v instanceof Uint8Array ? v : new Uint8Array(v);
    if (STORE_AREAS.some(([a, e]) => off >= a && off + 4096 <= e && off % 4096 === 0) && b.length === 4096)
      nor.set(b, off);
  }
}

export const HOLD_MS = 24;     // a button change stays this long (device time) before the next: the UI reads them a frame

const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();
const nowSec = () => (typeof currentTime === "number" ? currentTime : clock() / 1000);
const Base = typeof AudioWorkletProcessor === "function" ? AudioWorkletProcessor : class { constructor() { this.port = { postMessage() {}, onmessage: null }; } };
{
  class FMiniSMK extends Base {
    constructor() {
      super();
      this.ex = null;
      this.sentDraws = -1;
      this.sentWrites = -1;
      this.sentLeds = "";
      this.lastFrame = 0;
      this.period = 1 / 30;
      this.queue = [];
      this.nextOk = 0;
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    async onMessage(m) {
      if (m.type === "load") {
        try {
          const { instance } = await WebAssembly.instantiate(m.wasm, {});
          const ex = instance.exports;
          ex._initialize();
          ex.web_nor_erase();
          restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), m.sectors);
          ex.web_smk_wheel(0, 535);
          ex.web_smk_wheel(1, 615);
          for (let i = 0; i < 4; i++) ex.web_smk_fader(i, m.faders ? m.faders[i] : 0);
          ex.web_boot();
          this.ex = ex;
          this.sentWrites = ex.web_flash_writes_count();
          this.port.postMessage({ type: "ready", rate: ex.web_sample_rate() });
        } catch (err) {
          this.port.postMessage({ type: "error", message: String(err && err.message || err) });
        }
        return;
      }
      const ex = this.ex;
      if (!ex) return;
      if (m.type === "buttons") this.queue.push(m);
      else if (m.type === "key") ex.web_smk_key(m.k, m.vel | 0);
      else if (m.type === "pad") ex.web_smk_pad(m.p, m.vel | 0);
      else if (m.type === "enc") ex.web_smk_enc(m.e, m.n | 0);
      else if (m.type === "fader") ex.web_smk_fader(m.i, m.v | 0);
      else if (m.type === "wheel") ex.web_smk_wheel(m.i, m.raw | 0);
      else if (m.type === "midi") ex.web_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0);
      else if (m.type === "visible") this.period = m.on ? 1 / 30 : 1 / 4;
    }

    publish() {
      const ex = this.ex, mem = ex.memory.buffer;
      const msg = { type: "frame" };
      const pads = Array.from(new Uint32Array(mem, ex.web_smk_pad_rgb(), 16));
      const leds = ex.web_smk_leds();
      const key = leds + ";" + pads.join(",");
      if (key !== this.sentLeds) {
        this.sentLeds = key;
        msg.leds = leds;
        msg.pads = pads;
      }
      const transfer = [];
      const draws = ex.web_screen_draws();
      if (draws !== this.sentDraws) {
        this.sentDraws = draws;
        msg.fb = new Uint16Array(mem, ex.web_screen(), 240 * 240).slice();
        transfer.push(msg.fb.buffer);
      }
      const writes = ex.web_flash_writes_count();
      if (writes !== this.sentWrites) {
        this.sentWrites = writes;
        msg.sectors = storeSectors(new Uint8Array(mem, ex.web_nor(), ex.web_nor_size()));
      }
      if (msg.leds !== undefined || msg.fb || msg.sectors) this.port.postMessage(msg, transfer);
    }

    input() {
      const ex = this.ex, now = ex.web_now_ms();
      while (this.queue.length && now - this.nextOk >= 0) {
        const m = this.queue.shift();
        ex.web_smk_buttons(m.mask >>> 0);
        this.nextOk = now + HOLD_MS;
      }
    }

    process(inputs, outputs) {
      const ex = this.ex;
      if (!ex) return true;
      if (this.queue.length) this.input();
      const out = outputs[0], n = out[0].length;
      ex.web_render(n);
      out[0].set(new Float32Array(ex.memory.buffer, ex.web_out_l(), n));
      if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.web_out_r(), n));
      if (nowSec() - this.lastFrame >= this.period) {
        this.lastFrame = nowSec();
        this.publish();
      }
      return true;
    }
  }
  if (typeof registerProcessor === "function") registerProcessor("fmini-smk", FMiniSMK);
  globalThis.FMiniSMKCore = FMiniSMK;
}
export const FMiniSMKCore = globalThis.FMiniSMKCore;
