// tests/ui/finances_panel_background_test.cpp
//
// Phase 11m D7: FinancesPanel constructor calls setElementBackground with the
// correct ARGB values (13, 27, 42, 217) for the panel background element.
//
// Mock policy: NiceMock for all (many incidental backend calls during construction).
// TearDown contract: panel_.reset() before mock destructors.

#include "src/ui/FinancesPanel.h"
#include "src/ui/ui_types.h"
#include "src/interfaces/IUIBackend.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::NiceMock;
using ::testing::Return;

// ---------------------------------------------------------------------------
// FinancesPanelBackgroundTest fixture
// ---------------------------------------------------------------------------
class FinancesPanelBackgroundTest : public ::testing::Test {
protected:
    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;

    // panel_ declared after mocks — destroyed first.
    std::unique_ptr<FinancesPanel> panel_;

    void SetUp() override {
        // Backend stubs — FinancesPanel construction calls addStaticText/addButton.
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(false));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{0, 0, 0, 0}));

        // Sim stubs.
        ON_CALL(sim_, getTaxRate(_)).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(10000.0f));
        ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getWagesCost()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getRoadMaintenanceCost()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getServiceUpkeepCost()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getUtilityFeeRevenue()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getTaxRevenue(_)).WillByDefault(Return(0.0f));
        ON_CALL(sim_, estimateMonthlyUpkeep()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getCurrentMonthlyRevenue()).WillByDefault(Return(0.0f));
    }

    void TearDown() override {
        panel_.reset();
    }

private:
    UIElementHandle nextHandle_{0};
};

// ---------------------------------------------------------------------------
// Test: FinancesPanel_Constructor_CallsSetElementBackground
//
// Verify that FinancesPanel constructor calls setElementBackground with the
// panel background element handle and ARGB components (13, 27, 42, 217).
// ---------------------------------------------------------------------------
TEST_F(FinancesPanelBackgroundTest, FinancesPanel_Constructor_CallsSetElementBackground)
{
    // Expect setElementBackground called exactly once during construction
    // with the correct dark-navy translucent background ARGB.
    EXPECT_CALL(backend_, setElementBackground(_, 13, 27, 42, 217)).Times(1);

    // FinancesPanel constructor order: (backend, sim, audio, clock)
    // Note: this differs from the UIManager canonical 4-param order (backend, audio, sim, clock).
    panel_ = std::make_unique<FinancesPanel>(&backend_, &sim_, &audio_, &clock_);

    EXPECT_TRUE(::testing::Mock::VerifyAndClearExpectations(&backend_));
}
