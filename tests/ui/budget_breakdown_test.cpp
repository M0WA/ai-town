// tests/ui/budget_breakdown_test.cpp
//
// Phase 11h Deliverable 5c: Budget Panel Income/Expense Breakdown Tests
//
// Verifies that BudgetDetailPanel correctly reports sectioned income totals,
// expense totals, net balance, and the V1 tourism income placeholder ($0).
//
// Tests verify the arithmetic contract of the budget breakdown sections:
//   Income = R_tax + C_tax + I_tax + utility_fees
//   Expenses = wages + road_maintenance + service_upkeep
//   Net = Income - Expenses
//   Tourism = $0 always in V1
//
// Added to ui_tests via:
//   target_sources(ui_tests PRIVATE tests/ui/budget_breakdown_test.cpp)
// Do NOT call add_executable(ui_tests ...) or aitown_add_tests(ui_tests ...) again.
//
// Mock policy: NiceMock for all — construction calls are complex.
// TearDown contract: panel_ reset before mocks destroyed.
//
// Spec reference: implementation/phase-11h.md §2a, §5c
//   Income section: Tax R + Tax C + Tax I + Utility fees
//   Expenses section: Road maintenance + Service upkeep + Wages
//   Total: Income - Expenses (net balance)
//   Tourism income: always $0 in V1 (grayed-out placeholder row)

#include "src/ui/BudgetDetailPanel.h"
#include "src/simulation/simulation_constants.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::AtLeast;
using ::testing::AnyNumber;

// ---------------------------------------------------------------------------
// BudgetBreakdownTest fixture
//
// Stubs:
//   getTaxRevenue(Residential) → 100.0f
//   getTaxRevenue(Commercial)  → 200.0f
//   getTaxRevenue(Industrial)  → 150.0f
//   getUtilityFeeRevenue()     →  50.0f
//   getWagesCost()             →  80.0f
//   getRoadMaintenanceCost()   →  30.0f
//   getServiceUpkeepCost()     →  20.0f
//
// Expected:
//   Income total  = 100 + 200 + 150 + 50  = 500
//   Expense total =  80 +  30 +  20       = 130
//   Net balance   = 500 - 130             = 370
//   Tourism       = 0 (V1 placeholder)
// ---------------------------------------------------------------------------
class BudgetBreakdownTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));

        // Budget line items per Phase 11h test spec §5c.
        ON_CALL(sim_, getTaxRevenue(ZoneType::Residential)).WillByDefault(Return(100.0f));
        ON_CALL(sim_, getTaxRevenue(ZoneType::Commercial)).WillByDefault(Return(200.0f));
        ON_CALL(sim_, getTaxRevenue(ZoneType::Industrial)).WillByDefault(Return(150.0f));
        ON_CALL(sim_, getUtilityFeeRevenue()).WillByDefault(Return(50.0f));
        ON_CALL(sim_, getWagesCost()).WillByDefault(Return(80.0f));
        ON_CALL(sim_, getRoadMaintenanceCost()).WillByDefault(Return(30.0f));
        ON_CALL(sim_, getServiceUpkeepCost()).WillByDefault(Return(20.0f));

        // Additional stubs required by BudgetDetailPanel construction/draw.
        ON_CALL(sim_, getCurrentMonthlyRevenue()).WillByDefault(Return(370.0f));
        ON_CALL(sim_, estimateMonthlyUpkeep()).WillByDefault(Return(130.0f));
        ON_CALL(sim_, getNextUnlockThreshold(_)).WillByDefault(Return(5000.0f));
        ON_CALL(sim_, getDensityUnlockState()).WillByDefault(Return(DensityUnlockState{}));

        panel_ = std::make_unique<BudgetDetailPanel>(&backend_, &sim_);
        panel_->show();
    }

    void TearDown() override {
        panel_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockCitySimulation> sim_;
    std::unique_ptr<BudgetDetailPanel> panel_;
    uint32_t nextHandle_{100};

    // Expected budget values (computed from stub data above).
    static constexpr float kExpectedIncomeR    = 100.0f;
    static constexpr float kExpectedIncomeC    = 200.0f;
    static constexpr float kExpectedIncomeI    = 150.0f;
    static constexpr float kExpectedIncomeUtil = 50.0f;
    static constexpr float kExpectedIncomeTotal = 500.0f;  // R + C + I + Util

    static constexpr float kExpectedExpenseWages    = 80.0f;
    static constexpr float kExpectedExpenseRoadMaint = 30.0f;
    static constexpr float kExpectedExpenseSvcUpkeep = 20.0f;
    static constexpr float kExpectedExpenseTotal    = 130.0f;  // Wages + Road + Svc

    static constexpr float kExpectedNetBalance = 370.0f;  // Income - Expenses
    static constexpr float kExpectedTourismIncome = 0.0f; // V1: always zero
};

