#pragma once

// LoanTerms — stub struct for forced-loan terms passed between CitySimulation and UIManager.
// Source location: src/interfaces/ (not src/simulation/) so that UIManager can include it
// without creating a circular src/ui/ -> src/simulation/ dependency.
struct LoanTerms {
    float principal;        // post-cap amount already computed by CitySimulation
    float interestRatePct;  // 5.0f — a PERCENTAGE value (5%), NOT a fractional rate (0.05);
                            // callers must use (interestRatePct / 100.0f) to obtain the
                            // fractional rate for arithmetic; unified rate per
                            // architecture/game-design/economy-model.md
    int   repaymentTicks;   // Must be populated by CitySimulation using
                            // SimulationConstants::loan_repayment_ticks
                            // (=12 for forced loans, 24 for emergency bonds) —
                            // never hardcode the literal value here.
    bool  isFirstLoan;      // true when outstanding_debt==0 at issuance (no debt-cap applied)
};
