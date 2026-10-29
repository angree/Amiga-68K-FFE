#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 angree. See LICENSE at the repository root.
"""
Builds ffe_gm.bnk, the General MIDI sample bank gm_synth.c plays.

EVERY RECORDED SAMPLE IS CC0. Two repositories, both by Versilian Studios,
both carrying the CC0 1.0 Universal dedication in their LICENSE file, are
read at PINNED commits so the bank can be rebuilt bit for bit:

  VCSL        the Versilian Community Sample Library  (the primary source)
  VSCO-2-CE   the VS Chamber Orchestra Community Edition (strings, brass,
              orchestral woodwinds -- the families VCSL does not have)

What neither has -- synth leads and pads, electric guitars and basses,
choir, sound effects, a drum-kit kick, whistle and cuica -- is GENERATED
below from integer-frequency additive synthesis and a seeded LCG noise
source. That output is ours and falls under this file's MIT licence.
GMBANK-SOURCES.md, written by this script next to the bank, lists every
input file with its repository path and commit, and every generated sound.

Downloads go to a cache (default E:\\ffe\\gmsrc\\cache), never into the repo.

---- what the build does to a recorded sample ------------------------------

  read RIFF WAVE (8/16/24/32-bit PCM or float) -> mono float
  trim leading silence, cut to the length the instrument needs
  resample (windowed sinc) to the zone rate, 11025..22050 Hz
  'sustain' : crossfade loop in the steady part, sample ends at loop end
  'decay'   : natural attack and decay up to the loop start, then a
              level-flattened crossfade loop; the synth's envelope does the
              rest of the decay at the rate MEASURED from the sample
  'oneshot' : cut with a fade, no loop (drums)
  peak-normalise to 8-bit signed; loudness is evened out by a per-zone gain

The root key comes from the file name, but octave conventions differ between
the instruments of the set (the Kawai piano calls its bottom note "A#-1", the
VSCO cello section calls cello C "C1"), so the octave is DETECTED: a YIN pitch
estimate of every file votes for the octave offset of its source folder.

---- bank format (all fields big-endian; see gm_synth.h) -------------------

  header   48 bytes     'GMBK', version, counts, table offsets
  programs 128 x 12     instrument, transpose, volume, envelope
  drums    128 x 8      channel-10 note -> instrument, pitch, volume, group
  insts    n x 20       name, first zone, zone count
  zones    n x 28       key range, root, rate, gain, loop, decay
  data                  8-bit signed samples, each followed by one guard byte
"""

import argparse
import json
import math
import os
import re
import struct
import sys
import urllib.parse
import urllib.request

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))

REPOS = {
	'vcsl': ('sgossner', 'VCSL', 'c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e'),
	'vsco': ('sgossner', 'VSCO-2-CE', '440300901dfe9275fd84e0b7763af1f8443ae62e'),
}

BANK_VERSION = 1
HDR_SIZE = 48
PROG_SIZE = 12
DRUM_SIZE = 8
INST_SIZE = 20
ZONE_SIZE = 28

ZF_LOOP = 1		# zone loops between loop_start and loop_end
ZF_TAIL = 2		# natural decay before the loop; synth decay starts at the loop

ATT_SILENT = 4096	# attenuation unit: 1/256 octave of amplitude (6.02 dB = 256)

NOTE_PC = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def log(*a):
	print(*a, flush=True)


# ---- sources ----------------------------------------------------------------

class Source:
	def __init__(self, cache):
		self.cache = cache
		self.trees = {}
		self.used = []		# (repo, path) in the order they were used

	def tree(self, repo):
		if repo in self.trees:
			return self.trees[repo]
		owner, name, sha = REPOS[repo]
		fn = os.path.join(self.cache, '%s-%s-tree.json' % (repo, sha[:12]))
		if not os.path.exists(fn):
			url = 'https://api.github.com/repos/%s/%s/git/trees/%s?recursive=1' % (owner, name, sha)
			log('fetch tree', url)
			with urllib.request.urlopen(url) as r:
				data = r.read()
			j = json.loads(data)
			if j.get('truncated'):
				raise SystemExit('tree listing of %s came back truncated' % repo)
			os.makedirs(self.cache, exist_ok=True)
			with open(fn + '.tmp', 'wb') as f:
				f.write(data)
			os.replace(fn + '.tmp', fn)
		with open(fn, 'rb') as f:
			j = json.load(f)
		paths = [e['path'] for e in j['tree'] if e['type'] == 'blob']
		self.trees[repo] = paths
		return paths

	def fetch(self, repo, path):
		owner, name, sha = REPOS[repo]
		local = os.path.join(self.cache, repo, *path.split('/'))
		if not os.path.exists(local):
			url = 'https://raw.githubusercontent.com/%s/%s/%s/%s' % (
				owner, name, sha, urllib.parse.quote(path))
			log('  download', repo, path)
			with urllib.request.urlopen(url) as r:
				data = r.read()
			os.makedirs(os.path.dirname(local), exist_ok=True)
			with open(local + '.tmp', 'wb') as f:
				f.write(data)
			os.replace(local + '.tmp', local)
		if (repo, path) not in self.used:
			self.used.append((repo, path))
		return local

	def find(self, repo, basename):
		hits = [p for p in self.tree(repo) if p.rsplit('/', 1)[-1] == basename]
		if len(hits) != 1:
			raise SystemExit('%s: %r matches %d files' % (repo, basename, len(hits)))
		return hits[0]

	def listdir(self, repo, folder, pattern):
		folder = folder.rstrip('/') + '/'
		rx = re.compile(pattern)
		out = []
		for p in self.tree(repo):
			if not p.startswith(folder) or not p.lower().endswith('.wav'):
				continue
			rest = p[len(folder):]
			if '/' in rest:
				continue
			if rx.search(rest):
				out.append(p)
		return sorted(out)


# ---- WAV I/O and DSP --------------------------------------------------------

def read_wav(fn):
	with open(fn, 'rb') as f:
		data = f.read()
	if data[0:4] != b'RIFF' or data[8:12] != b'WAVE':
		raise SystemExit('%s: not a RIFF WAVE file' % fn)
	pos = 12
	fmt = None
	raw = None
	while pos + 8 <= len(data):
		cid = data[pos:pos + 4]
		sz = struct.unpack('<I', data[pos + 4:pos + 8])[0]
		body = data[pos + 8:pos + 8 + sz]
		if cid == b'fmt ':
			tag, ch, sr, _, _, bits = struct.unpack('<HHIIHH', body[:16])
			if tag == 0xFFFE and len(body) >= 26:
				tag = struct.unpack('<H', body[24:26])[0]
			fmt = (tag, ch, sr, bits)
		elif cid == b'data':
			raw = body
		pos += 8 + sz + (sz & 1)
	if fmt is None or raw is None:
		raise SystemExit('%s: no fmt/data chunk' % fn)
	tag, ch, sr, bits = fmt
	bps = bits // 8
	n = len(raw) // (bps * ch)
	raw = raw[:n * bps * ch]
	if tag == 3 and bits == 32:
		x = np.frombuffer(raw, '<f4').astype(np.float64)
	elif tag == 1 and bits == 16:
		x = np.frombuffer(raw, '<i2').astype(np.float64) / 32768.0
	elif tag == 1 and bits == 24:
		b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
		v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
		v = np.where(v >= 1 << 23, v - (1 << 24), v)
		x = v.astype(np.float64) / float(1 << 23)
	elif tag == 1 and bits == 32:
		x = np.frombuffer(raw, '<i4').astype(np.float64) / 2147483648.0
	elif tag == 1 and bits == 8:
		x = (np.frombuffer(raw, np.uint8).astype(np.float64) - 128.0) / 128.0
	else:
		raise SystemExit('%s: unsupported WAVE format tag %d, %d bits' % (fn, tag, bits))
	x = x.reshape(-1, ch).mean(axis=1)
	return x, sr


def resample(x, sr_in, sr_out):
	"""Windowed-sinc resampler (Blackman, 32 taps a side). Offline only."""
	if sr_in == sr_out:
		return x.copy()
	ratio = sr_in / sr_out
	n_out = int(len(x) / ratio)
	cut = min(1.0, sr_out / sr_in) * 0.92
	half = 32
	t = np.arange(n_out) * ratio
	base = np.floor(t).astype(np.int64)
	frac = t - base
	xp = np.concatenate([np.zeros(half + 1), x, np.zeros(half + 2)])
	y = np.zeros(n_out)
	for k in range(-half, half + 1):
		d = frac - k
		w = 0.42 + 0.5 * np.cos(np.pi * d / (half + 1)) + 0.08 * np.cos(2 * np.pi * d / (half + 1))
		y += xp[base + k + half + 1] * cut * np.sinc(cut * d) * w
	return y


def rms_env(x, rate, win_s=0.02):
	w = max(1, int(win_s * rate))
	k = np.ones(w) / w
	return np.sqrt(np.convolve(x * x, k, mode='same') + 1e-12)


