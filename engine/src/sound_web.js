// sound_web.js -- Web Audio side of sound_web.cpp (ultima-assist).
// Emscripten JS library: link with --js-library.
//
// The engine decodes the game's sounds in C (stb_vorbis / sfx_gen) and hands
// PCM to these functions.  Nothing plays until the page calls
// Module.WebAudio.unlock() from a user gesture (browsers require that).
// The page also drives mute and volume through Module.WebAudio.setUi().

mergeInto(LibraryManager.library, {
  $WebAudio__postset: 'Module["WebAudio"] = WebAudio;',
  $WebAudio: {
    ctx: null, master: null, sfxGain: null, musicGain: null,
    pcm: {},                 // sound id -> { ch: [Float32Array], rate, frames }
    abuf: {},                // sound id -> { ctx, b: AudioBuffer }
    active: [],              // playing effect sources
    track: null,             // current music track { gain, next, sources }
    pendingFade: 0,
    ui: { mute: false, music: 0.6, sfx: 0.8 },     // set by the page
    eng: { music: 1, sfx: 1 },                     // set by the game's own volume settings
    onstate: null,

    unlock: function() {
      var A = WebAudio;
      if (!A.ctx) {
        var AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) return;
        try { A.ctx = new AC(); } catch (e) { return; }
        A.master = A.ctx.createGain();   A.master.connect(A.ctx.destination);
        A.sfxGain = A.ctx.createGain();  A.sfxGain.connect(A.master);
        A.musicGain = A.ctx.createGain(); A.musicGain.connect(A.master);
        A.apply();
        A.ctx.onstatechange = function() { if (A.onstate) A.onstate(A.running()); };
        // iOS only unlocks audio if something is played inside the gesture.
        try {
          var b = A.ctx.createBuffer(1, 1, 22050), s = A.ctx.createBufferSource();
          s.buffer = b; s.connect(A.ctx.destination); s.start(0);
        } catch (e) {}
      }
      if (A.ctx.state !== 'running') { try { A.ctx.resume(); } catch (e) {} }
    },
    running: function() { return !!WebAudio.ctx && WebAudio.ctx.state === 'running'; },
    apply: function() {
      var A = WebAudio;
      if (!A.ctx) return;
      A.master.gain.value = A.ui.mute ? 0 : 1;
      A.sfxGain.gain.value = A.ui.sfx * A.eng.sfx;
      A.musicGain.gain.value = A.ui.music * A.eng.music;
    },
    setUi: function(o) {
      var A = WebAudio;
      if (typeof o.mute === 'boolean') A.ui.mute = o.mute;
      if (typeof o.music === 'number') A.ui.music = Math.max(0, Math.min(1, o.music));
      if (typeof o.sfx === 'number') A.ui.sfx = Math.max(0, Math.min(1, o.sfx));
      A.apply();
    },

    getBuffer: function(id) {
      var A = WebAudio, c = A.abuf[id];
      if (c && c.ctx === A.ctx) return c.b;
      var p = A.pcm[id];
      if (!p || !A.ctx) return null;
      var b = A.ctx.createBuffer(p.ch.length, p.frames, p.rate);
      for (var i = 0; i < p.ch.length; i++) b.getChannelData(i).set(p.ch[i]);
      A.abuf[id] = { ctx: A.ctx, b: b };
      return b;
    },
    killTrack: function(t) {
      for (var i = 0; i < t.sources.length; i++) { try { t.sources[i].stop(); } catch (e) {} }
      try { t.gain.disconnect(); } catch (e) {}
    }
  },

  web_audio_init__deps: ['$WebAudio'],
  web_audio_init: function() {
    var A = WebAudio;
    if (A.pump) return;
    // Feed the music stream from C while the page is running.
    A.pump = setInterval(function() {
      if (A.running() && Module['_web_music_pump']) Module['_web_music_pump']();
    }, 120);
  },

  web_audio_ready__deps: ['$WebAudio'],
  web_audio_ready: function() { return WebAudio.running() ? 1 : 0; },

  // 16-bit interleaved PCM -> stored for later playback.
  web_audio_set_buffer__deps: ['$WebAudio'],
  web_audio_set_buffer: function(id, ptr, frames, channels, rate) {
    var ch = [], base = ptr >> 1;
    for (var c = 0; c < channels; c++) {
      var a = new Float32Array(frames);
      for (var i = 0; i < frames; i++) a[i] = HEAP16[base + i * channels + c] / 32768;
      ch.push(a);
    }
    WebAudio.pcm[id] = { ch: ch, rate: rate, frames: frames };
    delete WebAudio.abuf[id];
  },

  // Mono float PCM (synthesized sound effects).
  web_audio_set_buffer_f32__deps: ['$WebAudio'],
  web_audio_set_buffer_f32: function(id, ptr, frames, rate) {
    var a = new Float32Array(frames), base = ptr >> 2;
    for (var i = 0; i < frames; i++) a[i] = HEAPF32[base + i];
    WebAudio.pcm[id] = { ch: [a], rate: rate, frames: frames };
    delete WebAudio.abuf[id];
  },

  web_audio_play__deps: ['$WebAudio'],
  web_audio_play: function(id, limitMs) {
    var A = WebAudio;
    if (!A.running()) return;
    var b = A.getBuffer(id);
    if (!b) return;
    var s = A.ctx.createBufferSource();
    s.buffer = b;
    s.connect(A.sfxGain);
    s.onended = function() { var i = A.active.indexOf(s); if (i >= 0) A.active.splice(i, 1); };
    s.start();
    if (limitMs > 0) { try { s.stop(A.ctx.currentTime + limitMs / 1000); } catch (e) {} }
    A.active.push(s);
    if (A.active.length > 24) { try { A.active.shift().stop(); } catch (e) {} }
  },

  web_audio_stop_sounds__deps: ['$WebAudio'],
  web_audio_stop_sounds: function() {
    var A = WebAudio;
    while (A.active.length) { try { A.active.pop().stop(); } catch (e) {} }
  },

  web_audio_music_begin__deps: ['$WebAudio'],
  web_audio_music_begin: function(fadeInMs) {
    var A = WebAudio, t = A.track;
    A.track = null;
    A.pendingFade = fadeInMs | 0;
    if (t && A.ctx) {
      var now = A.ctx.currentTime;
      t.gain.gain.cancelScheduledValues(now);
      t.gain.gain.setValueAtTime(t.gain.gain.value, now);
      t.gain.gain.linearRampToValueAtTime(0, now + 0.08);
      setTimeout(function() { A.killTrack(t); }, 200);
    }
  },

  web_audio_music_end__deps: ['$WebAudio'],
  web_audio_music_end: function(fadeOutMs) {
    var A = WebAudio, t = A.track;
    A.track = null;
    A.pendingFade = 0;
    if (!t || !A.ctx) return;
    var ms = fadeOutMs > 0 ? fadeOutMs : 60, now = A.ctx.currentTime;
    t.gain.gain.cancelScheduledValues(now);
    t.gain.gain.setValueAtTime(t.gain.gain.value, now);
    t.gain.gain.linearRampToValueAtTime(0, now + ms / 1000);
    setTimeout(function() { A.killTrack(t); }, ms + 150);
  },

  // Planar float PCM in wasm memory (left, right) -> scheduled back to back.
  web_audio_music_queue__deps: ['$WebAudio'],
  web_audio_music_queue: function(left, right, frames, rate) {
    var A = WebAudio;
    if (!A.running()) return;
    var now = A.ctx.currentTime, t = A.track;
    if (!t) {
      var g = A.ctx.createGain();
      g.connect(A.musicGain);
      t = A.track = { gain: g, next: 0, sources: [] };
      if (A.pendingFade > 0) {
        g.gain.setValueAtTime(0, now);
        g.gain.linearRampToValueAtTime(1, now + A.pendingFade / 1000);
      }
      A.pendingFade = 0;
    }
    var b = A.ctx.createBuffer(2, frames, rate);
    b.getChannelData(0).set(HEAPF32.subarray(left >> 2, (left >> 2) + frames));
    b.getChannelData(1).set(HEAPF32.subarray(right >> 2, (right >> 2) + frames));
    var s = A.ctx.createBufferSource();
    s.buffer = b;
    s.connect(t.gain);
    var when = Math.max(t.next, now + 0.05);
    s.start(when);
    t.next = when + frames / rate;
    t.sources.push(s);
    s.onended = function() { var i = t.sources.indexOf(s); if (i >= 0) t.sources.splice(i, 1); };
  },

  // Milliseconds of music already scheduled; large while audio isn't running so
  // the engine doesn't decode anything yet.
  web_audio_music_buffered_ms__deps: ['$WebAudio'],
  web_audio_music_buffered_ms: function() {
    var A = WebAudio;
    if (!A.running()) return 100000;
    if (!A.track) return 0;
    return Math.max(0, Math.floor((A.track.next - A.ctx.currentTime) * 1000));
  },

  web_audio_set_volumes__deps: ['$WebAudio'],
  web_audio_set_volumes: function(music, sfx) {
    WebAudio.eng.music = music;
    WebAudio.eng.sfx = sfx;
    WebAudio.apply();
  },

  web_audio_suspend__deps: ['$WebAudio'],
  web_audio_suspend: function(on) {
    var A = WebAudio;
    if (!A.ctx) return;
    try { if (on) A.ctx.suspend(); else A.ctx.resume(); } catch (e) {}
  }
});
