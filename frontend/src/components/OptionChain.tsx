"use client";
'use client';

import React, { useState } from 'react';
import styles from './OptionChain.module.css';

interface OptionData {
  strike: number;
  callBid: number;
  callAsk: number;
  callVolume: number;
  callOI: number;
  putBid: number;
  putAsk: number;
  putVolume: number;
  putOI: number;
}

const mockData: OptionData[] = [
  { strike: 100, callBid: 5.5, callAsk: 5.7, callVolume: 1200, callOI: 3000, putBid: 0.1, putAsk: 0.2, putVolume: 150, putOI: 400 },
  { strike: 105, callBid: 2.1, callAsk: 2.3, callVolume: 3200, callOI: 5000, putBid: 0.5, putAsk: 0.6, putVolume: 300, putOI: 800 },
  { strike: 110, callBid: 0.8, callAsk: 0.9, callVolume: 5100, callOI: 8000, putBid: 1.2, putAsk: 1.4, putVolume: 1200, putOI: 2000 },
  { strike: 115, callBid: 0.2, callAsk: 0.3, callVolume: 800, callOI: 1500, putBid: 3.5, putAsk: 3.8, putVolume: 4000, putOI: 6000 },
  { strike: 120, callBid: 0.05, callAsk: 0.1, callVolume: 200, callOI: 500, putBid: 7.8, putAsk: 8.2, putVolume: 1000, putOI: 1500 },
];

export default function OptionChain() {
  const [expiry, setExpiry] = useState('2026-08-21');

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <h2 className={styles.title}>Option Chain</h2>
        <select className={styles.select} value={expiry} onChange={(e) => setExpiry(e.target.value)}>
          <option value="2026-07-31">July 31, 2026</option>
          <option value="2026-08-21">August 21, 2026</option>
          <option value="2026-09-18">September 18, 2026</option>
        </select>
      </div>

      <div className={styles.tableWrapper}>
        <table className={styles.chainTable}>
          <thead>
            <tr>
              <th colSpan={4} className={styles.callHeader}>CALLS</th>
              <th className={styles.strikeHeader}>STRIKE</th>
              <th colSpan={4} className={styles.putHeader}>PUTS</th>
            </tr>
            <tr className={styles.subHeader}>
              <th>OI</th>
              <th>Vol</th>
              <th>Bid</th>
              <th>Ask</th>
              <th></th>
              <th>Bid</th>
              <th>Ask</th>
              <th>Vol</th>
              <th>OI</th>
            </tr>
          </thead>
          <tbody>
            {mockData.map((row, index) => (
              <tr key={index} className={styles.row}>
                <td className={styles.cell}>{row.callOI}</td>
                <td className={styles.cell}>{row.callVolume}</td>
                <td className={`${styles.cell} ${styles.bid}`}>{row.callBid.toFixed(2)}</td>
                <td className={`${styles.cell} ${styles.ask}`}>{row.callAsk.toFixed(2)}</td>
                <td className={styles.strikeCell}>{row.strike}</td>
                <td className={`${styles.cell} ${styles.bid}`}>{row.putBid.toFixed(2)}</td>
                <td className={`${styles.cell} ${styles.ask}`}>{row.putAsk.toFixed(2)}</td>
                <td className={styles.cell}>{row.putVolume}</td>
                <td className={styles.cell}>{row.putOI}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
