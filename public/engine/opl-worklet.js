// OPL3 synthesis on the audio thread. SPDX-License-Identifier: GPL-2.0-or-later
//
// Runs public/engine/opl3.wasm (Nuked OPL3, built from opl_worklet_chip.c)
// inside an AudioWorklet. The engine, on the main thread, schedules register
// writes ahead of time as (frame, register, value) triples stamped in this
// context's frame clock; each is applied at exactly that frame, so timing does
// not depend on how promptly the main thread runs. See src/opl_worklet.c.

class OPLProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const module = new WebAssembly.Module(options.processorOptions.wasm);
    const { exports } = new WebAssembly.Instance(module, {});
    if (exports._initialize) exports._initialize();
    exports.opl_reset(sampleRate);
    this.exports = exports;
    this.memory = exports.memory;
    this.writes = []; // Float64Array triples, in schedule order.
    this.index = 0; // Next unapplied triple in writes[0].
    this.muted = false;
    this.port.onmessage = ({ data }) => {
      if (data.type === 'writes') this.writes.push(data.triples);
      else if (data.type === 'mute') this.muted = data.muted;
    };
  }

  // Apply every write due at or before `frame`; return frames until the next.
  applyDue(frame) {
    for (;;) {
      const batch = this.writes[0];
      if (!batch) return Infinity;
      if (this.index >= batch.length) {
        this.writes.shift();
        this.index = 0;
        continue;
      }
      const due = batch[this.index];
      if (due > frame) return due - frame;
      this.exports.opl_write(batch[this.index + 1], batch[this.index + 2]);
      this.index += 3;
    }
  }

  process(inputs, outputs) {
    const [left, right] = outputs[0];
    const frames = left.length;
    let done = 0;
    while (done < frames) {
      const count = Math.min(frames - done, Math.ceil(this.applyDue(currentFrame + done)));
      const pointer = this.exports.opl_render(count);
      const samples = new Int16Array(this.memory.buffer, pointer, count * 2);
      for (let i = 0; i < count; i++) {
        left[done + i] = samples[2 * i] / 32768;
        right[done + i] = samples[2 * i + 1] / 32768;
      }
      done += count;
    }
    if (this.muted) {
      left.fill(0);
      right.fill(0);
    }
    return true;
  }
}

registerProcessor('doom-opl', OPLProcessor);