def yin_f0(x, sr):
	"""YIN fundamental estimate of a 250 ms window after the attack."""
	start = int(0.12 * sr)
	if len(x) < start + int(0.3 * sr):
		start = 0
	seg = x[start:start + int(0.3 * sr)]
	if len(seg) < 2048:
		return None
	maxlag = int(sr / 26.0)
	minlag = int(sr / 4500.0)
	W = len(seg) - maxlag
	if W < maxlag:
		maxlag = len(seg) // 2
		W = len(seg) - maxlag
	a = seg[:W]
	nfft = 1 << int(math.ceil(math.log2(len(seg) + W)))
	r = np.fft.irfft(np.fft.rfft(seg, nfft) * np.conj(np.fft.rfft(a, nfft)), nfft)[:maxlag + 1]
	c = np.concatenate([[0.0], np.cumsum(seg * seg)])
	e1 = c[W]
	e2 = c[np.arange(maxlag + 1) + W] - c[np.arange(maxlag + 1)]
	d = e1 + e2 - 2 * r
	d[0] = 0
	cm = np.cumsum(d[1:])
	dn = np.ones(maxlag + 1)
	dn[1:] = d[1:] * np.arange(1, maxlag + 1) / np.maximum(cm, 1e-12)
	tau = None
	for t in range(max(2, minlag), maxlag):
		if dn[t] < 0.12:
			while t + 1 < maxlag and dn[t + 1] < dn[t]:
				t += 1
			tau = t
			break
	if tau is None:
		tau = int(np.argmin(dn[max(2, minlag):maxlag])) + max(2, minlag)
		if dn[tau] > 0.4:
			return None
	if 1 <= tau < maxlag:
		a0, b0, c0 = dn[tau - 1], dn[tau], dn[tau + 1]
		den = a0 - 2 * b0 + c0
		off = 0.5 * (a0 - c0) / den if abs(den) > 1e-12 else 0.0
	else:
		off = 0.0
	return sr / (tau + off)


def midi_hz(m):
	return 440.0 * 2.0 ** ((m - 69) / 12.0)


def parse_note(fname):
	stem = fname.rsplit('.', 1)[0]
	for tok in re.split(r'[_ ]', stem):
		m = re.fullmatch(r'([A-Ga-g])(#|b)?(-?\d)', tok)
		if m:
			pc = NOTE_PC[m.group(1).upper()]
			if m.group(2) == '#':
				pc += 1
			elif m.group(2) == 'b':
				pc -= 1
			return (int(m.group(3)) + 1) * 12 + pc
	return None


# ---- loops ------------------------------------------------------------------

def best_loop_len(x, ls, L0, period, rate):
	"""Pick a loop length near L0 whose end matches the start best."""
	w = int(max(32, min(0.01 * rate, 3 * period + 32)))
	span = int(period / 2 + 1)
	if ls - w < 0 or ls + L0 + span + w >= len(x):
		return L0
	ref = x[ls - w:ls + w]
	best, bl = -2.0, L0
	for L in range(max(16, L0 - span), L0 + span + 1):
		s = ls + L
		if s + w > len(x):
			break
		seg = x[s - w:s + w]
		den = math.sqrt(float(np.dot(ref, ref) * np.dot(seg, seg))) + 1e-12
		c = float(np.dot(ref, seg)) / den
		if c > best:
			best, bl = c, L
	return bl


