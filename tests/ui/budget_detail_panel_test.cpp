// tests/ui/budget_detail_panel_test.cpp
//
// Phase 8 BudgetDetailPanel tests.
//
// Tests: BudgetDetailPanel data display, density unlock preview sentinel,
// and tax revenue per-zone display.
//
// Tests BudgetDetailPanel directly (standalone) because UIManager's HUD creates
// BudgetDetailPanel in hidden state. Direct testing gives us show() control.
//
// Mock policy: NiceMock for all (construction calls are complex).
// TearDown contract: panel_ reset before mocks destroyed.

#include "src/ui/BudgetDetailPanel.h"
#include "src/simulation/simulation_constants.h"
#include "tests/ui/mock_ui_backend.h"
#include "tests/ui/mock_city_simulation.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::AtLeast;
using ::testing::AnyNumber;

class BudgetDetailPanelTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));

        // Budget line items.
        ON_CALL(sim_, getTaxRevenue(ZoneType::Residential)).WillByDefault(Return(1200.0f));
        ON_CALL(sim_, getTaxRevenue(ZoneType::Commercial)).WillByDefault(Return(800.0f));
        ON_CALL(sim_, getTaxRevenue(ZoneType::Industrial)).WillByDefault(Return(600.0f));
        ON_CALL(sim_, getWagesCost()).WillByDefault(Return(500.0f));
        ON_CALL(sim_, getRoadMaintenanceCost()).WillByDefault(Return(200.0f));
        ON_CALL(sim_, getServiceUpkeepCost()).WillByDefault(Return(150.0f));
        ON_CALL(sim_, getUtilityFeeRevenue()).WillByDefault(Return(100.0f));
        ON_CALL(sim_, getCurrentMonthlyRevenue()).WillByDefault(Return(1850.0f));
        ON_CALL(sim_, estimateMonthlyUpkeep()).WillByDefault(Return(350.0f));
        ON_CALL(sim_, getNextUnlockThreshold(_)).WillByDefault(Return(5000.0f));
        ON_CALL(sim_, getDensityUnlockState()).WillByDefault(Return(DensityUnlockState{}));

        panel_ = std::make_unique<BudgetDetailPanel>(&backend_, &sim_);
        // Panel starts hidden; show it for draw tests.
        panel_->show();
    }

    void TearDown() override {
        panel_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockCitySimulation> sim_;
    std::unique_ptr<BudgetDetailPanel> panel_;
    uint32_t                     nextHandle_{100};
};

// DensityUnlockPreview_HiddenWhenSentinelReturned
// Verifies: when getNextUnlockThreshold() returns kNoUnlockThreshold (-1.0f),
// the Density Unlock Preview tooltip line is hidden.
TEST_F(BudgetDetailPanelTest, DensityUnlockPreview_HiddenWhenSentinelReturned) {
    ON_CALL(sim_, getNextUnlockThreshold(_)).WillByDefault(
        Return(SimulationConstants::kNoUnlockThreshold));

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());
    // Draw should set visible to false for the density unlock preview element.
    EXPECT_CALL(backend_, setElementVisible(_, false)).Times(AtLeast(1));
    panel_->draw();
}

// Tax revenue display per zone: residential.
// BudgetDetailPanel format: "Tax Residential: $1200"
TEST_F(BudgetDetailPanelTest, TaxRevenue_Residential_DisplaysCorrectValue) {
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("$1200"))).Times(AtLeast(1));
    panel_->draw();
}

// Tax revenue display per zone: commercial.
// BudgetDetailPanel format: "Tax Commercial: $800"
TEST_F(BudgetDetailPanelTest, TaxRevenue_Commercial_DisplaysCorrectValue) {
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("$800"))).Times(AtLeast(1));
    panel_->draw();
}

// Tax revenue display per zone: industrial.
// BudgetDetailPanel format: "Tax Industrial: $600"
TEST_F(BudgetDetailPanelTest, TaxRevenue_Industrial_DisplaysCorrectValue) {
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("$600"))).Times(AtLeast(1));
    panel_->draw();
}

// Wages cost display.
// BudgetDetailPanel format: "Wages: $500"
TEST_F(BudgetDetailPanelTest, WagesCost_DisplaysCorrectValue) {
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Wages: $500"))).Times(AtLeast(1));
    panel_->draw();
}

// Road maintenance cost display.
// BudgetDetailPanel format: "Road Maintenance: $200"
TEST_F(BudgetDetailPanelTest, RoadMaintenance_DisplaysCorrectValue) {
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Road Maintenance: $200"))).Times(AtLeast(1));
    panel_->draw();
}

// Service upkeep cost display.
// BudgetDetailPanel format: "Service Upkeep: $150"
TEST_F(BudgetDetailPanelTest, ServiceUpkeep_DisplaysCorrectValue) {
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Service Upkeep: $150"))).Times(AtLeast(1));
    panel_->draw();
}

// Utility fee revenue display.
// BudgetDetailPanel format: "Utility Fees: $100"
TEST_F(BudgetDetailPanelTest, UtilityFee_DisplaysCorrectValue) {
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Utility Fees: $100"))).Times(AtLeast(1));
    panel_->draw();
}

// Net balance display.
// BudgetDetailPanel format: "Net Monthly Balance: $1850"
TEST_F(BudgetDetailPanelTest, NetBalance_DisplaysCalculatedValue) {
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Net Monthly Balance: $1850"))).Times(AtLeast(1));
    panel_->draw();
}

// Density unlock preview shown when threshold is reachable and revenue >= 90% of threshold.
TEST_F(BudgetDetailPanelTest, DensityUnlockPreview_ShownWhenThresholdReachable) {
    // Threshold is 5000. Revenue must be >= 4500 (90% of 5000) for preview to show.
    ON_CALL(sim_, getNextUnlockThreshold(_)).WillByDefault(Return(5000.0f));
    ON_CALL(sim_, getCurrentMonthlyRevenue()).WillByDefault(Return(4800.0f));

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());
    // The unlock preview should be set visible.
    EXPECT_CALL(backend_, setElementVisible(_, true)).Times(AtLeast(1));
    panel_->draw();
}

// Panel starts hidden and then becomes visible after show().
TEST_F(BudgetDetailPanelTest, PanelStartsHidden_BecomesVisible) {
    // Panel was shown in SetUp() -- verify isVisible is true.
    EXPECT_TRUE(panel_->isVisible());
    panel_->hide();
    EXPECT_FALSE(panel_->isVisible());
}
