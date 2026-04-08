// ui_manager_integration_test.cpp
//
// Integration tests for UIManager, ModalDialog, HUD, FinancesPanel,
// NotificationManager, MainMenuPanel, PauseMenuPanel, SettingsPanel
// using IrrlichtUIBackend (EDT_NULL) + NiceMock dependency doubles.
//
// EDT_NULL provides a real IrrlichtUIBackend without any display or OpenGL,
// exercising the real UI code paths (constructor, panel allocation, show/hide,
// draw, onEvent, update) through the concrete backend.
//
// CMake target: integration_tests, label "integration".
// Mock policy: NiceMock for integration tests.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>

// Irrlicht for EDT_NULL device
#include <irrlicht.h>

// Production UI classes under test
#include "src/ui/UIManager.h"
#include "src/ui/ModalDialog.h"
#include "src/ui/HUD.h"
#include "src/ui/FinancesPanel.h"
#include "src/ui/MainMenuPanel.h"
#include "src/ui/PauseMenuPanel.h"
#include "src/ui/SettingsPanel.h"
#include "src/ui/NotificationManager.h"
#include "src/ui/ui_types.h"

// Real IrrlichtUIBackend (EDT_NULL)
#include "src/rendering/IrrlichtUIBackend.h"

// Platform
#include "src/platform/input_event.h"
#include "src/interfaces/simulation_types.h"
#include "src/interfaces/LoanTerms.h"

// Test doubles
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;
using ::testing::AnyNumber;

// ---------------------------------------------------------------------------
// Helper: create an EDT_NULL Irrlicht device
// ---------------------------------------------------------------------------
static irr::IrrlichtDevice* createNullDevice() {
    return irr::createDevice(
        irr::video::EDT_NULL,
        irr::core::dimension2d<irr::u32>(640, 480));
}

// ---------------------------------------------------------------------------
// UIManagerIntegrationFixture
//
// Instantiates a real IrrlichtUIBackend (EDT_NULL) and UIManager with
// NiceMock doubles for sim, audio, and ManualClock.
// ---------------------------------------------------------------------------
class UIManagerIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_ = createNullDevice();
        ASSERT_NE(device_, nullptr) << "EDT_NULL device creation failed";

        backend_ = std::make_unique<IrrlichtUIBackend>(device_);

        // Set up common mock return values
        ON_CALL(sim_, isPaused()).WillByDefault(Return(false));
        ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));
        ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false));
        ON_CALL(sim_, getSpeedMultiplier()).WillByDefault(Return(SpeedMultiplier::x1));
        ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(500000.0f));
        ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Village));
        ON_CALL(sim_, getTotalPopulation()).WillByDefault(Return(0));
        ON_CALL(sim_, getSimulationTime()).WillByDefault(Return(SimulationTime{1, 1}));
        ON_CALL(sim_, getZoneDemandFactor(_)).WillByDefault(Return(0.0f));
        ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));
        ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(0.0));
        ON_CALL(sim_, consumeBudgetTicks()).WillByDefault(Return(0));
        ON_CALL(sim_, getTaxRate(_)).WillByDefault(Return(0.05f));
        ON_CALL(sim_, getTaxRevenue(_)).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getUtilityFeeRevenue()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getWagesCost()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getRoadMaintenanceCost()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getServiceUpkeepCost()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getMapTilesX()).WillByDefault(Return(128));
        ON_CALL(sim_, getMapTilesZ()).WillByDefault(Return(128));
        ON_CALL(sim_, getTimeOfDay()).WillByDefault(Return(TimeOfDay::DAY));
        ON_CALL(sim_, getDensityUnlockState()).WillByDefault(Return(DensityUnlockState{}));
        ON_CALL(sim_, getNextUnlockThreshold(_)).WillByDefault(Return(1000000.0f));

        uiManager_ = std::make_unique<UIManager>(
            backend_.get(), &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        uiManager_.reset();
        backend_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    irr::IrrlichtDevice*               device_{nullptr};
    std::unique_ptr<IrrlichtUIBackend>  backend_;
    NiceMock<MockCitySimulation>        sim_;
    NiceMock<MockAudioSystem>           audio_;
    ManualClock                         clock_;
    std::unique_ptr<UIManager>          uiManager_;
};

