#pragma once

// LoanTerms — stub struct for forced-loan terms passed between CitySimulation and UIManager.
// Source location: src/interfaces/ (not src/simulation/) so that UIManager can include it
// without creating a circular src/ui/ -> src/simulation/ dependency.
//
// Field naming matches Phase 3 spec exactly:
//   amount         — post-cap principal already computed by CitySimulation
//   repaymentTicks — populated by CitySimulation using SimulationConstants::loan_repayment_ticks
//                    (12 for forced loans) or SimulationConstants::bond_repayment_ticks (24 for
//                    emergency bonds) — never hardcode the literal value here
//   interestRate   — fractional rate (0.05f = 5%/year); unified rate per
//                    architecture/game-design/economy-model.md; callers use this value directly
//                    in the interest formula: interest_per_tick = outstanding_debt * (interestRate / ticks_per_year)
struct LoanTerms {
    float amount{0.0f};
    int   repaymentTicks{0};
    float interestRate{0.05f};
};
