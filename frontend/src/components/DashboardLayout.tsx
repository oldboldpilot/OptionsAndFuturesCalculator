'use client';

import React, { ReactNode, useState } from 'react';
import { Sidebar } from './Sidebar';
import { AuthUI } from './AuthUI';
import { useCalculatorStore } from '../store/useCalculatorStore';
import './DashboardLayout.css';

interface DashboardLayoutProps {
  children: ReactNode;
}

export const DashboardLayout: React.FC<DashboardLayoutProps> = ({ children }) => {
  const { symbol, setSymbol, calculateStrategy } = useCalculatorStore();
  const [topSearch, setTopSearch] = useState(symbol);

  const handleSearchSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!topSearch.trim()) return;
    setSymbol(topSearch.trim());
    calculateStrategy();
  };

  return (
    <div className="dashboard-layout">
      <Sidebar />
      <main className="dashboard-main">
        <header className="topbar">
          <form onSubmit={handleSearchSubmit} className="topbar-search">
            <svg width="18" height="18" fill="none" stroke="currentColor" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
            </svg>
            <input 
              type="text" 
              value={topSearch}
              onChange={(e) => setTopSearch(e.target.value)}
              placeholder="Search symbol (e.g. SPY, NQ, NVDA, BTC) & press Enter..." 
            />
          </form>
          <div className="topbar-actions">
            <button className="action-btn" aria-label="Notifications">
              <svg width="20" height="20" fill="none" stroke="currentColor" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M15 17h5l-1.405-1.405A2.032 2.032 0 0118 14.158V11a6.002 6.002 0 00-4-5.659V5a2 2 0 10-4 0v.341C7.67 6.165 6 8.388 6 11v3.159c0 .538-.214 1.055-.595 1.436L4 17h5m6 0v1a3 3 0 11-6 0v-1m6 0H9" />
              </svg>
            </button>
            <button className="btn" onClick={() => calculateStrategy()}>
              New Trade
            </button>
            <AuthUI />
          </div>
        </header>
        <div className="dashboard-content">
          {children}
        </div>
      </main>
    </div>
  );
};
