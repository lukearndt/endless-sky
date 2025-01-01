/* BoardingPanel.h
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "Crew.h"
#include "Panel.h"

#include "CaptureOdds.h"
#include "Plunder.h"

#include <memory>
#include <string>
#include <vector>

class Outfit;
class PlayerInfo;
class Ship;



// This panel is displayed whenever your flagship boards another ship, to give
// you a choice of what to plunder or whether to attempt to capture it. The
// items you can plunder are shown in a list sorted by value per ton. Ship
// capture is "turn-based" combat where each "turn" one or both ships lose crew.
class BoardingPanel : public Panel {
public:
	BoardingPanel(PlayerInfo &player, std::shared_ptr<Ship> &victim);

	virtual void Draw() override;


protected:
	// Overrides from Panel.
	virtual bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override;
	virtual bool Click(int x, int y, int clicks) override;
	virtual bool Drag(double dx, double dy) override;
	virtual bool Scroll(double dx, double dy) override;


private:
	// You can't exit this dialog if you are in the middle of combat.
	bool CanExit() const;
	// Check if you can raid the ship for valuables.
	bool CanRaid(bool alreadyCheckedPlunder = false, bool canTakeSomething = false) const;
	// Check if you can take the outfit at the given position in the list.
	bool CanTake() const;
	// Check if you can initiate hand to hand combat.
	bool CanCapture() const;
	// Check if you are in the midst of hand to hand combat.
	bool CanAttack() const;

	// Handle the keyboard scrolling and selection in the panel list.
	void DoKeyboardNavigation(const SDL_Keycode key);

	// Build a list of casualties from the boarding action and trigger any
	// consequences that result from them, such as death benefits and death shares.
	void ResolveCasualties();

private:
	PlayerInfo &player;
	std::shared_ptr<Ship> you;
	std::shared_ptr<Ship> victim;

	int selected = 0;
	double scroll = 0.;

	bool playerDied = false;
	bool isCapturing = false;
	bool isFirstCaptureAction = true;
	// Calculating the odds of combat success, and the expected casualties, is
	// non-trivial. So, cache the results for all crew amounts up to full.
	CaptureOdds attackOdds;
	CaptureOdds defenseOdds;
	// These messages are shown to report the results of hand to hand combat.
	std::vector<std::string> messages;

	// Allows us to list plunderable items and take them from the victim.
	Plunder::Session plunderSession;

	// Whether or not the ship can be captured.
	bool canCapture = false;

	// Over the course of the boaring action, you may lose crew members.
	// We need to keep track of them and enforce death benefits and death shares.
	// This is the crew manifest at the start of the boarding action.
	Crew::ShipAnalysis shipAnalysisBefore;

	// If you lose crew members during boarding and then give up, we still
	// need to trigger consequences for the lost crew members. We use this
	// flag when we close the panel to check if we need to do that.
	bool hasUnresolvedCasualties = false;
};