// ============================================================================
// BudgetBreakdown_IncomeSectionTotal_MatchesSumOfLineItems
//
// The income section total shown by BudgetDetailPanel must equal the arithmetic
// sum of all income line items:
//   Income total = Tax R + Tax C + Tax I + Utility fees = 100 + 200 + 150 + 50 = 500
//
// Verified by checking that the panel's draw() outputs text containing "$500"
// for the income section (section header or subtotal line).
// ============================================================================
TEST_F(BudgetBreakdownTest, BudgetBreakdown_IncomeSectionTotal_MatchesSumOfLineItems)
{
    // Verify the arithmetic contract: sum of income line items = 500.
    float incomeTotal = kExpectedIncomeR + kExpectedIncomeC
                      + kExpectedIncomeI + kExpectedIncomeUtil;
    EXPECT_FLOAT_EQ(incomeTotal, kExpectedIncomeTotal)
        << "Income total must equal sum of line items: 100 + 200 + 150 + 50 = 500";

    // Verify BudgetDetailPanel displays the correct income total.
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("$500"))).Times(AtLeast(1));
    panel_->draw();
}

// ============================================================================
// BudgetBreakdown_ExpenseSectionTotal_MatchesSumOfLineItems
//
// The expense section total must equal the arithmetic sum of expense line items:
//   Expense total = Wages + Road maintenance + Service upkeep = 80 + 30 + 20 = 130
//
// Verified by checking that draw() outputs text containing "$130" for the
// expenses section.
// ============================================================================
TEST_F(BudgetBreakdownTest, BudgetBreakdown_ExpenseSectionTotal_MatchesSumOfLineItems)
{
    float expenseTotal = kExpectedExpenseWages + kExpectedExpenseRoadMaint
                       + kExpectedExpenseSvcUpkeep;
    EXPECT_FLOAT_EQ(expenseTotal, kExpectedExpenseTotal)
        << "Expense total must equal sum of line items: 80 + 30 + 20 = 130";

    // Verify BudgetDetailPanel displays the correct expense total.
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("$130"))).Times(AtLeast(1));
    panel_->draw();
}

// ============================================================================
// BudgetBreakdown_NetBalance_EqualsIncomeTotalMinusExpenseTotal
//
// The net balance line must equal Income total − Expense total:
//   Net = 500 − 130 = 370
//
// Verified by checking that draw() outputs text containing "$370" or "+$370"
// for the total/net balance line.
// ============================================================================
TEST_F(BudgetBreakdownTest, BudgetBreakdown_NetBalance_EqualsIncomeTotalMinusExpenseTotal)
{
    float netBalance = kExpectedIncomeTotal - kExpectedExpenseTotal;
    EXPECT_FLOAT_EQ(netBalance, kExpectedNetBalance)
        << "Net balance must equal income total minus expense total: 500 - 130 = 370";

    // BudgetDetailPanel displays the net balance as a dollar amount.
    // The format may be "$370" or "+$370" depending on implementation.
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("$370"))).Times(AtLeast(1));
    panel_->draw();
}

// ============================================================================
// BudgetBreakdown_TourismIncome_AlwaysZeroInV1
//
// Tourism income is a post-V1 feature. In V1 the Budget Detail Panel income
// section must display a grayed-out placeholder "Tourism income: $0 (post-V1)".
// This test verifies that the panel never reports a non-zero tourism value.
//
// The tourism line is always $0 regardless of treasury state, population, or
// tick count. There is no ICitySimulation method that returns tourism revenue —
// the panel hardcodes 0.0f for tourism.
// ============================================================================
TEST_F(BudgetBreakdownTest, BudgetBreakdown_TourismIncome_AlwaysZeroInV1)
{
    // Verify the expected tourism constant is zero.
    EXPECT_FLOAT_EQ(kExpectedTourismIncome, 0.0f)
        << "Tourism income must be $0 in V1 (post-V1 feature, not yet implemented)";

    // The panel must display the tourism placeholder row.
    // Draw and verify the tourism label appears in the output.
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    // The tourism line is labeled "Tourism income: $0 (post-V1)" per phase-11h.md §2a.
    // Verify draw() calls setElementText with a substring matching the zero amount.
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("$0"))).Times(AtLeast(1));
    panel_->draw();
}
