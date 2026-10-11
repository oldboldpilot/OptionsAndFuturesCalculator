/**
 * What a results panel says when the position needs Pro.
 *
 * The full notice -- the engine's own sentence, the checkout buttons and the
 * price -- is shown ONCE, in the results strip. Probability, matrix, surface
 * and outcome each used to carry the entire pitch, so a refused two-leg
 * position drew the identical block three times on one screen, and the copy in
 * the narrowest of them was clipped. These panels now say the one fact they
 * need to ("this panel is empty because of the gate") and point at the notice.
 */
export function GateNote() {
  return (
    <div className="empty-state empty-state--gate">
      <span className="empty-state-title">Needs Pro</span>
      <span>This position needs Pro. The notice at the top of the results says why.</span>
    </div>
  );
}

export default GateNote;