def crossfade_loop(x, ls, L, xf):
	"""Blend the pre-loop audio into the loop's end so the jump is seamless."""
	y = x[:ls + L].copy()
	xf = min(xf, ls, L // 2)
	if xf > 0:
		w = np.arange(xf) / xf
		y[ls + L - xf:ls + L] = x[ls + L - xf:ls + L] * (1 - w) + x[ls - xf:ls] * w
	return y


def flatten_from(x, ls, xf, rate):
	"""Hold the level of a decaying sound at its value at ls from ls on."""
	env = rms_env(x, rate, 0.03)
	ref = env[ls]
	g = np.ones(len(x))
	tail = np.arange(len(x)) >= ls - 2 * xf
	g[tail] = ref / np.maximum(env[tail], ref * 1e-3)
	ramp = np.clip((np.arange(len(x)) - (ls - 2 * xf)) / max(1, xf), 0, 1)
	g = g ** ramp
	return x * g


def decay_time(x, rate, t0, t1):
	"""Seconds for a 96 dB fall, fitted to the RMS envelope over [t0, t1] of
	the UNCUT recording -- the stretch the synth's envelope replaces -- and
	only where it is above a -60 dB floor. None when there is too little."""
	env = rms_env(x, rate, 0.03)
	a, b = int(t0 * rate), min(len(x), int(t1 * rate))
	if b - a < int(0.1 * rate):
		return None
	seg = env[a:b]
	keep = seg > np.max(env) * 1e-3
	if int(np.sum(keep)) < int(0.1 * rate):
		return None
	db = 20 * np.log10(seg[keep])
	t = (np.arange(a, b) / rate)[keep]
	slope = np.polyfit(t, db, 1)[0]
	if slope >= -96.0 / 30.0:
		return 30.0
	return float(max(0.15, 96.0 / -slope))


def smooth_decays(zones):
	"""Even out the measured decays of one instrument: a straight line in
	log(decay) over the root key, each zone held within x1.6 of it. One
	recording with a long ring-out must not make one key sing for 30 s."""
	pts = [(z.root, math.log(z.decay_s)) for z in zones if z.decay_s]
	if not pts:
		for z in zones:
			z.decay_s = 2.0
		return
	if len(pts) >= 3:
		k, c = np.polyfit([p[0] for p in pts], [p[1] for p in pts], 1)
		fit = lambda r: math.exp(k * r + c)
	else:
		m = math.exp(float(np.median([p[1] for p in pts])))
		fit = lambda r: m
	for z in zones:
		f = fit(z.root)
		d = z.decay_s or f
		z.decay_s = float(np.clip(d, f / 1.6, f * 1.6))
		z.decay_s = float(np.clip(z.decay_s, 0.2, 30.0))


# ---- zones ------------------------------------------------------------------

class Zone:
	def __init__(self, data, rate, root, loop=None, tail=False, decay_s=0.0, fine=0, label=''):
		self.data = data		# float, peak-normalised later
		self.rate = rate
		self.root = root
		self.loop = loop		# (start, end) or None
		self.tail = tail
		self.decay_s = decay_s
		self.fine = fine
		self.label = label
		self.lo = 0
		self.hi = 127
		self.gain = 128


def zone_rms(z):
	"""Loudness for gain matching: over the loop of a sustained sound, over
	the first 250 ms after the peak of everything that decays."""
	x = z.data
	if z.loop and not z.tail:
		seg = x[z.loop[0]:z.loop[1]]
	else:
		pk = int(np.argmax(np.abs(x)))
		seg = x[pk:pk + int(0.25 * z.rate)]
	return math.sqrt(float(np.mean(seg * seg))) if len(seg) else 0.0


def finish_oneshot(x, rate, max_s, fade_s=0.03):
	n = min(len(x), int(max_s * rate))
	y = x[:n].copy()
	f = min(n, int(fade_s * rate))
	if f > 0:
		y[n - f:] *= np.linspace(1, 0, f)
	return y


def trim_lead(x, sr):
	pk = np.max(np.abs(x)) + 1e-12
	idx = np.nonzero(np.abs(x) > pk * 0.01)[0]
	s = int(idx[0]) if len(idx) else 0
	return x[max(0, s - int(0.002 * sr)):]


def make_zone(x, sr, root, rate, mode, ls_s=0.3, loop_s=0.3, max_s=1.0, label=''):
	x = trim_lead(x, sr)
	natural = decay_time(x, sr, ls_s, ls_s + 2.5) if mode == 'decay' else None
	need = max_s if mode == 'oneshot' else ls_s + loop_s + 0.1
	x = x[:int((need + 0.05) * sr)]
	y = resample(x, sr, rate)
	if mode == 'oneshot':
		return Zone(finish_oneshot(y, rate, max_s), rate, root, label=label)
	period = rate / midi_hz(root) if root is not None else None
	ls = int(ls_s * rate)
	if ls + int(loop_s * rate) + 64 > len(y):
		ls = max(int(0.05 * rate), len(y) - int(loop_s * rate) - 64)
	if period:
		L0 = max(1, round(loop_s * rate / period)) * period
		L0 = int(round(L0))
	else:
		L0 = int(loop_s * rate)
	L0 = min(L0, len(y) - ls - 2)
	xf = int(min(0.4 * L0, 0.08 * rate))
	decay_s = 0.0
	if mode == 'decay':
		decay_s = natural
		y = flatten_from(y, ls, xf, rate)
	L = best_loop_len(y, ls, L0, period, rate) if period else L0
	y = crossfade_loop(y, ls, L, xf)
	return Zone(y, rate, root, loop=(ls, ls + L), tail=(mode == 'decay'),
		decay_s=decay_s, label=label)


# ---- sampled instruments ----------------------------------------------------

class Sampled:
	"""One instrument from recorded files.

	srcs: list of (repo, folder, regex, lo, hi) -- the files in folder whose
	name matches regex may provide zones for roots in [lo, hi]. An optional
	sixth element forces the octave offset instead of voting -- tubular bells,
	whose perceived strike tone is not the fundamental YIN finds.

	tune: measure every picked file with YIN and correct the zone's fine tune
	(off for glockenspiel and bells, whose overtones are inharmonic).
	"""
	def __init__(self, name, srcs, mode, spacing=6, rates=(16000, 22050, 72),
	             ls=0.3, loop=0.3, max_s=1.0, vol=1.0, tune=True):
		self.name, self.srcs, self.mode = name, srcs, mode
		self.tune = tune
		self.spacing, self.rates = spacing, rates
		self.ls, self.loop_s, self.max_s, self.vol = ls, loop, max_s, vol

	def rate_for(self, root):
		lo, hi, split = self.rates
		return hi if root >= split else lo

	def pick(self, dedup):
		"""Greedy, spacing apart: the next pick is the candidate nearest
		last+spacing among those at least half a spacing above the last."""
		pick = [dedup[0]]
		while True:
			last = pick[-1][0]
			rest = [c for c in dedup if c[0] >= last + max(2, self.spacing // 2)]
			if not rest:
				break
			t = last + self.spacing
			pick.append(min(rest, key=lambda c: (abs(c[0] - t), c[0])))
		return pick

	def build(self, src):
		cands = []
		for s in self.srcs:
			repo, folder, rx, lo, hi = s[:5]
			force = s[5] if len(s) > 5 else None
			files = src.listdir(repo, folder, rx)
			if not files:
				raise SystemExit('%s: no files in %s/%s matching %s' % (self.name, repo, folder, rx))
			named = [(parse_note(f.rsplit('/', 1)[-1]), f) for f in files]
			named = [(m, f) for m, f in named if m is not None]
			# octave vote: detect a handful of files spread over the range
			votes = {}
			probe = named[::max(1, len(named) // 6)][:8]
			for m, f in probe:
				x, sr = read_wav(src.fetch(repo, f))
				f0 = yin_f0(trim_lead(x, sr), sr)
				if not f0:
					continue
				det = 69 + 12 * math.log2(f0 / 440.0)
				off = int(round((det - m) / 12.0)) * 12
				votes[off] = votes.get(off, 0) + 1
			if force is not None:
				off = force
			elif not votes:
				raise SystemExit('%s: no pitch could be detected in %s' % (self.name, folder))
			else:
				off = max(sorted(votes), key=lambda k: votes[k])
			log('  %-10s %s octave offset %+d (votes %s%s)' % (self.name, folder, off, votes,
				', FORCED' if force is not None else ''))
			for m, f in named:
				m2 = m + off
				if lo <= m2 <= hi:
					cands.append((m2, repo, f))
		cands.sort()
		dedup = []
		for c in cands:
			if not dedup or dedup[-1][0] != c[0]:
				dedup.append(c)
		if not dedup:
			raise SystemExit('%s: no file has a root inside its source range' % self.name)
		# Pick, measure the tuning of what was picked, throw out any file that
		# is more than 0.6 semitone off its name (a mislabelled sample), repeat.
		tuning = {}
		while True:
			pick = self.pick(dedup)
			bad = []
			for m, repo, f in pick:
				if not self.tune or f in tuning:
					continue
				x, sr = read_wav(src.fetch(repo, f))
				f0 = yin_f0(trim_lead(x, sr), sr)
				if not f0:
					tuning[f] = 0.0
					continue
				dev = 69 + 12 * math.log2(f0 / 440.0) - m
				dev -= 12 * round(dev / 12.0)		# YIN may land an octave off
				if min(abs(abs(dev) - 4.98), abs(abs(dev) - 7.02)) < 0.2:
					dev = 0.0			# ... or on a 3:2 ratio: a miss, not a fact
				tuning[f] = dev
				if abs(dev) > 0.6:
					log('  %-10s REJECTED %s: %+.2f semitones off its name' % (self.name, f, dev))
					bad.append(f)
			if not bad:
				break
			dedup = [c for c in dedup if c[2] not in bad]
		zones = []
		for m, repo, f in pick:
			x, sr = read_wav(src.fetch(repo, f))
			z = make_zone(x, sr, m, self.rate_for(m), self.mode, self.ls, self.loop_s,
				self.max_s, label='%s:%s' % (repo, f))
			z.fine = -int(round(tuning.get(f, 0.0) * 100))
			zones.append(z)
		if self.mode == 'decay':
			smooth_decays(zones)
		return zones


class OneShot:
	"""A single recorded hit (drums and melodic percussion)."""
	def __init__(self, name, repo, basename, max_s=0.6, rate=22050, vol=1.0, reverse=False):
		self.name, self.repo, self.basename = name, repo, basename
		self.max_s, self.rate, self.vol, self.reverse = max_s, rate, vol, reverse
		self.mode = 'oneshot'

	def build(self, src):
		path = src.find(self.repo, self.basename)
		x, sr = read_wav(src.fetch(self.repo, path))
		if self.reverse:
			x = trim_lead(x, sr)[:int(self.max_s * sr)][::-1].copy()
			y = resample(x, sr, self.rate)
			f = int(0.05 * self.rate)
			y[:f] *= np.linspace(0, 1, f)
			y[-int(0.01 * self.rate):] *= np.linspace(1, 0, int(0.01 * self.rate))
			return [Zone(y, self.rate, 60, label='%s:%s (reversed)' % (self.repo, path))]
		return [make_zone(x, sr, 60, self.rate, 'oneshot', max_s=self.max_s,
			label='%s:%s' % (self.repo, path))]


def spectral_peak(x, sr, fmin, fmax):
	"""Strongest spectral peak in [fmin, fmax] over 80..800 ms -- for drums
	with a pitch (timpani), where YIN finds no periodicity in the inharmonic
	partials."""
	a, b = int(0.08 * sr), min(len(x), int(0.8 * sr))
	seg = x[a:b] * np.hanning(b - a)
	n = 1 << 17
	sp = np.abs(np.fft.rfft(seg, n))
	fr = np.arange(len(sp)) * sr / n
	m = (fr > fmin) & (fr < fmax)
	return float(fr[m][int(np.argmax(sp[m]))])


class DetectedHits:
	"""Recorded pitched hits whose file names carry no note (VCSL timpani):
	the root is the strongest low spectral peak, rounded to the semitone."""
	def __init__(self, name, repo, basenames, max_s=1.5, rate=11025, vol=1.0):
		self.name, self.repo, self.basenames = name, repo, basenames
		self.max_s, self.rate, self.vol = max_s, rate, vol
		self.mode = 'oneshot'

	def build(self, src):
		zones = []
		for b in self.basenames:
			path = src.find(self.repo, b)
			x, sr = read_wav(src.fetch(self.repo, path))
			f0 = spectral_peak(trim_lead(x, sr), sr, 60.0, 400.0)
			det = 69 + 12 * math.log2(f0 / 440.0)
			root = int(round(det))
			fine = -int(round((det - root) * 100))	# zone fine tune CORRECTS the deviation
			log('  %-10s %s detected %.1f Hz -> root %d %+d cents' % (self.name, b, f0, root, fine))
			z = make_zone(x, sr, root, self.rate, 'oneshot', max_s=self.max_s,
				label='%s:%s' % (self.repo, path))
			z.fine = fine
			zones.append(z)
		roots = [z.root for z in zones]
		if len(set(roots)) != len(roots):
			raise SystemExit('%s: two hits detected at the same root %s' % (self.name, roots))
		return zones


class LoopedNoise:
	"""A recorded unpitched sustain (ocean drum), crossfade-looped."""
	def __init__(self, name, repo, basename, ls=0.5, loop=1.5, rate=16000, vol=1.0):
		self.name, self.repo, self.basename = name, repo, basename
		self.ls, self.loop_s, self.rate, self.vol = ls, loop, rate, vol
		self.mode = 'sustain'

	def build(self, src):
		path = src.find(self.repo, self.basename)
		x, sr = read_wav(src.fetch(self.repo, path))
		return [make_zone(x, sr, None, self.rate, 'sustain', self.ls, self.loop_s,
			label='%s:%s' % (self.repo, path))]


# ---- generated instruments (ours, MIT) --------------------------------------

class Lcg:
	"""Deterministic noise: the numbers do not depend on the numpy version."""
	def __init__(self, seed):
		self.s = seed & 0xFFFFFFFF

	def noise(self, n):
		out = np.empty(n)
		s = self.s
		for i in range(n):
			s = (s * 1664525 + 1013904223) & 0xFFFFFFFF
			out[i] = (s >> 8) / 8388608.0 - 1.0
		self.s = s
		return out


def onepole_lp(x, a):
	y = np.empty_like(x)
	acc = 0.0
	for i in range(len(x)):
		acc += a * (x[i] - acc)
		y[i] = acc
	return y


def additive(f0, rate, partials, dur, loop_t, loop_len, detune=(0,), attack=0.005,
             drive=0.0):
	"""Sum of integer-Hz partials; every amplitude is frozen from loop_t on.

	partials: list of (multiple, amplitude, tau) -- tau is the decay time
	constant in seconds before loop_t (None = steady). With f0 a multiple of
	5 Hz and integer multiples, every partial completes whole cycles in a
	0.2 s loop; with integer detunes the loop must be 1 s. So the loop is
	exact and needs no crossfade."""
	ls = int(round(loop_t * rate))
	le = ls + int(round(loop_len * rate))
	n = max(le, int(dur * rate))
	t = np.arange(n) / rate
	te = np.minimum(t, loop_t)
	y = np.zeros(n)
	ny = rate * 0.5
	for d in detune:
		for mult, amp, tau in partials:
			f = f0 * mult + d
			if f >= ny * 0.70:
				continue
			env = amp * (np.exp(-te / tau) if tau else 1.0)
			y += env * np.sin(2 * np.pi * f * t + 0.7 * mult + 1.3 * d)
	y /= len(detune)
	if attack > 0:
		y *= np.minimum(1.0, t / attack)
	if drive > 0:
		y = np.tanh(drive * y / (np.max(np.abs(y)) + 1e-12))
	return y[:le], (ls, le)


def formant_amp(f, formants):
	a = 0.0
	for fc, bw, g in formants:
		a += g / (1.0 + ((f - fc) / bw) ** 2)
	return a


class Generated:
	"""A pitched instrument from additive synthesis, zones on A notes."""
	def __init__(self, name, partial_fn, roots=(33, 45, 57, 69, 81), rate=22050,
	             dur=None, loop_t=0.3, loop_len=0.2, detune=(0,), attack=0.005,
	             drive=0.0, tail=False, decay_s=0.0, vol=1.0, low_rate=None):
		self.name, self.partial_fn, self.roots = name, partial_fn, roots
		self.rate, self.loop_t, self.loop_len = rate, loop_t, loop_len
		self.detune, self.attack, self.drive = detune, attack, drive
		self.tail, self.decay_s, self.vol = tail, decay_s, vol
		self.low_rate = low_rate
		self.mode = 'decay' if tail else 'sustain'

	def build(self, src):
		zones = []
		for r in self.roots:
			f0 = midi_hz(r)
			assert abs(f0 - round(f0 / 5) * 5) < 1e-6, f0
			rate = self.low_rate if (self.low_rate and r < 45) else self.rate
			y, lp = additive(round(f0), rate, self.partial_fn(round(f0)), self.loop_t + self.loop_len,
				self.loop_t, self.loop_len, self.detune, self.attack, self.drive)
			z = Zone(y, rate, r, loop=lp, tail=self.tail, decay_s=self.decay_s,
				label='generated: %s at %d Hz' % (self.name, round(f0)))
			zones.append(z)
		return zones


class GenNoise:
	"""An unpitched generated sound: fn(rate) -> (signal, loop or None)."""
	def __init__(self, name, fn, rate=22050, vol=1.0, desc=''):
		self.name, self.fn, self.rate, self.vol, self.desc = name, fn, rate, vol, desc
		self.mode = 'oneshot'

	def build(self, src):
		y, lp = self.fn(self.rate)
		return [Zone(y, self.rate, 60, loop=lp, label='generated: %s (%s)' % (self.name, self.desc))]


def p_saw(n, roll=1.0):
	return lambda f0: [(k, 1.0 / k ** roll, None) for k in range(1, n + 1)]


def p_square(n):
	return lambda f0: [(k, 1.0 / k, None) for k in range(1, n + 1, 2)]


def p_organ(f0):
	bars = [(1, 1.0), (2, 0.8), (3, 0.55), (4, 0.45), (6, 0.3), (8, 0.25), (10, 0.12), (12, 0.1)]
	return [(m, a, None) for m, a in bars]


def p_pluck(n, roll, tau0, tau_k):
	def f(f0):
		out = []
		for k in range(1, n + 1):
			comb = abs(math.sin(math.pi * k * 0.18)) + 0.15
			out.append((k, comb / k ** roll, tau0 / (1.0 + tau_k * (k - 1))))
		return out
	return f


def p_bright_saw(n, roll, settle):
	"""Saw whose upper harmonics settle to a darker steady level (filter attack)."""
	def f(f0):
		out = []
		for k in range(1, n + 1):
			steady = 1.0 / k ** roll
			out.append((k, steady, None))
			if k > 1:
				out.append((k, (1.0 / k ** 0.9 - steady), settle))
		return out
	return f


def p_choir(f0):
	fm = [(700, 110, 1.0), (1220, 140, 0.55), (2600, 220, 0.25), (3300, 300, 0.1)]
	out = []
	k = 1
	while f0 * k < 5000:
		out.append((k, formant_amp(f0 * k, fm) * (1.0 / k ** 0.3), None))
		k += 1
	return out


def p_steel(f0):
	return [(1, 1.0, 1.2), (2, 0.6, 0.6), (3, 0.15, 0.3), (4, 0.4, 0.35), (6, 0.1, 0.2)]


def p_bell(f0):
	return [(1, 1.0, 2.5), (2, 0.5, 1.5), (3, 0.35, 0.9), (5, 0.25, 0.5), (8, 0.12, 0.3)]


def g_kick(rate):
	n = int(0.45 * rate)
	t = np.arange(n) / rate
	f = 45 + 110 * np.exp(-t / 0.035)
	ph = 2 * np.pi * np.cumsum(f) / rate
	y = np.sin(ph) * np.exp(-t / 0.16)
	click = Lcg(11).noise(n) * np.exp(-t / 0.003) * 0.4
	return finish_oneshot(y + click, rate, 0.45, 0.02), None


def g_whistle(f_hz, dur):
	def f(rate):
		n = int(dur * rate)
		t = np.arange(n) / rate
		fr = f_hz * (1 + 0.01 * np.sin(2 * np.pi * 30 * t))
		y = np.sin(2 * np.pi * np.cumsum(fr) / rate) + 0.08 * Lcg(23).noise(n)
		y *= np.minimum(1, t / 0.01)
		return finish_oneshot(y, rate, dur, 0.04), None
	return f


def g_cuica(f_a, f_b, dur):
	def f(rate):
		n = int(dur * rate)
		t = np.arange(n) / rate
		fr = f_a + (f_b - f_a) * np.minimum(1, t / (dur * 0.7))
		y = np.sin(2 * np.pi * np.cumsum(fr) / rate)
		y += 0.3 * np.sin(4 * np.pi * np.cumsum(fr) / rate)
		y *= np.minimum(1, t / 0.01) * np.exp(-t / (dur * 0.5))
		return finish_oneshot(y, rate, dur, 0.03), None
	return f


def g_breath(rate):
	n = int(1.6 * rate)
	x = Lcg(31).noise(n)
	y = onepole_lp(x, 0.25) - onepole_lp(x, 0.03)
	t = np.arange(n) / rate
	y *= np.minimum(1, t / 0.08)
	ls, L = int(0.3 * rate), int(1.2 * rate)
	return crossfade_loop(y, ls, L, int(0.2 * rate)), (ls, ls + L)


def g_fret(rate):
	n = int(0.25 * rate)
	t = np.arange(n) / rate
	x = Lcg(41).noise(n)
	y = (onepole_lp(x, 0.5) - onepole_lp(x, 0.1)) * np.exp(-t / 0.05)
	y += 0.5 * np.sin(2 * np.pi * np.cumsum(1800 - 1200 * t / 0.25) / rate) * np.exp(-t / 0.04)
	return finish_oneshot(y, rate, 0.25), None


def g_bird(rate):
	n = int(0.9 * rate)
	t = np.arange(n) / rate
	y = np.zeros(n)
	for k, st in enumerate((0.0, 0.18, 0.36, 0.55)):
		m = (t >= st) & (t < st + 0.12)
		tt = t[m] - st
		fr = 3200 + 1600 * np.sin(np.pi * tt / 0.12) + 300 * k
		ph = 2 * np.pi * np.cumsum(fr) / rate
		y[m] += np.sin(ph) * np.sin(np.pi * tt / 0.12)
	return finish_oneshot(y, rate, 0.9), None


def g_phone(rate):
	n = int(1.0 * rate)
	t = np.arange(n) / rate
	y = (np.sin(2 * np.pi * 440 * t) + np.sin(2 * np.pi * 480 * t)) * 0.5
	y *= (np.sin(2 * np.pi * 20 * t) > -0.2)
	ls, L = int(0.0 * rate), n
	return y, (ls, ls + L)


def g_heli(rate):
	n = int(1.2 * rate)
	t = np.arange(n) / rate
	x = Lcg(53).noise(n)
	y = onepole_lp(x, 0.08) * (0.25 + 0.75 * np.maximum(0, np.sin(2 * np.pi * 12 * t)) ** 3)
	y += 0.3 * np.sin(2 * np.pi * 60 * t)
	ls, L = int(0.1 * rate), int(1.0 * rate)
	return crossfade_loop(y, ls, L, int(0.05 * rate)), (ls, ls + L)


def g_applause(rate):
	n = int(1.6 * rate)
	lc = Lcg(61)
	y = np.zeros(n)
	pos = lc.noise(260)
	for i in range(260):
		st = int((pos[i] * 0.5 + 0.5) * (n - int(0.03 * rate)))
		m = int(0.02 * rate)
		y[st:st + m] += lc.noise(m) * np.exp(-np.arange(m) / (0.004 * rate))
	y = y - onepole_lp(y, 0.05)
	ls, L = int(0.3 * rate), int(1.2 * rate)
	return crossfade_loop(y, ls, L, int(0.1 * rate)), (ls, ls + L)


def g_gunshot(rate):
	n = int(0.7 * rate)
	t = np.arange(n) / rate
	x = Lcg(71).noise(n)
	y = onepole_lp(x, 0.6) * np.exp(-t / 0.08) + 0.6 * np.sin(2 * np.pi * 70 * t) * np.exp(-t / 0.05)
	return finish_oneshot(y, rate, 0.7), None


# ---- the instrument set -----------------------------------------------------

V = 'vcsl'
S = 'vsco'
VZ = 'Chordophones/Zithers/'
VI = 'Idiophones/Struck Idiophones/'
VM = 'Membranophones/Struck Membranophones/'

INSTRUMENTS = [
	# ---- recorded, melodic
	Sampled('piano', [(V, VZ + 'Grand Piano, Kawai/Sustains', r'_v3_', 21, 108)], 'decay',
		spacing=7, rates=(16000, 22050, 67), ls=0.45, loop=0.12),
	Sampled('epiano', [(V, 'Electrophones/TX81Z/FM Piano', r'_vl2', 24, 100)], 'decay',
		spacing=12, rates=(16000, 22050, 67), ls=0.45, loop=0.12),
	Sampled('clav', [(V, 'Electrophones/TX81Z/Clavisynth', r'_vl2', 24, 100)], 'decay',
		spacing=12, rates=(16000, 22050, 67), ls=0.3, loop=0.1),
	Sampled('harpsi', [(V, VZ + 'Harpsichord, English/Sustains/Normal', r'', 24, 96)], 'decay',
		spacing=8, rates=(16000, 22050, 67), ls=0.35, loop=0.1),
	Sampled('glock', [(V, VI + 'Glockenspiel', r'_medium_', 60, 108)], 'decay',
		spacing=7, rates=(22050, 22050, 0), ls=0.3, loop=0.08, tune=False),
	Sampled('vibes', [(V, VI + 'Vibraphone/Hard Mallets', r'_v2_', 48, 96)], 'decay',
		spacing=7, ls=0.4, loop=0.1),
	Sampled('marimba', [(V, VI + 'Marimba', r'_med_', 36, 96)], 'decay',
		spacing=7, rates=(16000, 22050, 67), ls=0.2, loop=0.08),
	Sampled('xylo', [(V, VI + 'Xylophone/Medium Mallets', r'_pp_', 55, 108)], 'decay',
		spacing=7, ls=0.18, loop=0.06),
	Sampled('chimes', [(V, VI + 'Tubular Bells 1', r'_(ff|f|fff)_', 48, 84, 12)], 'decay',
		spacing=6, ls=0.6, loop=0.12, tune=False),
	Sampled('pipeorgan', [(V, 'Aerophones/Edge-blown Aerophones/Pipe Organ/Loud', r'', 24, 96)], 'sustain',
		spacing=12, rates=(16000, 22050, 67), ls=0.3, loop=0.25),
	Sampled('harmonica', [(V, 'Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal', r'', 36, 96)],
		'sustain', spacing=9, ls=0.25, loop=0.25),
	Sampled('guitar', [(V, 'Chordophones/Composite Chordophones/Strumstick/Finger', r'_vl2_', 38, 86)], 'decay',
		spacing=6, rates=(16000, 22050, 67), ls=0.4, loop=0.1),
	Sampled('harp', [(V, 'Chordophones/Composite Chordophones/Concert Harp', r'_mf', 24, 103)], 'decay',
		spacing=7, rates=(16000, 22050, 67), ls=0.4, loop=0.1),
	Sampled('sax', [(V, 'Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato', r'_vl2_', 40, 84)], 'sustain',
		spacing=7, rates=(16000, 22050, 67), ls=0.25, loop=0.25),
	Sampled('sopsax', [(V, 'Aerophones/Reed Aerophones/Saxello/Non-Vibrato', r'_vl2_', 50, 90)], 'sustain',
		spacing=7, ls=0.25, loop=0.25),
	Sampled('recorder', [(V, 'Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain', r'', 60, 100)],
		'sustain', spacing=7, ls=0.25, loop=0.25),
	Sampled('ocarina', [(V, 'Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus', r'', 60, 90)],
		'sustain', spacing=6, ls=0.25, loop=0.25),
	Sampled('koto', [(V, VZ + 'Dan Tranh/Normal', r'_f_', 40, 80)], 'decay',
		spacing=6, rates=(16000, 22050, 67), ls=0.35, loop=0.1),
	Sampled('kalimba', [(V, 'Idiophones/Plucked Idiophones/Kalimba, Kenya', r'', 48, 84)], 'decay',
		spacing=6, ls=0.25, loop=0.08),
	DetectedHits('timpani', V, ['Timpani1A_hit_v3_rr1_main.wav', 'Timpani4A_hit_v3_rr1_main.wav'],
		max_s=1.6, rate=11025),
	# ---- recorded, orchestral (VSCO 2 CE)
	Sampled('strings', [(S, 'Strings/Cello Section/susvib', r'_v1_1', 36, 54),
	                    (S, 'Strings/Violin Section/susVib', r'_v1', 55, 96)], 'sustain',
		spacing=6, rates=(16000, 22050, 67), ls=0.35, loop=0.4),
	Sampled('pizz', [(S, 'Strings/Solo Contrabass/Pizz', r'_v1_rr1', 28, 54),
	                 (S, 'Strings/Violin Section/Pizz', r'_v1_rr1', 55, 96)], 'decay',
		spacing=6, rates=(11025, 22050, 55), ls=0.2, loop=0.06),
	Sampled('cbpizz', [(S, 'Strings/Solo Contrabass/Pizz', r'_v1_rr1', 24, 60)], 'decay',
		spacing=6, rates=(11025, 11025, 0), ls=0.35, loop=0.12),
	Sampled('trumpet', [(S, 'Brass/Trumpet/sus', r'_v3_', 52, 88)], 'sustain',
		spacing=6, ls=0.25, loop=0.25),
	Sampled('trombone', [(S, 'Brass/Tenor Trombone/sus', r'_v3_', 34, 70)], 'sustain',
		spacing=6, rates=(16000, 16000, 0), ls=0.25, loop=0.25),
	Sampled('tuba', [(S, 'Brass/Tuba/sus', r'_v2_', 24, 58)], 'sustain',
		spacing=7, rates=(11025, 11025, 0), ls=0.3, loop=0.25),
	Sampled('horn', [(S, 'Brass/F Horn/sus', r'_v2_', 34, 77)], 'sustain',
		spacing=6, rates=(16000, 16000, 0), ls=0.3, loop=0.25),
	Sampled('brass', [(S, 'Brass/Tenor Trombone/sus', r'_v3_', 34, 56),
	                  (S, 'Brass/Trumpet/sus', r'_v3_', 57, 88)], 'sustain',
		spacing=6, rates=(16000, 22050, 72), ls=0.25, loop=0.25),
	Sampled('flute', [(S, 'Woodwinds/Flute/susNV', r'_v1_', 59, 96)], 'sustain',
		spacing=7, ls=0.25, loop=0.25),
	Sampled('oboe', [(S, 'Woodwinds/Oboe/Sus', r'_v3_', 58, 92)], 'sustain',
		spacing=6, ls=0.25, loop=0.25),
	Sampled('bassoon', [(S, 'Woodwinds/Bassoon/sus', r'_v2_', 34, 75)], 'sustain',
		spacing=7, rates=(11025, 16000, 48), ls=0.25, loop=0.25),
	Sampled('clarinet', [(S, 'Woodwinds/Clarinet/susLong', r'_v2_', 50, 91)], 'sustain',
		spacing=7, ls=0.25, loop=0.25),
	LoopedNoise('ocean', V, 'OceanDrum_Sus_1_Mid.wav', ls=0.6, loop=1.5, rate=16000),
	# ---- generated, pitched
	Generated('organ', p_organ, attack=0.004, loop_t=0.05),
	Generated('eguitar', p_pluck(24, 1.1, 1.6, 0.18), roots=(45, 57, 69), loop_t=0.45, tail=True, decay_s=3.5),
	Generated('odguitar', p_pluck(16, 0.9, 3.0, 0.08), roots=(45, 57, 69), loop_t=0.35, drive=4.0, tail=True, decay_s=8.0),
	Generated('distguitar', p_pluck(16, 0.8, 4.0, 0.05), roots=(45, 57, 69), loop_t=0.3, drive=12.0, tail=True, decay_s=12.0),
	Generated('ebass', p_pluck(14, 1.3, 1.4, 0.35), roots=(33, 45, 57), rate=11025, loop_t=0.35, tail=True, decay_s=4.0),
	Generated('synbass', p_bright_saw(30, 1.6, 0.12), roots=(33, 45, 57), rate=11025, loop_t=0.4),
	Generated('sqlead', p_square(25), roots=(45, 57, 69, 81), loop_t=0.05),
	Generated('sawlead', p_saw(30, 1.0), roots=(45, 57, 69, 81), loop_t=0.05),
	Generated('synbrass', p_bright_saw(30, 1.4, 0.25), roots=(45, 57, 69), loop_t=0.6, attack=0.03),
	Generated('pad', p_saw(24, 1.7), roots=(45, 57, 69, 81), rate=16000, loop_t=0.1, loop_len=1.0, detune=(0, 1, -1), attack=0.08, vol=1.6),
	Generated('choir', p_choir, roots=(45, 57, 69, 81), rate=16000, loop_t=0.1, loop_len=1.0, detune=(0, 1, -1), attack=0.08, vol=1.6),
	Generated('steeldrum', p_steel, roots=(57, 69, 81), loop_t=0.4, tail=True, decay_s=2.5),
	Generated('fxbell', p_bell, roots=(57, 69, 81), loop_t=0.5, tail=True, decay_s=6.0),
	GenNoise('breath', g_breath, rate=16000, desc='band-passed LCG noise, looped'),
	GenNoise('fret', g_fret, desc='noise burst and falling squeak'),
	GenNoise('bird', g_bird, desc='four sine chirps'),
	GenNoise('phone', g_phone, desc='440+480 Hz ring at 20 Hz, looped'),
	GenNoise('heli', g_heli, rate=11025, desc='noise chopped at 12 Hz, looped'),
	GenNoise('applause', g_applause, rate=16000, desc='260 LCG-placed noise claps, looped'),
	GenNoise('gunshot', g_gunshot, rate=16000, desc='decaying noise and 70 Hz thump'),
	# ---- drums (recorded)
	OneShot('kick_c', V, 'BDrumNew_hit_v5_rr1_Sum.wav', 0.8, 16000),
	OneShot('kick_m', S, 'bdrum_muted_ff_1.wav', 0.6, 16000),
	OneShot('snare', V, 'Snare2_HitSN_v6_rr1_Mid.wav', 0.6),
	OneShot('snare2', V, 'Snare4_HitSN_v4_rr1_Mid.wav', 0.5),
	OneShot('sidestick', V, 'RopeSnare_sidestick_Main_vl2_rr1.wav', 0.3),
	OneShot('clap', V, 'Clap_rr1.wav', 0.4),
	OneShot('tom_lo', V, 'TomL_HitS_v3_rr1_Mid.wav', 0.8, 16000),
	OneShot('tom_hi', V, 'TomH_HitS_v3_rr1_Mid.wav', 0.7, 16000),
	OneShot('hh_closed', V, 'HiHat_HitC_v3_rr1_Mid.wav', 0.3),
	OneShot('hh_pedal', V, 'HiHat_Close_rr1_Mid.wav', 0.3),
	OneShot('hh_open', V, 'HiHat_HitO_rr1_Mid.wav', 0.9),
	OneShot('crash', V, 'susCymb1_hit_stick_f1.wav', 1.5),
	OneShot('crash2', V, 'cymbal_crash1_mf1.wav', 1.5),
	OneShot('ride', V, 'susCymb2_hit_stick_mp1.wav', 1.3),
	OneShot('ridebell', V, 'susCymb1_hit_bell_mf1.wav', 1.2),
	OneShot('tamb', V, 'Tamb1_Hit_v2_rr1_Mid.wav', 0.6),
	OneShot('cowbell', V, 'Cowbell1_Normal_v3_rr1_Mid.wav', 0.5),
	OneShot('vibraslap', V, 'vibraslap_rr1.wav', 1.0, 16000),
	OneShot('bongo_hi', V, 'BongoH_Hit1_v2_rr1_Mid.wav', 0.4),
	OneShot('bongo_lo', V, 'BongoL_Hit1_v2_rr1_Mid.wav', 0.45),
	OneShot('conga_mute', V, 'Quinto_HitFM1_v2_rr1_Sum.wav', 0.3),
	OneShot('conga_hi', V, 'Conga_HitN_v2_rr1_Sum.wav', 0.6),
	OneShot('conga_lo', V, 'Tumba_HitN_v2_rr1_Sum.wav', 0.7, 16000),
	OneShot('timbale', V, 'RopeSnare_low_ns_Main_vl2_rr1.wav', 0.5),
	OneShot('agogo_hi', V, 'Agogo_High_v2_rr1_Mid.wav', 0.6),
	OneShot('agogo_lo', V, 'Agogo_Low_v2_rr1_Mid.wav', 0.6),
	OneShot('cabasa', V, 'Cabasa1_Hit_rr1_Mid.wav', 0.3),
	OneShot('maracas', V, 'Mid_Shaker_Slap_rr1.wav', 0.3),
	OneShot('guiro_s', V, 'Guiro_Hit_rr1_Mid.wav', 0.3),
	OneShot('guiro_l', V, 'Guiro_Med_rr1_Mid.wav', 0.8),
	OneShot('claves', V, 'Claves1_Hit_v2_rr1_Mid.wav', 0.3),
	OneShot('woodblock', V, 'wood_click_f_rr1.wav', 0.3),
	OneShot('tri_mute', V, 'Triangle1_HitM_v1_rr2_Mid.wav', 0.4),
	OneShot('tri_open', V, 'Triangle1_Hit_v2_rr1_Mid.wav', 1.3),
	OneShot('shaker', V, 'Mid_ShakerDouble_Down_rr1.wav', 0.4),
	OneShot('sleigh', V, 'Sleighbells_Hit_rr1_Mid.wav', 0.8),
	OneShot('marktree', V, 'windchimes_asc1.wav', 1.5),
	OneShot('revcym', V, 'cymbal_crash1_ff2.wav', 1.3, 22050, reverse=True),
	# ---- drums (generated)
	GenNoise('kick_g', g_kick, rate=16000, desc='sine sweep 155->45 Hz with click'),
	GenNoise('whistle_s', g_whistle(2400, 0.12), desc='short whistle'),
	GenNoise('whistle_l', g_whistle(2300, 0.45), desc='long whistle'),
	GenNoise('cuica_m', g_cuica(700, 600, 0.15), desc='muted cuica sweep'),
	GenNoise('cuica_o', g_cuica(450, 750, 0.35), desc='open cuica sweep'),
]

# ---- the GM program map -----------------------------------------------------

GM_NAMES = [
	'Acoustic Grand', 'Bright Acoustic', 'Electric Grand', 'Honky-tonk', 'Electric Piano 1',
	'Electric Piano 2', 'Harpsichord', 'Clavinet', 'Celesta', 'Glockenspiel', 'Music Box',
	'Vibraphone', 'Marimba', 'Xylophone', 'Tubular Bells', 'Dulcimer', 'Drawbar Organ',
	'Percussive Organ', 'Rock Organ', 'Church Organ', 'Reed Organ', 'Accordion', 'Harmonica',
	'Tango Accordion', 'Nylon Guitar', 'Steel Guitar', 'Jazz Guitar', 'Clean Guitar',
	'Muted Guitar', 'Overdriven Guitar', 'Distortion Guitar', 'Guitar Harmonics',
	'Acoustic Bass', 'Fingered Bass', 'Picked Bass', 'Fretless Bass', 'Slap Bass 1',
	'Slap Bass 2', 'Synth Bass 1', 'Synth Bass 2', 'Violin', 'Viola', 'Cello', 'Contrabass',
	'Tremolo Strings', 'Pizzicato Strings', 'Orchestral Harp', 'Timpani', 'String Ensemble 1',
	'String Ensemble 2', 'Synth Strings 1', 'Synth Strings 2', 'Choir Aahs', 'Voice Oohs',
	'Synth Voice', 'Orchestra Hit', 'Trumpet', 'Trombone', 'Tuba', 'Muted Trumpet',
	'French Horn', 'Brass Section', 'Synth Brass 1', 'Synth Brass 2', 'Soprano Sax',
	'Alto Sax', 'Tenor Sax', 'Baritone Sax', 'Oboe', 'English Horn', 'Bassoon', 'Clarinet',
	'Piccolo', 'Flute', 'Recorder', 'Pan Flute', 'Blown Bottle', 'Shakuhachi', 'Whistle',
	'Ocarina', 'Square Lead', 'Saw Lead', 'Calliope Lead', 'Chiff Lead', 'Charang Lead',
	'Voice Lead', 'Fifths Lead', 'Bass + Lead', 'New Age Pad', 'Warm Pad', 'Polysynth Pad',
	'Choir Pad', 'Bowed Pad', 'Metallic Pad', 'Halo Pad', 'Sweep Pad', 'Rain', 'Soundtrack',
	'Crystal', 'Atmosphere', 'Brightness', 'Goblins', 'Echoes', 'Sci-fi', 'Sitar', 'Banjo',
	'Shamisen', 'Koto', 'Kalimba', 'Bagpipe', 'Fiddle', 'Shanai', 'Tinkle Bell', 'Agogo',
	'Steel Drums', 'Woodblock', 'Taiko Drum', 'Melodic Tom', 'Synth Drum', 'Reverse Cymbal',
	'Guitar Fret Noise', 'Breath Noise', 'Seashore', 'Bird Tweet', 'Telephone Ring',
	'Helicopter', 'Applause', 'Gunshot',
]
assert len(GM_NAMES) == 128
# (instrument, transpose, volume 0..127, attack ms, decay ms, sustain dB, release ms)
# decay 0 on a 'decay' instrument = the zone's measured natural decay.

def P(inst, vol=100, att=0, dec=0, sus=0.0, rel=250, tr=0):
	return (inst, tr, vol, att, dec, sus, rel)

PROGRAMS = [
	P('piano', rel=300), P('piano', 108, rel=300), P('piano', rel=300), P('piano', 96, rel=300),
	P('epiano', rel=400), P('epiano', 92, rel=400), P('harpsi', rel=200), P('clav', rel=150),
	P('glock', 90, rel=600), P('glock', rel=600), P('glock', 85, rel=500, tr=12), P('vibes', rel=700),
	P('marimba', rel=300), P('xylo', rel=300), P('chimes', rel=1500), P('koto', rel=500),
	P('organ', rel=60), P('organ', 96, dec=600, sus=-8.0, rel=60), P('organ', 110, rel=60),
	P('pipeorgan', rel=350), P('harmonica', 90, att=20, rel=150),
	P('harmonica', att=20, rel=150), P('harmonica', att=10, rel=120), P('harmonica', 96, att=20, rel=150),
	P('guitar', rel=400), P('guitar', 104, rel=400), P('eguitar', 96, rel=300), P('eguitar', rel=300),
	P('eguitar', dec=500, rel=80), P('odguitar', 92, rel=200), P('distguitar', 88, rel=200),
	P('fxbell', 80, rel=600, tr=12),
	P('cbpizz', 110, rel=300), P('ebass', rel=150), P('ebass', 104, rel=120), P('ebass', att=20, rel=300),
	P('ebass', 110, dec=800, rel=100), P('ebass', 110, dec=600, rel=100), P('synbass', rel=100),
	P('synbass', 104, dec=900, sus=-6.0, rel=100),
	P('strings', att=60, rel=300), P('strings', att=60, rel=300), P('strings', att=60, rel=300),
	P('strings', att=60, rel=300), P('strings', 92, att=30, rel=300), P('pizz', rel=200),
	P('harp', rel=800), P('timpani', 110, rel=800),
	P('strings', att=80, rel=400), P('strings', 92, att=350, rel=600), P('pad', 88, att=150, rel=500),
	P('pad', 84, att=300, rel=700), P('choir', att=150, rel=500), P('choir', 92, att=120, rel=400),
	P('choir', 88, att=60, rel=400), P('brass', 110, dec=300, sus=-96.0, rel=200),
	P('trumpet', att=10, rel=200), P('trombone', att=15, rel=200), P('tuba', att=20, rel=200),
	P('trumpet', 80, att=10, rel=200), P('horn', att=30, rel=300), P('brass', att=20, rel=250),
	P('synbrass', rel=200), P('synbrass', 96, att=40, rel=300),
	P('sopsax', att=15, rel=200), P('sax', att=15, rel=200, tr=0), P('sax', att=15, rel=200),
	P('sax', 104, att=15, rel=200, tr=0),
	P('oboe', att=20, rel=200), P('oboe', 96, att=25, rel=250), P('bassoon', att=20, rel=200),
	P('clarinet', att=20, rel=200),
	P('flute', att=20, rel=200, tr=0), P('flute', att=25, rel=250), P('recorder', att=15, rel=150),
	P('recorder', 92, att=40, rel=250), P('ocarina', 90, att=40, rel=250), P('recorder', 90, att=50, rel=300),
	P('ocarina', 88, att=20, rel=200), P('ocarina', att=20, rel=200),
	P('sqlead', 80, rel=120), P('sawlead', 80, rel=120), P('sqlead', 76, att=30, rel=200),
	P('flute', att=5, rel=120), P('odguitar', 84, rel=200), P('choir', 90, att=40, rel=300),
	P('sawlead', 76, rel=150), P('sawlead', 80, rel=150),
	P('pad', 90, att=200, rel=900), P('pad', 92, att=400, rel=900), P('sawlead', 70, dec=800, sus=-10.0, rel=400),
	P('choir', 88, att=400, rel=900), P('pad', 86, att=500, rel=1200), P('pad', 80, att=300, rel=900),
	P('choir', 84, att=500, rel=1200), P('pad', 84, att=800, rel=1200),
	P('fxbell', 84, rel=900), P('pad', 82, att=400, rel=1200), P('fxbell', 88, rel=1200),
	P('pad', 84, att=300, rel=900), P('pad', 84, att=200, rel=900), P('choir', 80, att=600, rel=1200),
	P('fxbell', 80, att=200, rel=1500), P('fxbell', 80, att=50, rel=1200),
	P('koto', 96, rel=600), P('guitar', 96, dec=900, rel=200), P('koto', 92, dec=700, rel=200),
	P('koto', rel=600), P('kalimba', rel=400), P('oboe', 90, att=30, rel=150), P('strings', 92, att=40, rel=250),
	P('oboe', 96, att=20, rel=150),
	P('glock', 90, rel=500, tr=12), P('agogo_hi', 100, rel=300), P('steeldrum', rel=500), P('woodblock', rel=200),
	P('kick_c', 120, rel=500), P('tom_lo', rel=300), P('kick_g', 110, rel=200), P('revcym', rel=300),
	P('fret', rel=100), P('breath', 90, att=80, rel=300), P('ocean', att=500, rel=1000), P('bird', rel=200),
	P('phone', 80, rel=50), P('heli', att=300, rel=800), P('applause', att=300, rel=1000), P('gunshot', 120, rel=200),
]
assert len(PROGRAMS) == 128, len(PROGRAMS)

# ---- the GM percussion map, channel 10 --------------------------------------
# note: (instrument, pitch to play it at -- 60 = as recorded, volume, exclusive group, decay ms)

DRUMS = {
	27: ('claves', 72, 80, 0, 0), 28: ('clap', 64, 80, 0, 0), 31: ('claves', 60, 90, 0, 0),
	33: ('woodblock', 66, 80, 0, 0), 34: ('cowbell', 72, 80, 0, 0),
	35: ('kick_m', 60, 120, 0, 0), 36: ('kick_g', 60, 120, 0, 0), 37: ('sidestick', 60, 100, 0, 0),
	38: ('snare', 60, 110, 0, 0), 39: ('clap', 60, 100, 0, 0), 40: ('snare2', 60, 110, 0, 0),
	41: ('tom_lo', 53, 110, 0, 0), 42: ('hh_closed', 60, 90, 1, 0), 43: ('tom_lo', 57, 110, 0, 0),
	44: ('hh_pedal', 60, 85, 1, 0), 45: ('tom_lo', 61, 110, 0, 0), 46: ('hh_open', 60, 90, 1, 0),
	47: ('tom_hi', 57, 105, 0, 0), 48: ('tom_hi', 60, 105, 0, 0), 49: ('crash', 60, 100, 0, 0),
	50: ('tom_hi', 63, 105, 0, 0), 51: ('ride', 60, 85, 0, 0), 52: ('crash2', 55, 95, 0, 0),
	53: ('ridebell', 60, 90, 0, 0), 54: ('tamb', 60, 90, 0, 0), 55: ('crash', 67, 85, 0, 700),
	56: ('cowbell', 60, 90, 0, 0), 57: ('crash2', 60, 100, 0, 0), 58: ('vibraslap', 60, 90, 0, 0),
	59: ('ride', 63, 85, 0, 0), 60: ('bongo_hi', 60, 100, 0, 0), 61: ('bongo_lo', 60, 100, 0, 0),
	62: ('conga_mute', 60, 100, 0, 0), 63: ('conga_hi', 60, 100, 0, 0), 64: ('conga_lo', 60, 100, 0, 0),
	65: ('timbale', 64, 100, 0, 0), 66: ('timbale', 58, 100, 0, 0), 67: ('agogo_hi', 60, 90, 0, 0),
	68: ('agogo_lo', 60, 90, 0, 0), 69: ('cabasa', 60, 90, 0, 0), 70: ('maracas', 60, 90, 0, 0),
	71: ('whistle_s', 60, 80, 2, 0), 72: ('whistle_l', 60, 80, 2, 0), 73: ('guiro_s', 60, 90, 3, 0),
	74: ('guiro_l', 60, 90, 3, 0), 75: ('claves', 60, 100, 0, 0), 76: ('woodblock', 64, 100, 0, 0),
	77: ('woodblock', 58, 100, 0, 0), 78: ('cuica_m', 60, 80, 4, 0), 79: ('cuica_o', 60, 80, 4, 0),
	80: ('tri_mute', 60, 85, 5, 0), 81: ('tri_open', 60, 85, 5, 0), 82: ('shaker', 60, 90, 0, 0),
	83: ('sleigh', 60, 85, 0, 0), 84: ('marktree', 60, 80, 0, 0), 85: ('claves', 70, 85, 0, 0),
	86: ('kick_c', 55, 110, 0, 0), 87: ('kick_c', 50, 110, 0, 0),
}


# ---- assembly ---------------------------------------------------------------

def att_from_db(db):
	"""dB (<= 0) -> attenuation units, 1/256 octave of amplitude."""
	if db <= -96.0:
		return ATT_SILENT
	return int(round(-db / (20 * math.log10(2)) * 256))


def build(args):
	src = Source(args.cache)
	insts = []
	for spec in INSTRUMENTS:
		log('instrument', spec.name)
		zones = spec.build(src)
		insts.append((spec, zones))
	names = [s.name for s, _ in insts]
	for p in PROGRAMS:
		assert p[0] in names, p[0]
	for d in DRUMS.values():
		assert d[0] in names, d[0]

	# key ranges, gain, 8-bit conversion
	REF = 0.18
	data = bytearray()
	shared = {}
	zone_rows = []
	inst_rows = []
	report = []
	for spec, zones in insts:
		zones.sort(key=lambda z: z.root if z.root is not None else 60)
		for i, z in enumerate(zones):
			z.lo = 0 if i == 0 else (zones[i - 1].root + z.root) // 2 + 1
			z.hi = 127 if i == len(zones) - 1 else (z.root + zones[i + 1].root) // 2
		first = len(zone_rows)
		for z in zones:
			x = z.data
			pk = float(np.max(np.abs(x))) + 1e-12
			x = x / pk
			r = zone_rms(Zone(x, z.rate, z.root, z.loop, z.tail))
			g = REF / max(r, 1e-4) * spec.vol
			gain = int(max(8, min(255, round(128 * g))))
			q = np.clip(np.round(x * 127.0), -128, 127).astype(np.int8)
			n = len(q)
			assert n < 65472, (spec.name, n)		# gm_synth's 16.16 positions
			if z.loop:
				ls, le = z.loop
				assert 0 <= ls < le <= n, (spec.name, ls, le, n)
				q = q[:le]
				n = le
				guard = q[ls]
			else:
				ls = le = 0
				guard = 0
			blob = q.tobytes() + struct.pack('b', int(guard))
			off = shared.get(blob)
			stored = off is None
			if stored:		# identical sample data is stored once (brass = trombone + trumpet)
				off = len(data)
				shared[blob] = off
				data += blob
			flags = (ZF_LOOP if z.loop else 0) | (ZF_TAIL if z.tail else 0)
			root = 60 if z.root is None else z.root
			dec = int(min(65535, round(z.decay_s * 1000))) if z.tail else 0
			zone_rows.append(struct.pack('>BBBbHBBHHIIII', z.lo, z.hi, root, z.fine, z.rate,
				gain, flags, dec, 0, off, n, ls, le))
			report.append((spec.name, z, n, gain, stored))
		inst_rows.append(struct.pack('>16sHH', spec.name.encode()[:16], first, len(zones)))

	idx = {n: i for i, n in enumerate(names)}
	prog_rows = []
	for inst, tr, vol, att, dec, sus, rel in PROGRAMS:
		prog_rows.append(struct.pack('>BbBBHHHH', idx[inst], tr, vol, 0, att, dec,
			att_from_db(sus), rel))
	drum_rows = []
	for k in range(128):
		if k in DRUMS:
			inst, key, vol, ex, dec = DRUMS[k]
			drum_rows.append(struct.pack('>BBBBHH', idx[inst], key, vol, ex, dec, 0))
		else:
			drum_rows.append(struct.pack('>BBBBHH', 0xFF, 60, 0, 0, 0, 0))

	prog_off = HDR_SIZE
	drum_off = prog_off + 128 * PROG_SIZE
	inst_off = drum_off + 128 * DRUM_SIZE
	zone_off = inst_off + len(inst_rows) * INST_SIZE
	data_off = zone_off + len(zone_rows) * ZONE_SIZE
	data_off = (data_off + 3) & ~3
	total = data_off + len(data)
	hdr = struct.pack('>4sHHHHIIIIIII8x', b'GMBK', BANK_VERSION, HDR_SIZE, len(inst_rows),
		len(zone_rows), prog_off, drum_off, inst_off, zone_off, data_off, len(data), total)
	assert len(hdr) == HDR_SIZE
	blob = bytearray(hdr) + b''.join(prog_rows) + b''.join(drum_rows) + b''.join(inst_rows) \
		+ b''.join(zone_rows)
	blob += b'\0' * (data_off - len(blob))
	blob += data
	assert len(blob) == total

	tmp = args.out + '.tmp'
	with open(tmp, 'wb') as f:
		f.write(blob)
	os.replace(tmp, args.out)

	log('')
	log('%-11s %4s %4s %4s %4s %6s %5s %5s %s' % ('inst', 'root', 'cent', 'lo', 'hi', 'rate', 'bytes', 'gain', 'flags'))
	per_inst = {}
	for name, z, n, gain, stored in report:
		per_inst[name] = per_inst.get(name, 0) + (n + 1 if stored else 0)
		log('%-11s %4s %+4d %4d %4d %6d %5d %5d %s%s %s' % (name, z.root, z.fine, z.lo, z.hi, z.rate, n + 1, gain,
			'L' if z.loop else '-', 'T' if z.tail else '-',
			('decay %.1fs' % z.decay_s) if z.tail else ''))
	log('')
	log('instruments %d, zones %d, sample data %d bytes, bank %d bytes (%.2f MB)' % (
		len(inst_rows), len(zone_rows), len(data), total, total / 1048576.0))
	write_sources(args, src, insts, per_inst, total)


def write_sources(args, src, insts, per_inst, total):
	lic = {}
	for repo in REPOS:
		owner, name, sha = REPOS[repo]
		lp = src.fetch(repo, 'LICENSE')
		with open(lp, 'r', encoding='utf-8', errors='replace') as f:
			lic[repo] = f.read().replace('\r\n', '\n').strip()
	out = []
	w = out.append
	w('# GM sample bank: provenance and licences')
	w('')
	w('<!-- WRITTEN BY tools/music/build_gmbank.py -- edit the script, not this file. -->')
	w('')
	w('`tools/music/ffe_gm.bnk` (%d bytes) is built by `tools/music/build_gmbank.py` from two' % total)
	w('repositories read at pinned commits, plus sounds the script generates itself.')
	w('')
	w('**Every recorded sample in the bank is CC0 1.0 Universal (public domain dedication).**')
	w('No GeneralUser GS, TimGM6mb, FluidR3, MuseScore General or any other non-CC0 bank was used.')
	w('')
	w('| repository | commit | licence |')
	w('|---|---|---|')
	for repo, (owner, name, sha) in REPOS.items():
		w('| https://github.com/%s/%s | `%s` | CC0 1.0 Universal (`LICENSE` at that commit, reproduced below) |'
			% (owner, name, sha))
	w('')
	w('Notes on the licence check:')
	w('')
	w('- VCSL: `LICENSE` is the CC0 1.0 Universal legal code; `README.md` says "This collection is under')
	w('  a Creative Commons 0 license. Essentially it\'s Public Domain- you can do whatever you want with')
	w('  these sounds (even make commercial software), no royalties, no credit, no special terms."')
	w('- VSCO-2-CE: `LICENSE` is the CC0 1.0 Universal legal code. Its older `Readme.txt` (2016) adds')
	w('  two non-binding *requests* -- not to sell the samples directly, and to credit Versilian')
	w('  Studios / Sam Gossner and Ivy Audio / Simon Dalzell. CC0 makes neither a condition; we honour')
	w('  both anyway: the bank is not sold, and the credit is here and in the release notes.')
	w('- VSCO-2-CE samples are used only for what VCSL lacks: bowed and plucked strings, brass,')
	w('  flute, oboe, bassoon, clarinet, and one muted concert bass drum.')
	w('')
	w('Credit: samples by Versilian Studios LLC (Sam Gossner) and contributors -- VCSL,')
	w('https://github.com/sgossner/VCSL -- and VS Chamber Orchestra: Community Edition, recorded by Sam')
	w('Gossner and Simon Dalzell (Ivy Audio), sample cutting by Elan Hickler (Soundemote).')
	w('')
	w('## Generated sounds (ours, MIT, like the script)')
	w('')
	w('These are computed by `build_gmbank.py` (integer-frequency additive synthesis, a seeded LCG')
	w('noise source, simple one-pole filters); no recording is involved:')
	w('')
	for spec, zones in insts:
		if isinstance(spec, (Generated, GenNoise)):
			w('- `%s`: %s' % (spec.name, '; '.join(sorted(set(z.label[len('generated: '):] for z in zones)))))
	w('')
	w('One derived sound: `revcym` is the VCSL clash cymbal `cymbal_crash1_ff2.wav` reversed (still CC0).')
	w('')
	w('## Exact input files')
	w('')
	w('Every file below was downloaded from `https://raw.githubusercontent.com/<owner>/<repo>/<commit>/<path>`.')
	w('')
	for spec, zones in insts:
		files = [z.label for z in zones if not z.label.startswith('generated')]
		if not files:
			continue
		w('- **%s** (%d bytes in the bank)' % (spec.name, per_inst.get(spec.name, 0)))
		for f in files:
			w('  - `%s`' % f)
	w('')
	w('Read but not stored (octave-convention and tuning probes, and files rejected as more than')
	w('0.6 semitone off their name):')
	w('')
	stored = set()
	for spec, zones in insts:
		for z in zones:
			stored.add(z.label.replace(' (reversed)', ''))
	for repo, path in src.used:
		if '%s:%s' % (repo, path) not in stored and path != 'LICENSE':
			w('- `%s:%s`' % (repo, path))
	w('')
	w('## GM program map')
	w('')
	w('| prog | GM name | instrument | prog | GM name | instrument |')
	w('|---|---|---|---|---|---|')
	for i in range(64):
		a, b = PROGRAMS[i], PROGRAMS[i + 64]
		w('| %d | %s | `%s` | %d | %s | `%s` |' % (i, GM_NAMES[i], a[0], i + 64, GM_NAMES[i + 64], b[0]))
	w('')
	w('## Channel 10 drum map')
	w('')
	w('| key | instrument | played at | group |')
	w('|---|---|---|---|')
	for k in sorted(DRUMS):
		inst, key, vol, ex, dec = DRUMS[k]
		w('| %d | `%s` | %d | %s |' % (k, inst, key, ex or ''))
	w('')
	for repo in REPOS:
		owner, name, sha = REPOS[repo]
		w('## Licence text: %s/%s at `%s`' % (owner, name, sha[:12]))
		w('')
		w('```')
		w(lic[repo])
		w('```')
		w('')
	md = os.path.join(os.path.dirname(os.path.abspath(args.out)), 'GMBANK-SOURCES.md')
	with open(md + '.tmp', 'w', encoding='utf-8', newline='\n') as f:
		f.write('\n'.join(out))
	os.replace(md + '.tmp', md)
	log('wrote', md)


def main():
	ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
	ap.add_argument('--cache', default=r'E:\ffe\gmsrc\cache')
	ap.add_argument('--out', default=os.path.join(HERE, 'ffe_gm.bnk'))
	args = ap.parse_args()
	build(args)


if __name__ == '__main__':
	main()
