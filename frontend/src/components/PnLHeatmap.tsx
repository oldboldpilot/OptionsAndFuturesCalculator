'use client';

import React, { useRef, useMemo } from 'react';
import { Canvas, useFrame } from '@react-three/fiber';
import { OrbitControls } from '@react-three/drei';
import * as THREE from 'three';
import { MatrixCell } from '../store/useCalculatorStore';

interface PnLHeatmapProps {
  data: MatrixCell[];
}

const HeatmapMesh: React.FC<{ data: MatrixCell[] }> = ({ data }) => {
  const meshRef = useRef<THREE.Mesh>(null);

  // Generate geometry from data
  const { geometry, colors } = useMemo(() => {
    // In a real scenario, this builds a parametric surface based on price & dte
    const geom = new THREE.PlaneGeometry(10, 10, 50, 50);
    
    // Fake vertex displacement and coloring for the scaffold
    const colors = new Float32Array(geom.attributes.position.count * 3);
    const positions = geom.attributes.position.array;
    
    for (let i = 0; i < positions.length; i += 3) {
      // Create a saddle shape for options PnL visualization
      const x = positions[i];
      const y = positions[i + 1];
      const z = (x * x - y * y) * 0.1; 
      positions[i + 2] = z;

      // Color mapping: green for profit, red for loss
      if (z > 0) {
        colors[i] = 0.06; // R
        colors[i + 1] = 0.72; // G
        colors[i + 2] = 0.5; // B
      } else {
        colors[i] = 0.93; // R
        colors[i + 1] = 0.26; // G
        colors[i + 2] = 0.26; // B
      }
    }
    
    geom.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    geom.computeVertexNormals();
    return { geometry: geom, colors };
  }, [data]);

  useFrame((state) => {
    if (meshRef.current) {
      meshRef.current.rotation.z = state.clock.elapsedTime * 0.1;
    }
  });

  return (
    <mesh ref={meshRef} geometry={geometry} rotation={[-Math.PI / 3, 0, 0]}>
      <meshStandardMaterial 
        vertexColors 
        side={THREE.DoubleSide} 
        wireframe={false} 
        roughness={0.4} 
        metalness={0.2} 
        transparent 
        opacity={0.85}
      />
    </mesh>
  );
};

export const PnLHeatmap: React.FC<PnLHeatmapProps> = ({ data }) => {
  return (
    <div className="glass-panel" style={{ height: '500px', width: '100%', position: 'relative' }}>
      <div style={{ position: 'absolute', top: '20px', left: '20px', zIndex: 10 }}>
        <h3 className="heading-1" style={{ fontSize: '1.5rem', margin: 0 }}>3D P&L Surface</h3>
        <p style={{ color: 'var(--text-secondary)' }}>Interactive WebGL Risk Profile</p>
      </div>
      <Canvas camera={{ position: [0, -5, 12], fov: 45 }}>
        <ambientLight intensity={0.5} />
        <directionalLight position={[10, 10, 5]} intensity={1} />
        <pointLight position={[-10, -10, -10]} intensity={0.5} />
        <HeatmapMesh data={data} />
        <OrbitControls enablePan={false} maxPolarAngle={Math.PI / 2} />
      </Canvas>
    </div>
  );
};
