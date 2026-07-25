'use client';

import React from 'react';
import { ShieldAlert, TrendingUp, TrendingDown, Activity } from 'lucide-react';
import styles from './RiskMetrics.module.css';

interface RiskMetricsProps {
  impliedVolatility?: number;
  riskFreeRate?: number;
  valueAtRisk?: number;
  beta?: number;
  sharpeRatio?: number;
}

export default function RiskMetrics({
  impliedVolatility = 24.5,
  riskFreeRate = 4.25,
  valueAtRisk = 12500,
  beta = 1.15,
  sharpeRatio = 1.8,
}: RiskMetricsProps) {
  return (
    <div className={styles.container}>
      <h2 className={styles.title}>
        <ShieldAlert size={28} color="#00f2fe" />
        Risk Metrics
      </h2>

      <div className={styles.metricsGrid}>
        <div className={styles.metricCard}>
          <div className={styles.metricLabel}>Implied Volatility</div>
          <div className={styles.metricValue}>
            {impliedVolatility.toFixed(1)} <span className={styles.metricUnit}>%</span>
          </div>
          <div className={styles.trendPositive}>
            <TrendingUp size={14} /> +1.2% (1w)
          </div>
        </div>

        <div className={styles.metricCard}>
          <div className={styles.metricLabel}>Risk-Free Rate</div>
          <div className={styles.metricValue}>
            {riskFreeRate.toFixed(2)} <span className={styles.metricUnit}>%</span>
          </div>
          <div className={styles.trendPositive}>
            <TrendingUp size={14} /> +0.25% (YTD)
          </div>
        </div>

        <div className={styles.metricCard}>
          <div className={styles.metricLabel}>Value at Risk (95%)</div>
          <div className={styles.metricValue}>
            <span className={styles.metricUnit}>$</span> {valueAtRisk.toLocaleString()}
          </div>
          <div className={styles.trendNegative}>
            <TrendingDown size={14} /> -3.4% (1m)
          </div>
        </div>

        <div className={styles.metricCard}>
          <div className={styles.metricLabel}>Asset Beta</div>
          <div className={styles.metricValue}>
            {beta.toFixed(2)}
          </div>
          <div className={styles.trendNeutral}>
            <Activity size={14} /> High correlation
          </div>
        </div>

        <div className={styles.metricCard}>
          <div className={styles.metricLabel}>Sharpe Ratio</div>
          <div className={styles.metricValue}>
            {sharpeRatio.toFixed(2)}
          </div>
          <div className={styles.trendPositive}>
            <TrendingUp size={14} /> Excellent
          </div>
        </div>
      </div>
    </div>
  );
}