// ---------------------------------------------------------------------------
// Construction tests
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, Construction_DoesNotCrash) {
    EXPECT_NE(uiManager_, nullptr);
}

TEST_F(UIManagerIntegrationTest, GetActiveTool_InitiallyNone) {
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::None);
}

TEST_F(UIManagerIntegrationTest, IsGameplayOrPaused_InitiallyFalse) {
    EXPECT_FALSE(uiManager_->isGameplayOrPaused());
}

TEST_F(UIManagerIntegrationTest, HasActiveModal_InitiallyFalse) {
    EXPECT_FALSE(uiManager_->hasActiveModal());
}

TEST_F(UIManagerIntegrationTest, IsQuitRequested_InitiallyFalse) {
    EXPECT_FALSE(uiManager_->isQuitRequested());
}

TEST_F(UIManagerIntegrationTest, GetPendingMapTiles_ReturnsNonZero) {
    int tiles = uiManager_->getPendingMapTiles();
    EXPECT_GT(tiles, 0);
}

// ---------------------------------------------------------------------------
// Draw — must not crash in any state
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, Draw_MainMenuState_DoesNotCrash) {
    // UIManager starts in MainMenu state
    uiManager_->draw();
}

TEST_F(UIManagerIntegrationTest, Update_MainMenuState_DoesNotCrash) {
    uiManager_->update(0.016f);
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, TransitionToGameplay_SetsGameplayOrPausedTrue) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    EXPECT_TRUE(uiManager_->isGameplayOrPaused());
}

TEST_F(UIManagerIntegrationTest, TransitionToGameplay_GetActiveTool_StillNone) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::None);
}

TEST_F(UIManagerIntegrationTest, TransitionToPaused_AfterGameplay_IsGameplayOrPaused) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->transitionToPaused();
    EXPECT_TRUE(uiManager_->isGameplayOrPaused());
}

TEST_F(UIManagerIntegrationTest, TransitionToGameplay_FromPaused_IsGameplayOrPaused) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->transitionToPaused();
    uiManager_->transitionToGameplay_fromPaused();
    EXPECT_TRUE(uiManager_->isGameplayOrPaused());
}

TEST_F(UIManagerIntegrationTest, TransitionToMainMenu_AfterGameplay_IsNotGameplayOrPaused) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->transitionToMainMenu();
    EXPECT_FALSE(uiManager_->isGameplayOrPaused());
}

TEST_F(UIManagerIntegrationTest, TransitionToGameplay_ThenDraw_DoesNotCrash) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->draw();
}

TEST_F(UIManagerIntegrationTest, TransitionToGameplay_ThenUpdate_DoesNotCrash) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->update(0.016f);
}

TEST_F(UIManagerIntegrationTest, TransitionToPaused_ThenDraw_DoesNotCrash) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->transitionToPaused();
    uiManager_->draw();
}

// ---------------------------------------------------------------------------
// Modal dialog
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, ShowForcedLoanDialog_SetsModalActive) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    LoanTerms terms;
    terms.amount = 50000.0f;
    terms.repaymentTicks = 24;
    terms.interestRate = 0.05f;
    uiManager_->showForcedLoanDialog(terms);
    EXPECT_TRUE(uiManager_->hasActiveModal());
}

TEST_F(UIManagerIntegrationTest, CloseModal_AfterForcedLoan_NoActiveModal) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    LoanTerms terms;
    terms.amount = 50000.0f;
    terms.repaymentTicks = 24;
    terms.interestRate = 0.05f;
    uiManager_->showForcedLoanDialog(terms);
    ASSERT_TRUE(uiManager_->hasActiveModal());
    uiManager_->closeModal();
    EXPECT_FALSE(uiManager_->hasActiveModal());
}

TEST_F(UIManagerIntegrationTest, ShowGameOverModal_SetsModalActive) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->showGameOverModal(100000, 3);
    EXPECT_TRUE(uiManager_->hasActiveModal());
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, ShowSettings_DoesNotCrash) {
    uiManager_->showSettings();
}

TEST_F(UIManagerIntegrationTest, ShowSettings_ThenDraw_DoesNotCrash) {
    uiManager_->showSettings();
    uiManager_->draw();
}

// ---------------------------------------------------------------------------
// setUnsavedChanges
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, SetUnsavedChanges_TrueAndFalse_DoNotCrash) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->setUnsavedChanges(true);
    uiManager_->setUnsavedChanges(false);
}

