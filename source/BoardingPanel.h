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

#include "Panel.h"

#include "BoardingCombat.h"
#include "CaptureOdds.h"
#include "Crew.h"
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
	BoardingPanel(
		PlayerInfo &player,
		std::shared_ptr<Ship> &boarder,
		std::shared_ptr<Ship> &target
	);

	virtual void Draw() override;


protected:
	// Overrides from Panel.
	virtual bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override;
	virtual bool Click(int x, int y, int clicks) override;
	virtual bool Drag(double dx, double dy) override;
	virtual bool Scroll(double dx, double dy) override;


private:
	bool CanAttack() const;
	bool CanCapture() const;
	bool CanDefend() const;
	bool CanLeave() const;
	bool CanPlunderSelected() const;
	bool CanRaid() const;

	void DoKeyboardNavigation(const SDL_Keycode key);

	bool TakeTurn(Boarding::Action action, Boarding::ActionDetails details = 0);

	BoardingCombat combat;
	PlayerInfo &player;
	std::shared_ptr<Ship> boarder;
	std::shared_ptr<Ship> target;
	bool isPlayerBoarder;
	std::shared_ptr<BoardingCombat::SituationReport> report;

	int plunderIndex = 0;
	std::shared_ptr<Plunder> selectedPlunder;
	double scroll = 0.;

	// These messages are shown to report activity as it occurs.
	std::vector<std::string> messages;

	// Whether or not the ship can be captured.
	bool canCapture = false;
};
