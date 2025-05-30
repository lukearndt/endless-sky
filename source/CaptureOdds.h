/* CaptureOdds.h
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

#include <vector>

class Ship;



/**
 * This class stores the odds that one ship will be able to conquer another,
 * and can report the odds for any number of crew up to the number that each
 * ship starts out with; the odds change each time a crew member is lost.
 *
 * During each round of combat, a number of rolls will occur depending on
 * the overall population of the ships and the boarding combat action
 * chosen by each combatant. Each roll is based on the ratio of power
 * between the combatants in carrying out their resective actions.
 *
 * The winner of each roll makes progress toward their chosen action's
 * goal. For the Attack and Defense actions, this means killing one of
 * the enemy ship's crew members. Successful Plunder actions allow the
 * combatant take plunderable items from their enemy's ship. Winning a
 * roll for the SelfDestruct action brings the ship closer to exploding.
 *
 * Each crew members will also make use of up to one boarding-related
 * outfit installed on their ship, such as a hand-to-hand weapon or
 * defensive implacement. Each outfit can only be used by one crew member.
 * More powerful outfits are chosen first, in order to gain the highest
 * possible power for their current action. Defending also grants each
 * crew member a +1 power bonus.
 */
class CaptureOdds {
public:
	// Calculate odds that the first given ship can conquer the second, assuming
	// the first ship always attacks and the second one always defends.
	CaptureOdds(const Ship &attacker, const Ship &defender);

	// Get the odds of the attacker winning if the two ships have the given
	// number of crew members remaining.
	double Odds(int attackingCrew, int defendingCrew) const;
	// Get the expected number of casualties in the remainder of the battle if
	// the two ships have the given number of crew remaining.
	double AttackerCasualties(int attackingCrew, int defendingCrew) const;
	double DefenderCasualties(int attackingCrew, int defendingCrew) const;

	// Get the total power (inherent crew power plus bonuses from hand to hand
	// weapons) for each ship when they have the given number of crew remaining.
	double AttackerPower(int attackingCrew) const;
	double DefenderPower(int defendingCrew) const;


private:
	// Generate the lookup table.
	void Calculate();
	// Map crew numbers into an index in the lookup table.
	int Index(int attackingCrew, int defendingCrew) const;

	// Calculate attack or defense power for each number of crew members up to
	// the given ship's full complement.
	static std::vector<double> Power(const Ship &ship, bool isDefender);


private:
	// Attacker and defender power lookup tables.
	std::vector<double> powerAttacker;
	std::vector<double> powerDefender;

	// Capture odds lookup table.
	std::vector<double> captureChance;
	// Expected casualties lookup table.
	std::vector<double> casualtiesAttacker;
	std::vector<double> casualtiesDefender;
};