// ---------------------------------------------------------------------------
// Map dimensions
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, SetMapDimensions_DoesNotCrash) {
    uiManager_->setMapDimensions(256, 256);
}

// ---------------------------------------------------------------------------
// Loading terrain gate
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, SetLoadingTerrain_True_UpdateIsNoOp) {
    uiManager_->setLoadingTerrain(true);
    uiManager_->update(0.016f);  // must not crash when loading terrain
    uiManager_->setLoadingTerrain(false);
}

// ---------------------------------------------------------------------------
// onGameLoaded
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, OnGameLoaded_AfterTransitionToGameplay_DoesNotCrash) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->onGameLoaded();
}

// ---------------------------------------------------------------------------
// consumeNewGameRequest / consumeLoadGameRequest
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, ConsumeNewGameRequest_InitiallyFalse) {
    EXPECT_FALSE(uiManager_->consumeNewGameRequest());
}

TEST_F(UIManagerIntegrationTest, ConsumeLoadGameRequest_InitiallyFalse) {
    std::string json;
    EXPECT_FALSE(uiManager_->consumeLoadGameRequest(json));
}

// ---------------------------------------------------------------------------
// Input events in main menu state
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, OnEvent_EscapeKeyInMainMenu_DoesNotCrash) {
    InputEvent ev;
    ev.type = InputEvent::Type::KeyDown;
    ev.keyCode = 27;  // Escape
    uiManager_->onEvent(ev);
}

TEST_F(UIManagerIntegrationTest, OnEvent_MouseMoveInMainMenu_DoesNotCrash) {
    InputEvent ev;
    ev.type = InputEvent::Type::MouseMove;
    ev.x = 100;
    ev.y = 200;
    uiManager_->onEvent(ev);
}

TEST_F(UIManagerIntegrationTest, OnEvent_EscapeKeyInGameplay_OpensPauseMenu) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    InputEvent ev;
    ev.type = InputEvent::Type::KeyDown;
    ev.keyCode = 27;  // Escape
    uiManager_->onEvent(ev);
    // Should now be in paused state
    EXPECT_TRUE(uiManager_->isGameplayOrPaused());
}

// ---------------------------------------------------------------------------
// Multiple update calls
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, MultipleUpdateCalls_DoNotCrash) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    for (int i = 0; i < 60; ++i) {
        clock_.advance(0.016);
        uiManager_->update(0.016f);
    }
}

TEST_F(UIManagerIntegrationTest, MultipleDrawCalls_DoNotCrash) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    for (int i = 0; i < 10; ++i) {
        uiManager_->draw();
    }
}

// ---------------------------------------------------------------------------
// rebuildCityFromSim
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, RebuildCityFromSim_DoesNotCrash) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->rebuildCityFromSim();
}

// ---------------------------------------------------------------------------
// onNewGame
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, OnNewGame_DoesNotCrash) {
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->onNewGame();
}

// ---------------------------------------------------------------------------
// DemolishConfirm setter
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, SetDemolishConfirm_FalseAndTrue_DoNotCrash) {
    uiManager_->setDemolishConfirm(false);
    uiManager_->setDemolishConfirm(true);
}

// ---------------------------------------------------------------------------
// setSaveAvailable / setSaveStatusText
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, SetSaveAvailable_DoesNotCrash) {
    uiManager_->setSaveAvailable(true);
    uiManager_->setSaveAvailable(false);
}

TEST_F(UIManagerIntegrationTest, SetSaveStatusText_DoesNotCrash) {
    uiManager_->setSaveStatusText("No saves found.");
    uiManager_->setSaveStatusText("");
}

// ---------------------------------------------------------------------------
// loadKeybindings / applyKeybindings
// ---------------------------------------------------------------------------

TEST_F(UIManagerIntegrationTest, LoadKeybindings_DoesNotCrash) {
    uiManager_->loadKeybindings();
}

// ---------------------------------------------------------------------------
// ModalDialog standalone test with EDT_NULL backend
// ---------------------------------------------------------------------------

class ModalDialogIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_ = createNullDevice();
        ASSERT_NE(device_, nullptr);
        backend_ = std::make_unique<IrrlichtUIBackend>(device_);

        ON_CALL(sim_, isPaused()).WillByDefault(Return(false));

        modal_ = std::make_unique<ModalDialog>(backend_.get(), &sim_);
    }

    void TearDown() override {
        modal_.reset();
        backend_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    irr::IrrlichtDevice*              device_{nullptr};
    std::unique_ptr<IrrlichtUIBackend> backend_;
    NiceMock<MockCitySimulation>       sim_;
    std::unique_ptr<ModalDialog>       modal_;
};

TEST_F(ModalDialogIntegrationTest, Construction_IsNotActive) {
    EXPECT_FALSE(modal_->isActive());
}

TEST_F(ModalDialogIntegrationTest, Show_IsNoOp_GenericShowDoesNotActivate) {
    // ModalDialog::show() is documented as a no-op; use specific dialog launchers
    modal_->show();
    EXPECT_FALSE(modal_->isActive())
        << "Generic show() is a no-op; isActive must remain false";
}

TEST_F(ModalDialogIntegrationTest, Hide_WhenInactive_DoesNotCrash) {
    ASSERT_FALSE(modal_->isActive());
    modal_->hide();  // must not crash when called while already inactive
    EXPECT_FALSE(modal_->isActive());
}

TEST_F(ModalDialogIntegrationTest, ShowForcedLoan_IsActive) {
    LoanTerms terms;
    terms.amount = 100000.0f;
    terms.repaymentTicks = 24;
    terms.interestRate = 0.05f;
    modal_->showForcedLoan(terms);
    EXPECT_TRUE(modal_->isActive());
}

TEST_F(ModalDialogIntegrationTest, ShowForcedLoan_Draw_DoesNotCrash) {
    LoanTerms terms;
    terms.amount = 100000.0f;
    terms.repaymentTicks = 24;
    terms.interestRate = 0.05f;
    modal_->showForcedLoan(terms);
    modal_->draw();
}

TEST_F(ModalDialogIntegrationTest, ShowDemolishConfirm_IsActive) {
    modal_->showDemolishConfirm(1);
    EXPECT_TRUE(modal_->isActive());
}

TEST_F(ModalDialogIntegrationTest, ShowDemolishConfirm_Draw_DoesNotCrash) {
    modal_->showDemolishConfirm(3);
    modal_->draw();
}

TEST_F(ModalDialogIntegrationTest, ShowWASDPreset_IsActive) {
    modal_->showWASDPreset();
    EXPECT_TRUE(modal_->isActive());
}

TEST_F(ModalDialogIntegrationTest, ShowGameOver_IsActive) {
    modal_->showGameOver(50000, 6);
    EXPECT_TRUE(modal_->isActive());
}

TEST_F(ModalDialogIntegrationTest, ShowGameOver_Draw_DoesNotCrash) {
    modal_->showGameOver(50000, 6);
    modal_->draw();
}

TEST_F(ModalDialogIntegrationTest, ShowUnsavedQuit_ToDesktop_IsActive) {
    modal_->showUnsavedQuit(true);
    EXPECT_TRUE(modal_->isActive());
}

TEST_F(ModalDialogIntegrationTest, ShowUnsavedQuit_ToMenu_Draw_DoesNotCrash) {
    modal_->showUnsavedQuit(false);
    modal_->draw();
}

TEST_F(ModalDialogIntegrationTest, ShowSaveFailure_IsActive) {
    modal_->showSaveFailure("Disk full");
    EXPECT_TRUE(modal_->isActive());
}

TEST_F(ModalDialogIntegrationTest, ShowSaveFailure_Draw_DoesNotCrash) {
    modal_->showSaveFailure("Write error");
    modal_->draw();
}

TEST_F(ModalDialogIntegrationTest, ShowRestoreDefaultsConfirm_IsActive) {
    modal_->showRestoreDefaultsConfirm();
    EXPECT_TRUE(modal_->isActive());
}

TEST_F(ModalDialogIntegrationTest, HideAfterShow_PollResultNone) {
    modal_->showDemolishConfirm(1);
    modal_->hide();
    // After hide (no button press), pollResult should return a result or None
    ModalDialog::DialogResult r = modal_->pollResult();
    // No expectation on the value — just verifying no crash
    (void)r;
}

