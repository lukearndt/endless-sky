/* BoardingPanel.cpp
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

#include "BoardingPanel.h"

#include "text/alignment.hpp"
#include "CargoHold.h"
#include "Crew.h"
#include "Depreciation.h"
#include "Dialog.h"
#include "text/DisplayText.h"
#include "FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "text/Format.h"
#include "GameData.h"
#include "Government.h"
#include "Information.h"
#include "Interface.h"
#include "Logger.h"
#include "Messages.h"
#include "PlayerInfo.h"
#include "Preferences.h"
#include "Random.h"
#include "Ship.h"
#include "ShipEvent.h"
#include "ShipInfoPanel.h"
#include "System.h"
#include "UI.h"

#include <algorithm>
#include <utility>

using namespace std;

namespace {
	// Format the given double with one decimal place.
	string Round(double value)
	{
		int integer = round(value * 10.);
		string result = to_string(integer / 10);
		result += ".0";
		result.back() += integer % 10;

		return result;
	}
}



// Constructor.
BoardingPanel::BoardingPanel(
	PlayerInfo &player,
	shared_ptr<Ship> &boarder,
	shared_ptr<Ship> &target
) :
	combat(BoardingCombat(player, boarder, target)),
	player(player),
	boarder(boarder),
	target(target),
	isPlayerBoarder(boarder->IsYours()),
	report(
		isPlayerBoarder
			? combat.GetHistory()->back()->boarderSituationReport
			: combat.GetHistory()->back()->targetSituationReport
	)
{
	// The escape key should close this panel rather than bringing up the main menu.
	SetInterruptible(false);

	canCapture = target->IsCapturable() || player.CaptureOverriden(target);
	// Some "ships" do not represent something the player could actually pilot.
	if(!canCapture)
		messages.emplace_back("This is not a ship that you can capture.");
}



// Draw the panel.
void BoardingPanel::Draw()
{
	// Draw a translucent black scrim over everything beneath this panel.
	DrawBackdrop();

	// Draw the list of plunder.
	const Color &opaque = *GameData::Colors().Get("panel background");
	const Color &back = *GameData::Colors().Get("faint");
	const Color &dim = *GameData::Colors().Get("dim");
	const Color &dimmer = *GameData::Colors().Get("dimmer");
	const Color &medium = *GameData::Colors().Get("medium");
	const Color &bright = *GameData::Colors().Get("bright");
	FillShader::Fill(Point(-155., -60.), Point(360., 250.), opaque);

	int index = (scroll - 10) / 20;
	int y = -170 - scroll + 20 * index;
	int endY = 60;

	const Font &font = FontSet::Get(14);
	// Y offset to center the text in a 20-pixel high row.
	double fontOff = .5 * (20 - font.Height());
	// TODO: Placeholder until the boarding UI png is updated.
	// bool canTakeSomething = false;


	shared_ptr<Ship> playerShip = report->ship;

	for( ; y < endY && static_cast<unsigned>(index) < report->plunderOptions.size(); y += 20, ++index)
	{
		const Plunder &item = *report->plunderOptions.at(index);

		// Check if this is the selected row.
		bool isSelected = (index == plunderIndex);
		if(isSelected)
			FillShader::Fill(Point(-155., y + 10.), Point(360., 20.), back);

		// Color the item based on whether you have space for it.
		bool hasSpace = item.HasEnoughSpace(playerShip);
		// Also color the item based on whether or not it's accessible.
		bool isAccessible = report->isEnemyConquered || !item.RequiresConquest();

		const Color &color = hasSpace
			? isAccessible
				? isSelected ? bright : medium
				: dim
			: dimmer;
		Point pos(-320., y + fontOff);
		font.Draw(item.Name(), pos, color);
		font.Draw({item.Value(), {260, Alignment::RIGHT}}, pos, color);
		font.Draw({item.Size(), {330, Alignment::RIGHT}}, pos, color);
	}

	// Set which buttons are active.
	info.ClearConditions();
	if(CanLeave())
		info.SetCondition("can leave");
	// TODO: The boarding UI png needs to be updated to include a "raid" button.
	// This is a placeholder until that has been done.
	// if(CanRaid(true, canTakeSomething))
	// 	info.SetCondition("can raid");
	if(CanPlunderSelected())
		info.SetCondition("can take");
	if(CanCapture())
		info.SetCondition("can capture");
	if(CanAttack())
		info.SetCondition("can attack");
	if(CanAttack())
		info.SetCondition("can defend");

	info.SetString("cargo space", to_string(report->cargoSpace));
	info.SetString("your crew", to_string(report->crew));
	info.SetString("your attack", Round(report->attackPower));
	info.SetString("your defense", Round(report->defensePower));
	info.SetString("enemy crew", to_string(report->enemyCrew));
	info.SetString("enemy attack", Round(report->enemyAttackPower));
	info.SetString("enemy defense", Round(report->enemyDefensePower));

	if(!report->isEnemyConquered)
	{
		info.SetString("attack odds", Round(100. * report->invasionVictoryProbability) + "%");
		info.SetString("attack casualties", Round(report->expectedInvasionCasualties));
		info.SetString("defense odds", Round(100. * report->defensiveVictoryProbability) + "%");
		info.SetString("defense casualties", Round(report->expectedDefensiveCasualties));
	}

	const Interface *boarding = GameData::Interfaces().Get("boarding");
	boarding->Draw(info, this);

	// Draw the status messages from hand to hand combat.
	Point messagePos(50., 55.);
	for(const string &message : messages)
	{
		font.Draw(message, messagePos, bright);
		messagePos.Y() += 20.;
	}
}



// Handle key presses or button clicks that were mapped to key presses.
bool BoardingPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if((key == 'l' || key == 'x' || key == SDLK_ESCAPE || (key == 'w' && (mod & (KMOD_CTRL | KMOD_GUI)))) && CanLeave())
	{
		if(TakeTurn(Boarding::Action::Objective::Leave))
			GetUI()->Pop(this);
	}
	else if(key == 't' && CanPlunderSelected())
	{
		int quantity = KMOD_SHIFT ? 1 : selectedPlunder->Count();
		return TakeTurn(Boarding::Action::Objective::Plunder, make_tuple(plunderIndex, quantity));
	}
	else if(key == 'r' && CanRaid())
		return TakeTurn(Boarding::Action::Objective::Plunder, false);
	else if(key == SDLK_UP || key == SDLK_DOWN || key == SDLK_PAGEUP
			|| key == SDLK_PAGEDOWN || key == SDLK_HOME || key == SDLK_END)
		DoKeyboardNavigation(key);
	else if(key == 'c' && CanCapture())
	{
		return TakeTurn(Boarding::Action::Objective::Capture);
		// A ship that self-destructs checks once when you board it, and again
		// when you try to capture it, to see if it will self-destruct. This is
		// so that capturing will be harder than plundering.
		// if(Random::Real() < target->Attributes().Get("self destruct"))
		// {
		// 	target->SelfDestruct();
		// 	GetUI()->Pop(this);
		// 	GetUI()->Push(new Dialog("The moment you blast through the airlock, a series of explosions rocks the enemy ship."
		// 		" They appear to have set off their self-destruct sequence..."));
		// 	return true;
		// }
		// isCapturing = true;
		// messages.push_back("The airlock blasts open. Combat has begun!");
		// messages.push_back("(It will end if you both choose to \"defend.\")");
	}
	else if(key == 'a' && CanAttack())
		return TakeTurn(Boarding::Action::Objective::Attack);
	else if(key == 'd' && CanDefend())
		return TakeTurn(Boarding::Action::Objective::Defend);
	// else if((key == 'a' || key == 'd') && CanAttack())
	// {
	// 	int yourStartCrew = you->Crew();
	// 	int enemyStartCrew = target->Crew();

	// 	// Figure out what action the other ship will take. As a special case,
	// 	// if you board them but immediately "defend" they will let you return
	// 	// to your ship in peace. That is to allow the player to "cancel" if
	// 	// they did not really mean to try to capture the ship.
	// 	bool youAttack = (key == 'a' && (yourStartCrew > 1 || !target->RequiredCrew()));
	// 	bool enemyAttacks = defenseOdds.Odds(enemyStartCrew, yourStartCrew) > .5;
	// 	if(isFirstCaptureAction && !youAttack)
	// 		enemyAttacks = false;
	// 	isFirstCaptureAction = false;

	// 	// If neither side attacks, combat ends.
	// 	if(!youAttack && !enemyAttacks)
	// 	{
	// 		messages.push_back("You retreat to your ships. Combat ends.");
	// 		isCapturing = false;
	// 	}
	// 	else
	// 	{
	// 		unsigned int yourCasualties = 0;
	// 		unsigned int enemyCasualties = 0;

	// 		// To speed things up, have multiple rounds of combat each time you
	// 		// click the button, if you started with a lot of crew.
	// 		int rounds = max(1, yourStartCrew / 5);
	// 		for(int round = 0; round < rounds; ++round)
	// 		{
	// 			int yourCrew = you->Crew();
	// 			int enemyCrew = target->Crew();
	// 			if(!yourCrew || !enemyCrew)
	// 				break;

	// 			if(youAttack)
	// 			{
	// 				// Your chance of winning this round is equal to the ratio of
	// 				// your power to the enemy's power.
	// 				double yourAttackPower = attackOdds.AttackerPower(yourCrew);
	// 				double total = yourAttackPower + attackOdds.DefenderPower(enemyCrew);

	// 				if(total)
	// 				{
	// 					if(Random::Real() * total >= yourAttackPower)
	// 					{
	// 						++yourCasualties;
	// 						hasUnresolvedCasualties = true;
	// 						you->AddCrew(-1);
	// 						if(you->Crew() <= 1)
	// 							break;
	// 					}
	// 					else
	// 					{
	// 						++enemyCasualties;
	// 						target->AddCrew(-1);
	// 						if(!target->Crew())
	// 							break;
	// 					}
	// 				}
	// 			}
	// 			if(enemyAttacks)
	// 			{
	// 				double yourDefensePower = defenseOdds.DefenderPower(yourCrew);
	// 				double total = defenseOdds.AttackerPower(enemyCrew) + yourDefensePower;

	// 				if(total)
	// 				{
	// 					if(Random::Real() * total >= yourDefensePower)
	// 					{
	// 						++yourCasualties;
	// 						hasUnresolvedCasualties = true;
	// 						you->AddCrew(-1);
	// 						if(!you->Crew())
	// 							break;
	// 					}
	// 					else
	// 					{
	// 						++enemyCasualties;
	// 						target->AddCrew(-1);
	// 						if(!target->Crew())
	// 							break;
	// 					}
	// 				}
	// 			}
	// 		}

	// 		// Report what happened and how many casualties each side suffered.
	// 		if(youAttack && enemyAttacks)
	// 			messages.push_back("You both attack. ");
	// 		else if(youAttack)
	// 			messages.push_back("You attack. ");
	// 		else if(enemyAttacks)
	// 			messages.push_back("They attack. ");

	// 		if(yourCasualties && enemyCasualties)
	// 			messages.back() += "You lose " + to_string(yourCasualties)
	// 				+ " crew; they lose " + to_string(enemyCasualties) + ".";
	// 		else if(yourCasualties)
	// 			messages.back() += "You lose " + to_string(yourCasualties) + " crew.";
	// 		else if(enemyCasualties)
	// 			messages.back() += "They lose " + to_string(enemyCasualties) + " crew.";

	// 		// Check if either ship has been captured.
	// 		if(!you->Crew())
	// 		{
	// 			messages.push_back("You have been killed. Your ship is lost.");
	// 			you->WasCaptured(target);
	// 			playerDied = true;
	// 			isCapturing = false;
	// 		}
	// 		else if(!target->Crew())
	// 		{
	// 			messages.push_back("You have succeeded in capturing this ship.");
	// 			target->GetGovernment()->Offend(ShipEvent::CAPTURE, target->CrewValue());

	// 			// We need to resolve any casualties before we transfer crew members
	// 			// to the other ship. Otherwise, we won't be able to tally them correctly.
				// ResolveCasualties();

	// 			// Transfer the crew and fuel from the captured ship to your ship.
	// 			int crewTransferred = target->WasCaptured(you);
	// 			if(crewTransferred > 0)
	// 			{
	// 				string transferMessage = Format::Number(crewTransferred) + " crew member";
	// 				if(crewTransferred == 1)
	// 					transferMessage += " has";
	// 				else
	// 					transferMessage += "s have";
	// 				transferMessage += " been transferred.";
	// 				messages.push_back(transferMessage);
	// 			}
	// 			if(!target->JumpsRemaining() && you->CanRefuel(*target))
	// 				you->TransferFuel(target->JumpFuelMissing(), &*target);
	// 			player.AddShip(target);
	// 			for(const Ship::Bay &bay : target->Bays())
	// 				if(bay.ship)
	// 				{
	// 					player.AddShip(bay.ship);
	// 					player.HandleEvent(ShipEvent(you, bay.ship, ShipEvent::CAPTURE), GetUI());
	// 				}
	// 			isCapturing = false;

	// 			// Report this ship as captured in case any missions care.
	// 			ShipEvent event(you, target, ShipEvent::CAPTURE);
	// 			player.HandleEvent(event, GetUI());
	// 		}
	// 	}
	// }
	else if(command.Has(Command::INFO))
		GetUI()->Push(new ShipInfoPanel(player));

	// Trim the list of status messages.
	while(messages.size() > 5)
		messages.erase(messages.begin());

	return true;
}



// Handle mouse clicks.
bool BoardingPanel::Click(int x, int y, int clicks)
{
	// Was the click inside the plunder list?
	if(x >= -330 && x < 20 && y >= -180 && y < 60)
	{
		int index = (scroll + y - -170) / 20;
		if(static_cast<unsigned>(index) < report->plunderOptions.size())
			plunderIndex = index;
		return true;
	}

	return true;
}



// Allow dragging of the plunder list.
bool BoardingPanel::Drag(double dx, double dy)
{
	// The list is 240 pixels tall, and there are 10 pixels padding on the top
	// and the bottom, so:
	double maximumScroll = max(0., 20. * report->plunderOptions.size() - 220.);
	scroll = max(0., min(maximumScroll, scroll - dy));

	return true;
}



// The scroll wheel can be used to scroll the plunder list.
bool BoardingPanel::Scroll(double dx, double dy)
{
	return Drag(0., dy * Preferences::ScrollSpeed());
}



/**
 * If the player is engaged in active combat, they must resolve it
 * before they can close the panel. This involves either conquering the
 * enemy ship, withdrawing from combat, or repelling enemy boarders.
 *
 * @return Whether or not the player can leave the boarding panel.
 */
