import { ImageResponse } from 'next/og';

export const runtime = 'edge';

export async function GET(request: Request) {
  try {
    const { searchParams } = new URL(request.url);
    
    // Extract strategy from query string, e.g. ?strategy=iron-condor
    const strategyQuery = searchParams.get('strategy') || 'options';
    const strategyName = strategyQuery
      .split('-')
      .map(word => word.charAt(0).toUpperCase() + word.slice(1))
      .join(' ');

    return new ImageResponse(
      (
        <div
          style={{
            height: '100%',
            width: '100%',
            display: 'flex',
            flexDirection: 'column',
            alignItems: 'center',
            justifyContent: 'center',
            backgroundColor: '#0f172a', // Slate 900
            backgroundImage: 'radial-gradient(circle at 25px 25px, #1e293b 2%, transparent 0%), radial-gradient(circle at 75px 75px, #1e293b 2%, transparent 0%)',
            backgroundSize: '100px 100px',
            color: 'white',
            fontFamily: 'sans-serif',
          }}
        >
          <div
            style={{
              display: 'flex',
              flexDirection: 'column',
              alignItems: 'center',
              justifyContent: 'center',
              background: 'rgba(30, 41, 59, 0.7)',
              padding: '60px 80px',
              borderRadius: '24px',
              border: '1px solid rgba(255, 255, 255, 0.1)',
              boxShadow: '0 25px 50px -12px rgba(0, 0, 0, 0.5)',
            }}
          >
            <h1
              style={{
                fontSize: '72px',
                fontWeight: 'bold',
                background: 'linear-gradient(to right, #38bdf8, #818cf8)',
                backgroundClip: 'text',
                color: 'transparent',
                marginBottom: '20px',
                textAlign: 'center',
              }}
            >
              {strategyName} Calculator
            </h1>
            
            <p
              style={{
                fontSize: '36px',
                color: '#cbd5e1',
                textAlign: 'center',
                maxWidth: '800px',
              }}
            >
              Model your risk, visualize maximum profit, and execute directly via partner brokers.
            </p>

            <div
              style={{
                display: 'flex',
                marginTop: '60px',
                gap: '24px',
              }}
            >
              <div style={{ display: 'flex', background: 'rgba(56, 189, 248, 0.2)', padding: '16px 32px', borderRadius: '12px', border: '1px solid rgba(56, 189, 248, 0.4)' }}>
                <span style={{ fontSize: '28px', color: '#38bdf8' }}>3D P&L Heatmap</span>
              </div>
              <div style={{ display: 'flex', background: 'rgba(129, 140, 248, 0.2)', padding: '16px 32px', borderRadius: '12px', border: '1px solid rgba(129, 140, 248, 0.4)' }}>
                <span style={{ fontSize: '28px', color: '#818cf8' }}>Probability Curves</span>
              </div>
            </div>
          </div>
        </div>
      ),
      {
        width: 1200,
        height: 630,
      }
    );
  } catch (e: any) {
    return new Response(`Failed to generate image`, {
      status: 500,
    });
  }
}
