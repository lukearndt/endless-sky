/* CaptureOdds.cpp
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

#include "CaptureOdds.h"

#include "Government.h"
#include "Outfit.h"
#include "Ship.h"

#include <algorithm>
#include <functional>

using namespace std;



// Constructor.
CaptureOdds::CaptureOdds(const Ship &attacker, const Ship &defender)
{
	powerAttacker = Power(attacker, false);
	powerDefender = Power(defender, true);
	Calculate();
}



// Get the odds of the attacker winning if the two ships have the given
// number of crew members remaining.
double CaptureOdds::Odds(int attackingCrew, int defendingCrew) const
{
	// If the defender has no crew remaining, odds are 100%.
	if(!defendingCrew)
		return 1.;

	// Make sure the input is within range, with the special constraint that the
	// attacker can never succeed if they don't have two crew left (one to pilot
	// each of the ships).
	int index = Index(attackingCrew, defendingCrew);
	if(attackingCrew < 2 || index < 0)
		return 0.;

	return captureChance[index];
}



// Get the expected number of casualties for the attacker in the remainder of
// the battle if the two ships have the given number of crew remaining.
double CaptureOdds::AttackerCasualties(int attackingCrew, int defendingCrew) const
{
	// If the attacker has fewer than two crew, they cannot attack. If the
	// defender has no crew, they cannot defend (so casualties will be zero).
	int index = Index(attackingCrew, defendingCrew);
	if(attackingCrew < 2 || !defendingCrew || index < 0)
		return 0.;

	return casualtiesAttacker[index];
}



// Get the expected number of casualties for the defender in the remainder of
// the battle if the two ships have the given number of crew remaining.
double CaptureOdds::DefenderCasualties(int attackingCrew, int defendingCrew) const
{
	// If the attacker has fewer than two crew, they cannot attack. If the
	// defender has no crew, they cannot defend (so casualties will be zero).
	int index = Index(attackingCrew, defendingCrew);
	if(attackingCrew < 2 || !defendingCrew || index < 0)
		return 0.;

	return casualtiesDefender[index];
}



// Get the total power (inherent crew power plus bonuses from hand to hand
// weapons) for the attacker when they have the given number of crew remaining.
double CaptureOdds::AttackerPower(int attackingCrew) const
{
	if(static_cast<unsigned>(attackingCrew - 1) >= powerAttacker.size())
		return 0.;

	return powerAttacker[attackingCrew - 1];
}



// Get the total power (inherent crew power plus bonuses from hand to hand
// weapons) for the defender when they have the given number of crew remaining.
double CaptureOdds::DefenderPower(int defendingCrew) const
{
	if(static_cast<unsigned>(defendingCrew - 1) >= powerDefender.size())
		return 0.;

	return powerDefender[defendingCrew - 1];
}



// Generate the lookup tables.
void CaptureOdds::Calculate()
{
	if(powerDefender.empty() || powerAttacker.empty())
		return;

	// The first row represents the case where the attacker has only one crew left.
	// In that case, the defending ship can never be successfully captured.
	captureChance.resize(powerDefender.size(), 0.);
	casualtiesAttacker.resize(powerDefender.size(), 0.);
	casualtiesDefender.resize(powerDefender.size(), 0.);

	unsigned index = 0;

	// Loop through each number of crew the attacker might have.
	for(unsigned a = 2; a <= powerAttacker.size(); ++a)
	{
		double attackPower = powerAttacker[a - 1];
		// Special case: odds for defender having only one person,
		// because 0 people is outside the end of the table.
		double attackerActionChance = attackPower / (attackPower + powerDefender[0]);
		double defenderActionChance = 1. - attackerActionChance;

		captureChance.push_back(attackerActionChance + defenderActionChance * captureChance[index]);

		casualtiesAttacker.push_back(defenderActionChance * (casualtiesAttacker[index] + 1.));

		casualtiesDefender.push_back(attackerActionChance + defenderActionChance * casualtiesDefender[index]);
		++index;

		// Loop through each number of crew the defender might have.
		for(unsigned defenderCrew = 1; defenderCrew < powerDefender.size(); ++defenderCrew)
		{
			//
			// This is a basic 2D dynamic program, where each value is based on
			// the odds of success and the values for one fewer crew members
			// for the defender or the attacker depending on who wins.
			attackerActionChance = attackPower / (attackPower + powerDefender[defenderCrew]);
			defenderActionChance = 1. - attackerActionChance;

			// I think this is trying to generate an aggregate probability that the attacker will defeat all the defenders
			// but I'm not sure it actually works.
			captureChance.push_back(
				attackerActionChance * captureChance.back() // one fewer defender
				 + defenderActionChance * captureChance[index] // why are these different
			);

			casualtiesAttacker.push_back(
				attackerActionChance * casualtiesAttacker.back()
				 + defenderActionChance * (casualtiesAttacker[index] + 1.)
			);

			casualtiesDefender.push_back(
				attackerActionChance * (casualtiesDefender.back() + 1.)
				 + defenderActionChance * casualtiesDefender[index]
			);

			++index;
		}
	}
}



// Map the given crew complements to an index in the lookup tables. There is no
// row in the table for 0 crew on either ship.
int CaptureOdds::Index(int attackingCrew, int defendingCrew) const
{
	if(static_cast<unsigned>(attackingCrew - 1) > powerAttacker.size())
		return -1;
	if(static_cast<unsigned>(defendingCrew - 1) > powerDefender.size())
		return -1;

	return (attackingCrew - 1) * powerDefender.size() + (defendingCrew - 1);
}



// Generate a vector with the total power of the given ship's crew when any
// number of them are left, either for attacking or for defending.
vector<double> CaptureOdds::Power(const Ship &ship, bool isDefender)
{
	vector<double> power;

	int effectiveCrewMembers = isDefender
		? ship.Crew() + static_cast<int>(ship.Attributes().Get("automated defenders"))
		: ship.Crew() + static_cast<int>(ship.Attributes().Get("automated invaders"));

	if(!effectiveCrewMembers)
		return power;

	// Check for any outfits that assist with attacking or defending:
	const string attribute = isDefender ? "boarding defense" : "boarding attack";
	const double crewPower = isDefender
		? ship.GetGovernment()->CrewDefense()
		: ship.GetGovernment()->CrewAttack();

	// Each crew member can wield one weapon. They use the most powerful ones
	// that can be wielded by the remaining crew.
	for(const auto &it : ship.Outfits())
	{
		double value = it.first->Get(attribute);
		if(value > 0. && it.second > 0)
			power.insert(power.end(), it.second, value);
	}
	// Use the best weapons first.
	sort(power.begin(), power.end(), greater<double>());

	// Resize the vector to have exactly one entry per effective crew member.
	power.resize(effectiveCrewMembers, 0.);

	// Calculate partial sums. That is, power[N - 1] should be your total crew
	// power when you have N crew left.
	power.front() += crewPower;
	for(unsigned i = 1; i < power.size(); ++i)
		power[i] += power[i - 1] + crewPower;

	return power;
}
