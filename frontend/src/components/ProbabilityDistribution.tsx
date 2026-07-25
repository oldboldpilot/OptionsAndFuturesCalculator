'use client';

import React, { useMemo } from 'react';
import { AreaChart, Area, XAxis, YAxis, Tooltip, ResponsiveContainer, ReferenceLine } from 'recharts';
import { Activity } from 'lucide-react';
import styles from './ProbabilityDistribution.module.css';

interface ProbabilityDistributionProps {
  mean?: number;
  stdDev?: number;
}

// Generates points for a normal distribution curve
const generateNormalDistribution = (mean: number, stdDev: number, points: number = 100) => {
  const data = [];
  const min = mean - 4 * stdDev;
  const max = mean + 4 * stdDev;
  const step = (max - min) / points;

  for (let i = 0; i <= points; i++) {
    const x = min + i * step;
    // Normal distribution formula
    const y =
      (1 / (stdDev * Math.sqrt(2 * Math.PI))) *
      Math.exp(-0.5 * Math.pow((x - mean) / stdDev, 2));
    
    data.push({ x: Number(x.toFixed(2)), probability: Number(y.toFixed(4)) });
  }
  return data;
};

// eslint-disable-next-line @typescript-eslint/no-explicit-any
const CustomTooltip = ({ active, payload, label }: any) => {
  if (active && payload && payload.length) {
    return (
      <div className={styles.tooltip}>
        <div className={styles.tooltipLabel}>Underlying Price</div>
        <div className={styles.tooltipValue}>
          ${label}
        </div>
        <div style={{ marginTop: '8px', fontSize: '0.875rem', color: 'rgba(255,255,255,0.8)' }}>
          Probability Density: {(payload[0].value * 100).toFixed(2)}%
        </div>
      </div>
    );
  }
  return null;
};

export default function ProbabilityDistribution({
  mean = 100,
  stdDev = 15,
}: ProbabilityDistributionProps) {
  const data = useMemo(() => generateNormalDistribution(mean, stdDev), [mean, stdDev]);

  return (
    <div className={styles.container}>
      <h2 className={styles.title}>
        <Activity size={28} color="#f093fb" />
        Probability Distribution
      </h2>
      
      <div className={styles.chartContainer}>
        <ResponsiveContainer width="100%" height="100%">
          <AreaChart data={data} margin={{ top: 20, right: 30, left: 0, bottom: 20 }}>
            <defs>
              <linearGradient id="colorProb" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#f093fb" stopOpacity={0.8} />
                <stop offset="95%" stopColor="#f5576c" stopOpacity={0} />
              </linearGradient>
            </defs>
            <XAxis 
              dataKey="x" 
              stroke="rgba(255,255,255,0.4)" 
              tick={{ fill: 'rgba(255,255,255,0.6)', fontSize: 13, fontFamily: 'Inter, sans-serif' }} 
              tickLine={false}
              axisLine={{ stroke: 'rgba(255,255,255,0.1)' }}
              tickFormatter={(value) => `$${value}`}
              dy={10}
            />
            <YAxis hide />
            <Tooltip content={<CustomTooltip />} cursor={{ stroke: 'rgba(255,255,255,0.2)', strokeWidth: 1, strokeDasharray: '3 3' }} />
            <ReferenceLine 
              x={mean} 
              stroke="#00f2fe" 
              strokeDasharray="4 4" 
              label={{ position: 'top', value: 'Expected', fill: '#00f2fe', fontSize: 13, fontFamily: 'Inter, sans-serif' }} 
            />
            <Area
              type="monotone"
              dataKey="probability"
              stroke="#f093fb"
              strokeWidth={3}
              fillOpacity={1}
              fill="url(#colorProb)"
              animationDuration={1500}
              activeDot={{ r: 6, fill: '#f5576c', stroke: '#fff', strokeWidth: 2 }}
            />
          </AreaChart>
        </ResponsiveContainer>
      </div>

      <div className={styles.statsRow}>
        <div className={styles.statItem}>
          <span className={styles.statLabel}>Expected Value</span>
          <span className={styles.statValue}>${mean.toFixed(2)}</span>
        </div>
        <div className={styles.statItem}>
          <span className={styles.statLabel}>1σ Range (~68%)</span>
          <span className={styles.statValue}>
            ${(mean - stdDev).toFixed(2)} - ${(mean + stdDev).toFixed(2)}
          </span>
        </div>
        <div className={styles.statItem}>
          <span className={styles.statLabel}>Implied Volatility</span>
          <span className={styles.statValue}>{(stdDev / mean * 100).toFixed(1)}%</span>
        </div>
      </div>
    </div>
  );
}