bool BoardingPanel::CanLeave() const
{
	return report->validObjectives->at(Boarding::Action::Objective::Leave);
}



/**
 * @return Whether or not the player can raid the ship for valuables.
 */
bool BoardingPanel::CanRaid() const
{
	return !report->isPlunderFinished
		&& report->validObjectives->at(Boarding::Action::Objective::Plunder);
}



/**
 * @return Whether or not the player can take the Plunder action with
 * 	the currently selected plunder option.
 */
bool BoardingPanel::CanPlunderSelected() const
{
	return report->validObjectives->at(Boarding::Action::Objective::Plunder)
		&& selectedPlunder
		&& selectedPlunder->HasEnoughSpace(report->ship)
		&& (report->isEnemyConquered || !selectedPlunder->RequiresConquest());
}



/**
 * Check if the player can take the Capture action. This usually requires
 * that the enemy ship has been conquered and that the player has at
 * least one crew member to fly it.
 *
 * @return Whether or not the player can take the Capture action.
 */
bool BoardingPanel::CanCapture() const
{
	return report->validObjectives->at(Boarding::Action::Objective::Capture)
		&& (!report->enemyShip->RequiredCrew() || report->crew > 1);
}



/**
 * @return Whether or not the player can take the Attack action.
 */
bool BoardingPanel::CanAttack() const
{
	return report->validObjectives->at(Boarding::Action::Objective::Attack);
}