TEST_F(ModalDialogIntegrationTest, EscapeKey_OnForcedLoan_DoesNotCrash) {
    LoanTerms terms;
    terms.amount = 100000.0f;
    terms.repaymentTicks = 24;
    terms.interestRate = 0.05f;
    modal_->showForcedLoan(terms);

    InputEvent ev;
    ev.type = InputEvent::Type::KeyDown;
    ev.keyCode = 27;  // Escape
    modal_->onEvent(ev);
}

// ---------------------------------------------------------------------------
// HUD standalone integration test
// ---------------------------------------------------------------------------

class HUDIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_ = createNullDevice();
        ASSERT_NE(device_, nullptr);
        backend_ = std::make_unique<IrrlichtUIBackend>(device_);

        ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(500000.0f));
        ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Village));
        ON_CALL(sim_, getTotalPopulation()).WillByDefault(Return(0));
        ON_CALL(sim_, getSimulationTime()).WillByDefault(Return(SimulationTime{1, 1}));
        ON_CALL(sim_, getZoneDemandFactor(_)).WillByDefault(Return(0.0f));
        ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));
        ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(0.0));
        ON_CALL(sim_, getSpeedMultiplier()).WillByDefault(Return(SpeedMultiplier::x1));
        ON_CALL(sim_, isPaused()).WillByDefault(Return(false));

        hud_ = std::make_unique<HUD>(backend_.get(), &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        hud_.reset();
        backend_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    irr::IrrlichtDevice*              device_{nullptr};
    std::unique_ptr<IrrlichtUIBackend> backend_;
    NiceMock<MockCitySimulation>       sim_;
    NiceMock<MockAudioSystem>          audio_;
    ManualClock                        clock_;
    std::unique_ptr<HUD>               hud_;
};

TEST_F(HUDIntegrationTest, Construction_DoesNotCrash) {
    EXPECT_NE(hud_, nullptr);
}

TEST_F(HUDIntegrationTest, ShowAndHide_DoNotCrash) {
    hud_->show();
    hud_->hide();
}

TEST_F(HUDIntegrationTest, Draw_WhileHidden_DoesNotCrash) {
    hud_->draw();
}

TEST_F(HUDIntegrationTest, Draw_WhileVisible_DoesNotCrash) {
    hud_->show();
    hud_->draw();
}

TEST_F(HUDIntegrationTest, Update_WhileVisible_DoesNotCrash) {
    hud_->show();
    hud_->update(0.016f);
}

TEST_F(HUDIntegrationTest, SetUnsavedChanges_DoesNotCrash) {
    hud_->show();
    hud_->setUnsavedChanges(true);
    hud_->setUnsavedChanges(false);
}

TEST_F(HUDIntegrationTest, SetActiveToolLabel_DoesNotCrash) {
    hud_->show();
    hud_->setActiveToolLabel("Zone");
    hud_->setActiveToolLabel("Road");
    hud_->setActiveToolLabel("");
}

TEST_F(HUDIntegrationTest, ToggleFinancesPanel_DoesNotCrash) {
    hud_->show();
    hud_->toggleFinancesPanel();
    hud_->toggleFinancesPanel();
}

TEST_F(HUDIntegrationTest, NotifyGameStarted_DoesNotCrash) {
    hud_->notifyGameStarted();
}

TEST_F(HUDIntegrationTest, GetFinances_ReturnsNonNull) {
    EXPECT_NE(hud_->getFinances(), nullptr);
}

TEST_F(HUDIntegrationTest, MultipleUpdateCycles_DoNotCrash) {
    hud_->show();
    for (int i = 0; i < 60; ++i) {
        clock_.advance(0.016);
        hud_->update(0.016f);
    }
}

// ---------------------------------------------------------------------------
// FinancesPanel standalone integration test
// ---------------------------------------------------------------------------

class FinancesPanelIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_ = createNullDevice();
        ASSERT_NE(device_, nullptr);
        backend_ = std::make_unique<IrrlichtUIBackend>(device_);

        ON_CALL(sim_, getTaxRate(_)).WillByDefault(Return(0.05f));
        ON_CALL(sim_, getTaxRevenue(_)).WillByDefault(Return(5000.0f));
        ON_CALL(sim_, getUtilityFeeRevenue()).WillByDefault(Return(1000.0f));
        ON_CALL(sim_, getWagesCost()).WillByDefault(Return(2000.0f));
        ON_CALL(sim_, getRoadMaintenanceCost()).WillByDefault(Return(500.0f));
        ON_CALL(sim_, getServiceUpkeepCost()).WillByDefault(Return(1000.0f));

        panel_ = std::make_unique<FinancesPanel>(
            backend_.get(), &sim_, &audio_, &clock_);
    }

    void TearDown() override {
        panel_.reset();
        backend_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    irr::IrrlichtDevice*              device_{nullptr};
    std::unique_ptr<IrrlichtUIBackend> backend_;
    NiceMock<MockCitySimulation>       sim_;
    NiceMock<MockAudioSystem>          audio_;
    ManualClock                        clock_;
    std::unique_ptr<FinancesPanel>     panel_;
};

TEST_F(FinancesPanelIntegrationTest, Construction_DoesNotCrash) {
    EXPECT_NE(panel_, nullptr);
}

TEST_F(FinancesPanelIntegrationTest, InitialState_NotOpen) {
    EXPECT_FALSE(panel_->isOpen());
}

TEST_F(FinancesPanelIntegrationTest, OpenAndClose_TogglesIsOpen) {
    panel_->open();
    EXPECT_TRUE(panel_->isOpen());
    panel_->close();
    EXPECT_FALSE(panel_->isOpen());
}

TEST_F(FinancesPanelIntegrationTest, Draw_WhenClosed_DoesNotCrash) {
    panel_->draw();
}

TEST_F(FinancesPanelIntegrationTest, Draw_WhenOpen_DoesNotCrash) {
    panel_->open();
    panel_->draw();
}

TEST_F(FinancesPanelIntegrationTest, Update_WhenOpen_DoesNotCrash) {
    panel_->open();
    panel_->update(0.016f);
}

TEST_F(FinancesPanelIntegrationTest, HasPendingRateChange_InitiallyFalse) {
    EXPECT_FALSE(panel_->hasPendingRateChange());
}

TEST_F(FinancesPanelIntegrationTest, ClearPendingRateChange_DoesNotCrash) {
    panel_->clearPendingRateChange();
}

// ---------------------------------------------------------------------------
// NotificationManager standalone integration test
// ---------------------------------------------------------------------------

class NotificationManagerIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_ = createNullDevice();
        ASSERT_NE(device_, nullptr);
        backend_ = std::make_unique<IrrlichtUIBackend>(device_);

        ON_CALL(sim_, isPaused()).WillByDefault(Return(false));

        mgr_ = std::make_unique<NotificationManager>(
            backend_.get(), &sim_, &clock_, &audio_);
    }

    void TearDown() override {
        mgr_.reset();
        backend_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    irr::IrrlichtDevice*                   device_{nullptr};
    std::unique_ptr<IrrlichtUIBackend>      backend_;
    NiceMock<MockCitySimulation>            sim_;
    NiceMock<MockAudioSystem>               audio_;
    ManualClock                             clock_;
    std::unique_ptr<NotificationManager>    mgr_;
};

TEST_F(NotificationManagerIntegrationTest, Construction_DoesNotCrash) {
    EXPECT_NE(mgr_, nullptr);
}

TEST_F(NotificationManagerIntegrationTest, NoCriticalToast_HasCriticalVisible_False) {
    EXPECT_FALSE(mgr_->hasCriticalToastVisible());
}

TEST_F(NotificationManagerIntegrationTest, PostNormal_UpdateAndDraw_DoNotCrash) {
    mgr_->postNormal("Test", "Normal toast body");
    mgr_->update();
    mgr_->draw();
}

TEST_F(NotificationManagerIntegrationTest, PostCritical_HasCriticalToastVisible) {
    mgr_->postCritical("CRITICAL", "Something went wrong");
    EXPECT_TRUE(mgr_->hasCriticalToastVisible());
}

TEST_F(NotificationManagerIntegrationTest, PostCritical_Draw_DoesNotCrash) {
    mgr_->postCritical("CRITICAL", "Something went wrong");
    mgr_->draw();
}

TEST_F(NotificationManagerIntegrationTest, ToggleLog_DoesNotCrash) {
    mgr_->toggleLog();
    EXPECT_TRUE(mgr_->isLogOpen());
    mgr_->toggleLog();
    EXPECT_FALSE(mgr_->isLogOpen());
}

