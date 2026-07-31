'use client';

import { useMemo, useRef, useState } from 'react';
import { Canvas, useFrame } from '@react-three/fiber';
import * as THREE from 'three';
import { useCalculatorStore } from '../store/useCalculatorStore';

/**
 * The P&L surface: price × date × profit, as a solid.
 *
 * Same data as the matrix panel — `StrategyResponse.matrix`, every leg
 * re-priced at its own remaining maturity for each cell — read as a height
 * field instead of a table. The grid answers "what is it worth there"; the
 * surface answers "what shape is this position", which is the question a
 * calendar spread's ridge or a condor's plateau actually has a picture for.
 *
 * three, @react-three/fiber and drei were dependencies of this project without
 * a single component importing them: shipped weight with nothing behind it.
 *
 * Colour is the same encoding as the matrix — green above water, red below,
 * intensity by magnitude — so moving between the two views does not mean
 * relearning what a colour means. Height is normalised per position rather than
 * absolute, because the shape is the information; the numbers are next door.
 */

function Surface({ rows, cols, heights, maxAbs }: {
  rows: number; cols: number; heights: Float32Array; maxAbs: number;
}) {
  const mesh = useRef<THREE.Mesh>(null);

  const geometry = useMemo(() => {
    // A plane subdivided to the grid, then displaced. PlaneGeometry's vertex
    // order runs row-major from the top-left, which matches the way the cells
    // are laid out below.
    const g = new THREE.PlaneGeometry(4, 3, cols - 1, rows - 1);
    const pos = g.attributes.position as THREE.BufferAttribute;
    const colour = new Float32Array(pos.count * 3);
    const profit = new THREE.Color('#13aa52');
    const loss = new THREE.Color('#c0392b');
    const flat = new THREE.Color('#f2f2f2');

    for (let i = 0; i < pos.count; i++) {
      const h = heights[i] ?? 0;
      pos.setZ(i, (h / maxAbs) * 0.9);
      const t = Math.min(Math.abs(h) / maxAbs, 1);
      const c = flat.clone().lerp(h >= 0 ? profit : loss, 0.15 + t * 0.85);
      colour[i * 3] = c.r;
      colour[i * 3 + 1] = c.g;
      colour[i * 3 + 2] = c.b;
    }
    g.setAttribute('color', new THREE.BufferAttribute(colour, 3));
    g.computeVertexNormals();
    return g;
  }, [rows, cols, heights, maxAbs]);

  // A slow drift, not a spin. Enough parallax to read the relief; not so much
  // that reading a value becomes a moving target.
  useFrame(({ clock }) => {
    if (mesh.current) {
      mesh.current.rotation.z = -0.9 + Math.sin(clock.elapsedTime * 0.12) * 0.13;
    }
  });

  return (
    <mesh ref={mesh} geometry={geometry} rotation={[-1.05, 0, -0.9]}>
      <meshStandardMaterial vertexColors flatShading roughness={0.85} metalness={0.05} side={THREE.DoubleSide} />
    </mesh>
  );
}

export function PnLSurface() {
  const { result, isLoading, error } = useCalculatorStore();
  const [enabled, setEnabled] = useState(false);

  const field = useMemo(() => {
    const cells = result?.matrix ?? [];
    if (cells.length === 0) return null;

    const dates = [...new Set(cells.map((c) => c.date))].sort();
    const prices = [...new Set(cells.map((c) => c.price))].sort((a, b) => b - a);
    if (dates.length < 2 || prices.length < 2) return null;

    const byKey = new Map(cells.map((c) => [`${c.price}|${c.date}`, c.pnl]));
    const heights = new Float32Array(prices.length * dates.length);
    let maxAbs = 0;
    for (let r = 0; r < prices.length; r++) {
      for (let c = 0; c < dates.length; c++) {
        const v = byKey.get(`${prices[r]}|${dates[c]}`) ?? 0;
        heights[r * dates.length + c] = v;
        maxAbs = Math.max(maxAbs, Math.abs(v));
      }
    }
    return { rows: prices.length, cols: dates.length, heights, maxAbs: maxAbs || 1 };
  }, [result]);

  return (
    <div className="panel" style={{ flex: 1, minHeight: 0 }}>
      <div className="panel-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.4375rem' }}>
          <span className="panel-title">P&amp;L surface</span>
          <span className="chip" title="Same figures as the P&L matrix, read as a height field">
            price × date × profit
          </span>
        </div>
        <div className="segment">
          <button className="segment-item" data-active={!enabled} onClick={() => setEnabled(false)}>Off</button>
          <button className="segment-item" data-active={enabled} onClick={() => setEnabled(true)}>3D</button>
        </div>
      </div>

      <div className="panel-body panel-body--flush" style={{ flex: 1, minHeight: 0 }}>
        {error ? (
          <div className="empty-state empty-state--error">
            <span className="empty-state-title">Unavailable</span>
            <span>{error}</span>
          </div>
        ) : !enabled ? (
          <div className="empty-state">
            <span className="empty-state-title">Surface off</span>
            {/* Off by default and said plainly. WebGL costs a context and a
                render loop, and most of the time the grid next door answers the
                question faster. */}
            <span>Turn on 3D to see the position as a height field.</span>
          </div>
        ) : isLoading ? (
          <div className="empty-state"><span className="empty-state-title">Computing…</span></div>
        ) : !field ? (
          <div className="empty-state">
            <span className="empty-state-title">No surface yet</span>
            <span>Add priced legs to compute P&amp;L across price and date.</span>
          </div>
        ) : (
          <Canvas camera={{ position: [0, 0, 5.4], fov: 42 }} dpr={[1, 1.75]} style={{ background: 'transparent' }}>
            <ambientLight intensity={0.85} />
            <directionalLight position={[3, 5, 4]} intensity={1.1} />
            <directionalLight position={[-4, -2, 2]} intensity={0.35} />
            <Surface {...field} />
          </Canvas>
        )}
      </div>
    </div>
  );
}

export default PnLSurface;
