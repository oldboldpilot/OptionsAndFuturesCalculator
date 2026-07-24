import React from 'react';
import './RiskMetricsPanel.css';

export const RiskMetricsPanel: React.FC = () => {
  return (
    <div className="glass-panel risk-metrics-panel">
      <div className="metrics-header">
        <h2 className="heading-2">Risk & Probability Analysis</h2>
      </div>
      
      <div className="metrics-grid">
        <div className="metric-card">
          <div className="metric-label">Implied Volatility (IV)</div>
          <div className="metric-value iv-value">24.5%</div>
          <div className="metric-subtext">30-Day Avg: 22.1%</div>
        </div>
        
        <div className="metric-card">
          <div className="metric-label">Risk-Free Rate</div>
          <div className="metric-value">4.25%</div>
          <div className="metric-subtext">US 10Y Treasury</div>
        </div>
        
        <div className="metric-card">
          <div className="metric-label">Value at Risk (VaR)</div>
          <div className="metric-value var-value">-$1,250</div>
          <div className="metric-subtext">95% Confidence (1-Day)</div>
        </div>
      </div>

      <div className="probability-section">
        <h3 className="section-title">Probability Distribution (Log-Normal)</h3>
        <div className="distribution-chart-container">
          {/* Simulated Distribution Chart using CSS/SVG for Vanilla styling */}
          <svg viewBox="0 0 400 150" className="dist-chart">
            <defs>
              <linearGradient id="distGradient" x1="0" x2="0" y1="0" y2="1">
                <stop offset="0%" stopColor="var(--accent-glow)" stopOpacity="0.4" />
                <stop offset="100%" stopColor="var(--accent-glow)" stopOpacity="0.05" />
              </linearGradient>
            </defs>
            {/* Base line */}
            <line x1="0" y1="140" x2="400" y2="140" stroke="var(--glass-border)" strokeWidth="2" />
            
            {/* Bell Curve */}
            <path 
              d="M 10 140 C 80 140, 150 10, 200 10 C 250 10, 320 140, 390 140" 
              fill="url(#distGradient)" 
              stroke="var(--text-primary)" 
              strokeWidth="2" 
            />
            
            {/* Current Price Line */}
            <line x1="200" y1="10" x2="200" y2="140" stroke="var(--text-secondary)" strokeWidth="1" strokeDasharray="4 4" />
            <text x="200" y="155" fill="var(--text-secondary)" fontSize="12" textAnchor="middle">Current</text>
            
            {/* 1 Standard Deviation Lines */}
            <line x1="140" y1="40" x2="140" y2="140" stroke="var(--text-secondary)" strokeWidth="1" strokeDasharray="2 2" opacity="0.5" />
            <text x="140" y="155" fill="var(--text-secondary)" fontSize="10" textAnchor="middle" opacity="0.7">-1σ</text>
            
            <line x1="260" y1="40" x2="260" y2="140" stroke="var(--text-secondary)" strokeWidth="1" strokeDasharray="2 2" opacity="0.5" />
            <text x="260" y="155" fill="var(--text-secondary)" fontSize="10" textAnchor="middle" opacity="0.7">+1σ</text>
          </svg>
        </div>
        <div className="prob-stats">
          <div className="prob-stat">
            <span className="stat-label">POP (Prob. of Profit):</span>
            <span className="stat-value success">68.2%</span>
          </div>
          <div className="prob-stat">
            <span className="stat-label">Expected Value:</span>
            <span className="stat-value success">+$340</span>
          </div>
        </div>
      </div>
    </div>
  );
};
