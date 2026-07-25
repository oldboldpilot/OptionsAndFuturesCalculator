"use client";
import React, { useMemo, useRef, useState } from 'react';
import { Canvas, useFrame } from '@react-three/fiber';
import { OrbitControls, Text } from '@react-three/drei';
import * as THREE from 'three';
import styles from './OptionsHeatmap.module.css';

interface DataPoint {
  price: number;
  time: number;
  pnl: number;
}

// Generate mock data for the heatmap surface
const generateData = (resolution: number): DataPoint[][] => {
  const data: DataPoint[][] = [];
  const startPrice = 80;
  const endPrice = 120;
  const startDays = 0;
  const endDays = 30;

  for (let i = 0; i <= resolution; i++) {
    const row: DataPoint[] = [];
    for (let j = 0; j <= resolution; j++) {
      const price = startPrice + (endPrice - startPrice) * (i / resolution);
      const time = startDays + (endDays - startDays) * (j / resolution);
      
      // Simple mock formula for an option strategy PnL
      const normalizedPrice = (price - 100) / 10;
      const normalizedTime = (time - 15) / 15;
      const pnl = (10 - Math.abs(normalizedPrice * 5)) * Math.exp(-Math.abs(normalizedTime)) * 10 - 20;

      row.push({ price, time, pnl });
    }
    data.push(row);
  }
  return data;
};

const Surface = ({ data }: { data: DataPoint[][] }) => {
  const meshRef = useRef<THREE.Mesh>(null);
  
  const { geometry } = useMemo(() => {
    const resolution = data.length - 1;
    const geometry = new THREE.PlaneGeometry(10, 10, resolution, resolution);
    const colors = [];
    const positionAttribute = geometry.attributes.position;
    
    // Find min/max for color scaling
    let minPnl = Infinity;
    let maxPnl = -Infinity;
    data.forEach(row => {
      row.forEach(point => {
        if (point.pnl < minPnl) minPnl = point.pnl;
        if (point.pnl > maxPnl) maxPnl = point.pnl;
      });
    });

    const maxAbsPnl = Math.max(Math.abs(minPnl), Math.abs(maxPnl));

    let vertexIndex = 0;
    for (let i = 0; i <= resolution; i++) {
      for (let j = 0; j <= resolution; j++) {
        const point = data[j][i];
        
        // Z-axis represents PnL
        const z = point.pnl / maxAbsPnl * 2;
        positionAttribute.setZ(vertexIndex, z);

        // Color based on PnL (Red for loss, Green for profit)
        const normalizedPnl = point.pnl / maxAbsPnl;
        const color = new THREE.Color();
        if (normalizedPnl > 0) {
          color.setHSL(0.33, 1, 0.2 + normalizedPnl * 0.5); // Green
        } else {
          color.setHSL(0.0, 1, 0.2 - normalizedPnl * 0.5); // Red
        }
        colors.push(color.r, color.g, color.b);

        vertexIndex++;
      }
    }

    geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
    geometry.computeVertexNormals();

    return { geometry };
  }, [data]);

  useFrame((state) => {
    if (meshRef.current) {
      // Subtle floating animation
      meshRef.current.position.y = Math.sin(state.clock.elapsedTime * 0.5) * 0.1;
    }
  });

  return (
    <group rotation={[-Math.PI / 2 + 0.3, 0, 0.2]}>
      <mesh ref={meshRef} geometry={geometry}>
        <meshStandardMaterial 
          vertexColors 
          side={THREE.DoubleSide} 
          wireframe={false}
          roughness={0.4}
          metalness={0.6}
        />
      </mesh>
      {/* Grid helper for better context */}
      <gridHelper args={[10, 20, 0xffffff, 0xffffff]} position={[0, -2.5, 0]} material-opacity={0.1} material-transparent />
      
      {/* Labels */}
      <Text position={[0, -2.5, 5.5]} fontSize={0.4} color="#a5b4fc" anchorX="center" anchorY="middle">
        Underlying Price
      </Text>
      <Text position={[-5.5, -2.5, 0]} rotation={[0, -Math.PI / 2, 0]} fontSize={0.4} color="#a5b4fc" anchorX="center" anchorY="middle">
        Days to Expiration
      </Text>
    </group>
  );
};

export default function OptionsHeatmap() {
  const [resolution] = useState(40);
  const data = useMemo(() => generateData(resolution), [resolution]);

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <h2 className={styles.title}>3D PnL Heatmap</h2>
        <div className={styles.controls}>
          <span className={styles.badgeProfit}>Profit</span>
          <span className={styles.badgeLoss}>Loss</span>
        </div>
      </div>
      
      <div className={styles.canvasContainer}>
        <Canvas camera={{ position: [0, 8, 12], fov: 45 }}>
          <ambientLight intensity={0.5} />
          <pointLight position={[10, 10, 10]} intensity={1} />
          <spotLight position={[-10, 10, 5]} angle={0.3} penumbra={1} intensity={2} color="#818cf8" />
          <Surface data={data} />
          <OrbitControls 
            enablePan={false}
            minPolarAngle={Math.PI / 6}
            maxPolarAngle={Math.PI / 2.5}
            minDistance={5}
            maxDistance={20}
            autoRotate
            autoRotateSpeed={0.5}
          />
        </Canvas>
      </div>
      <div className={styles.footer}>
        <p>Interactive 3D visualization of option strategy profitability. Drag to rotate, scroll to zoom.</p>
      </div>
    </div>
  );
}