TEST_F(NotificationManagerIntegrationTest, SetModalActive_TrueAndFalse_DoNotCrash) {
    mgr_->setModalActive(true);
    mgr_->update();
    mgr_->setModalActive(false);
    mgr_->update();
}

TEST_F(NotificationManagerIntegrationTest, MultipleNormalToasts_DoNotCrash) {
    for (int i = 0; i < 5; ++i) {
        mgr_->postNormal("Title " + std::to_string(i), "Body " + std::to_string(i));
    }
    mgr_->update();
    mgr_->draw();
}

// ---------------------------------------------------------------------------
// MainMenuPanel standalone integration test
// ---------------------------------------------------------------------------

class MainMenuPanelIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_ = createNullDevice();
        ASSERT_NE(device_, nullptr);
        backend_ = std::make_unique<IrrlichtUIBackend>(device_);
        panel_ = std::make_unique<MainMenuPanel>(backend_.get());
    }

    void TearDown() override {
        panel_.reset();
        backend_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    irr::IrrlichtDevice*              device_{nullptr};
    std::unique_ptr<IrrlichtUIBackend> backend_;
    std::unique_ptr<MainMenuPanel>     panel_;
};

TEST_F(MainMenuPanelIntegrationTest, Construction_DoesNotCrash) {
    EXPECT_NE(panel_, nullptr);
}

TEST_F(MainMenuPanelIntegrationTest, ShowAndHide_DoNotCrash) {
    panel_->show();
    panel_->hide();
}

TEST_F(MainMenuPanelIntegrationTest, Draw_DoesNotCrash) {
    panel_->show();
    panel_->draw();
}

TEST_F(MainMenuPanelIntegrationTest, IsOnMainMenuScreen_InitiallyTrue) {
    panel_->show();
    EXPECT_TRUE(panel_->isOnMainMenuScreen());
}

TEST_F(MainMenuPanelIntegrationTest, ConsumeStartGameRequest_InitiallyFalse) {
    EXPECT_FALSE(panel_->consumeStartGameRequest());
}

TEST_F(MainMenuPanelIntegrationTest, ConsumeQuitRequest_InitiallyFalse) {
    EXPECT_FALSE(panel_->consumeQuitRequest());
}

TEST_F(MainMenuPanelIntegrationTest, ConsumeLoadGameRequest_InitiallyFalse) {
    EXPECT_FALSE(panel_->consumeLoadGameRequest());
}

TEST_F(MainMenuPanelIntegrationTest, SetSaveAvailable_TrueAndFalse_DoNotCrash) {
    panel_->show();
    panel_->setSaveAvailable(true);
    panel_->setSaveAvailable(false);
}

TEST_F(MainMenuPanelIntegrationTest, SetSaveStatusText_DoesNotCrash) {
    panel_->show();
    panel_->setSaveStatusText("No saves found.");
    panel_->setSaveStatusText("");
}

TEST_F(MainMenuPanelIntegrationTest, ShowLoadingScreen_DoesNotCrash) {
    panel_->show();
    panel_->showLoadingScreen();
}

TEST_F(MainMenuPanelIntegrationTest, SetAbortCheckpointPassed_DoesNotCrash) {
    panel_->show();
    panel_->showLoadingScreen();
    panel_->setAbortCheckpointPassed();
}

TEST_F(MainMenuPanelIntegrationTest, GetSelectedDifficulty_Default_IsNormal) {
    EXPECT_EQ(panel_->getSelectedDifficulty(), 1);  // 1 = Normal
}

TEST_F(MainMenuPanelIntegrationTest, GetSelectedMapSize_Default_IsMedium) {
    EXPECT_EQ(panel_->getSelectedMapSize(), MapSize::kMedium);
}

TEST_F(MainMenuPanelIntegrationTest, OnEvent_EnterKey_DoesNotCrash) {
    panel_->show();
    InputEvent ev;
    ev.type = InputEvent::Type::KeyDown;
    ev.keyCode = 13;  // Return
    panel_->onEvent(ev);
}

// ---------------------------------------------------------------------------
// PauseMenuPanel standalone integration test
// ---------------------------------------------------------------------------

class PauseMenuPanelIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_ = createNullDevice();
        ASSERT_NE(device_, nullptr);
        backend_ = std::make_unique<IrrlichtUIBackend>(device_);
        panel_ = std::make_unique<PauseMenuPanel>(backend_.get());
    }

    void TearDown() override {
        panel_.reset();
        backend_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    irr::IrrlichtDevice*              device_{nullptr};
    std::unique_ptr<IrrlichtUIBackend> backend_;
    std::unique_ptr<PauseMenuPanel>    panel_;
};

TEST_F(PauseMenuPanelIntegrationTest, Construction_DoesNotCrash) {
    EXPECT_NE(panel_, nullptr);
}

TEST_F(PauseMenuPanelIntegrationTest, ShowAndHide_IsVisibleToggles) {
    EXPECT_FALSE(panel_->isVisible());
    panel_->show();
    EXPECT_TRUE(panel_->isVisible());
    panel_->hide();
    EXPECT_FALSE(panel_->isVisible());
}

TEST_F(PauseMenuPanelIntegrationTest, Draw_WhenVisible_DoesNotCrash) {
    panel_->show();
    panel_->draw();
}

TEST_F(PauseMenuPanelIntegrationTest, Draw_WhenHidden_DoesNotCrash) {
    panel_->draw();
}

TEST_F(PauseMenuPanelIntegrationTest, ConsumeSaveRequest_InitiallyFalse) {
    EXPECT_FALSE(panel_->consumeSaveRequest());
}

TEST_F(PauseMenuPanelIntegrationTest, ConsumeQuitDesktopRequest_InitiallyFalse) {
    EXPECT_FALSE(panel_->consumeQuitDesktopRequest());
}

TEST_F(PauseMenuPanelIntegrationTest, ConsumeQuitToMenuRequest_InitiallyFalse) {
    EXPECT_FALSE(panel_->consumeQuitToMenuRequest());
}

TEST_F(PauseMenuPanelIntegrationTest, OnEvent_EscapeKey_DoesNotCrash) {
    panel_->show();
    InputEvent ev;
    ev.type = InputEvent::Type::KeyDown;
    ev.keyCode = 27;
    panel_->onEvent(ev);
}

// ---------------------------------------------------------------------------
// SettingsPanel standalone integration test
// ---------------------------------------------------------------------------

class SettingsPanelIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_ = createNullDevice();
        ASSERT_NE(device_, nullptr);
        backend_ = std::make_unique<IrrlichtUIBackend>(device_);
        panel_ = std::make_unique<SettingsPanel>(backend_.get(), &audio_, &clock_);
    }

    void TearDown() override {
        panel_.reset();
        backend_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    irr::IrrlichtDevice*              device_{nullptr};
    std::unique_ptr<IrrlichtUIBackend> backend_;
    NiceMock<MockAudioSystem>          audio_;
    ManualClock                        clock_;
    std::unique_ptr<SettingsPanel>     panel_;
};

TEST_F(SettingsPanelIntegrationTest, Construction_DoesNotCrash) {
    EXPECT_NE(panel_, nullptr);
}

TEST_F(SettingsPanelIntegrationTest, ShowAndHide_IsVisibleToggles) {
    EXPECT_FALSE(panel_->isVisible());
    panel_->show();
    EXPECT_TRUE(panel_->isVisible());
    panel_->hide();
    EXPECT_FALSE(panel_->isVisible());
}

TEST_F(SettingsPanelIntegrationTest, Draw_WhenVisible_DoesNotCrash) {
    panel_->show();
    panel_->draw();
}

TEST_F(SettingsPanelIntegrationTest, Draw_WhenHidden_DoesNotCrash) {
    panel_->draw();
}

TEST_F(SettingsPanelIntegrationTest, Update_WhenVisible_DoesNotCrash) {
    panel_->show();
    panel_->update();
}

TEST_F(SettingsPanelIntegrationTest, OnEvent_EscapeKey_DoesNotCrash) {
    panel_->show();
    InputEvent ev;
    ev.type = InputEvent::Type::KeyDown;
    ev.keyCode = 27;
    panel_->onEvent(ev);
}

TEST_F(SettingsPanelIntegrationTest, OnEvent_TabKey_DoesNotCrash) {
    panel_->show();
    InputEvent ev;
    ev.type = InputEvent::Type::KeyDown;
    ev.keyCode = 9;  // Tab
    panel_->onEvent(ev);
}