/**
 * @return Whether or not the player can take the Defend action.
 */
bool BoardingPanel::CanDefend() const
{
  return report->validObjectives->at(Boarding::Action::Objective::Defend);
}



// Handle the keyboard scrolling and selection in the panel list.
void BoardingPanel::DoKeyboardNavigation(const SDL_Keycode key)
{
	// Scrolling the list of plunder options.
	if(key == SDLK_PAGEUP || key == SDLK_PAGEDOWN)
		// Keep one of the previous items onscreen while paging through.
		plunderIndex += 10 * ((key == SDLK_PAGEDOWN) - (key == SDLK_PAGEUP));
	else if(key == SDLK_HOME)
		plunderIndex = 0;
	else if(key == SDLK_END)
		plunderIndex = static_cast<int>(report->plunderOptions.size() - 1);
	else
	{
		if(key == SDLK_UP)
			--plunderIndex;
		else if(key == SDLK_DOWN)
			++plunderIndex;
	}
	plunderIndex = max(0, min(static_cast<int>(report->plunderOptions.size() - 1), plunderIndex));

	// Scroll down at least far enough to view the current item.
	double minimumScroll = max(0., 20. * plunderIndex - 200.);
	double maximumScroll = 20. * plunderIndex;
	scroll = max(minimumScroll, min(maximumScroll, scroll));
}



/**
 * Attempt to take a turn in the boarding combat and assign a new
 * BoardingCombat::SituationReport to the panel's report variable.
 *
 * Logs an error if the turn could not be created, such as when a player
 * somehow attempts an invalid activity. This should not happen in
 * practice, and indicates a problem with the panel. If you see this
 * error, check the KeyDown() function to make sure that the key press
 * validates the objective and details before proceeding.
 *
 * @param objective The objective that the player is attempting to achieve.
 * @param details The details of that objective, if any.
 *
 * @return Whether or not a new turn was successfully created.
 */
bool BoardingPanel::TakeTurn(
	Boarding::Action::Objective objective,
	Boarding::Action::Details details
)
{
	try {
		shared_ptr<BoardingCombat::Turn> turn = combat.Step(
			Boarding::Action::Activity(objective, details)
		);

		report = isPlayerBoarder
			? turn->boarderSituationReport
			: turn->targetSituationReport;

		for(auto message : turn->messages)
			messages.push_back(message);
		return true;

	} catch(const exception &e) {
		Logger::LogError("BoardingPanel::TakeTurn - the next turn could not be created. This indicates a bug, most likely in the KeyDown() hanlder." + string(e.what()));
		return false;
	}
}
