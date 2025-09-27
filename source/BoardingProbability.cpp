/* BoardingProbability.cpp
Copyright (c) 2025 by Luke Arndt
Some logic extracted from CaptureOdds.h, copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "BoardingProbability.h"

#include "Boarding.h"
#include "Government.h"
#include "Logger.h"
#include "Outfit.h"
#include "Ship.h"

#include <algorithm>
#include <functional>
#include <stdexcept>

using namespace std;



// Constructor.
BoardingProbability::BoardingProbability(
	const shared_ptr<Ship> &boarder,
	const shared_ptr<Ship> &target
) :
	boarder(boarder),
	target(target),

	initialBoarderInvaders(boarder->Invaders()),
	initialBoarderDefenders(boarder->Defenders()),
	initialTargetInvaders(target->Invaders()),
	initialTargetDefenders(target->Defenders()),

	isDefensiveAttack(
		Boarding::Action::IsObjectiveDefensive(Boarding::Action::Objective::Attack)
	),
	isDefensiveDefend(
		Boarding::Action::IsObjectiveDefensive(Boarding::Action::Objective::Defend)
	),
	isDefensiveSelfDestruct(
		Boarding::Action::IsObjectiveDefensive(Boarding::Action::Objective::SelfDestruct)
	),

	boarderInvaderPower(EffectiveCrewPower(boarder, isDefensiveAttack)),
	boarderDefenderPower(EffectiveCrewPower(boarder, isDefensiveDefend)),
	targetInvaderPower(EffectiveCrewPower(target, isDefensiveAttack)),
	targetDefenderPower(EffectiveCrewPower(target, isDefensiveDefend)),

	totalBoarderAttackPower(TotalPower(boarder, boarderInvaderPower, isDefensiveAttack)),
	totalBoarderDefensePower(TotalPower(boarder, boarderDefenderPower, isDefensiveDefend)),
	totalTargetAttackPower(TotalPower(target, targetInvaderPower, isDefensiveAttack)),
	totalTargetDefensePower(TotalPower(target, targetDefenderPower, isDefensiveDefend)),

	boarderInvadesTargetDefends(PrepareStrategy(true, false)),
	boarderInvadesTargetSelfDestructs(PrepareStrategy())
	targetInvadesBoarderDefends(PrepareStrategy(false, true)),

	bothAttack(PrepareStrategy(true, true))
{}



/**
 * @return A BoardingProbability::Report struct with information about
 * 	the odds of the conflict in its current state.
 */
BoardingProbability::Report BoardingProbability::GetReport() const
{
	unsigned boarderInvaders = boarder->Invaders();
	unsigned boarderDefenders = boarder->Defenders();
	unsigned targetInvaders = target->Invaders();
	unsigned targetDefenders = target->Defenders();

	return Report{
		Forces{
			boarderInvaders,
			boarderDefenders,
			totalBoarderAttackPower[boarderInvaders],
			totalBoarderDefensePower[boarderDefenders]
		}, // boarderForces

		Forces{
			targetInvaders,
			targetDefenders,
			totalTargetAttackPower[targetInvaders],
			totalTargetDefensePower[targetDefenders]
		}, // targetForces

		boarderInvadesTargetDefends[
			ScenarioIndex(boarderInvaders, targetDefenders, false)
		], // boarderInvadesTargetDefends
		targetInvadesBoarderDefends[
			ScenarioIndex(boarderDefenders, targetInvaders, true)
		], // targetInvadesBoarderDefends
		bothAttack[
			ScenarioIndex(boarderInvaders, targetInvaders, true)
		] // bothAttack
	};
}



/**
 * Map the given crew complements to an index in the lookup tables that
 * predict overall outcomes for the boarding combat. The index is based
 * on the number of crew members on each ship, and the strategy that the
 * combat is following (whether or not each ship is attacking).
 *
 * Each combination produces an index that is unique for the given
 * strategy, but not unique across different attack/defense strategies.
 *
 * This indexing scheme allows us to use a one-dimensional vector to
 * store all of the possible combinations of boarderCrew and targetCrew
 * counts instead of having to use a two-dimensional vector structure.
 *
 * The index's stride (number of conceptual columns per row) is equal to
 * the maximum possible value of targetCrew, which depends on whether
 * the target is attacking or defending.
 *
 * @param boarderCrew The number of crew members on the boarder's ship.
 * @param targetCrew The number of crew members on the target's ship.
 * 	ship at the start of the boarding combat.
 * @param targetAttacks Whether or not the target is attacking.
 *
 * @return An index that can be used to locate the Scenario in a
 * 	Scenarios collection that matches the given strategy.
 */
unsigned BoardingProbability::ScenarioIndex(
	unsigned boarderCrew,
	unsigned targetCrew,
	bool targetAttacks
) const
{
	return targetCrew + boarderCrew * (
		targetAttacks
			? initialTargetInvaders
			: initialBoarderInvaders
		);
}



/**
 * Builds a Scenarios collection for a boarding combat that follows
 * one of the three potential strategies:
 *
 * 1. boarderInvadesTargetDefends: The boarder attacks while the boarder defends.
 *
 * 2. targetInvadesBoarderDefends: The target attacks while the boarder defends.
 *
 * 3. bothAttack: Both the boarder and target attack each other.
 *
 * Each list is indexed by the number of crew members remaining on each
 * ship, and whether or not each ship uses an attacking strategy.
 *
 * To calculate the aggregate probabilities of a step-by-step combat,
 * we need to simulate the process of each combatant peeling away the
 * other combatant's crew one by one. This is complicated by the fact
 * that their power at any given step is based on the number of crew
 * members that each combatant has remaining.
 *
 * We achieve this by creating a dynamic probability tree. Starting
 * from each base cases where either the boarder or target has achieved
 * victory, we iterate through each state that could transition into it.
 *
 * At each step, we store an aggregate Scenario struct based on the
 * likelihood of the combat progressing into each next state and the
 * already-calculated Scenario values held by that state.
 *
 * By repeating this process using each of the potential victory states
 * as a root, we can map out the entire tree of Scenarios and make
 * a Scenario struct for each one.
 *
 * During gameplay, the boarding combat system can then look up the
 * Scenario for a given combination of remaining crew members without
 * having to recalculate the entire tree of Scenarios leading from
 * that state.
 *
 * @param boarderAttacks Whether or not the boarder is attacking.
 * @param targetAttacks Whether or not the target is attacking.
 *
 * @return A Scenarios collection containing a Scenario for each
 * 	potential combination of boarderCrew and targetCrew, specific to the
 * 	strategy (attack / defense combination) specified by the inputs.
 *
 * @throw invalid_argument If neither ship is attacking, as that would
 * 	not result in any combat. Calling the function in this manner is
 * 	therefore assumed to be a programming error.
 */
BoardingProbability::Scenarios BoardingProbability::PrepareStrategy(
	Boarding::Action::Objective boarderAttacks, // TODO-NEXT: You were adding support for self destruct scenarios, so that the logic can be pulled away from Boarding::Combatant and the calculations can use the tree probability system.
	bool targetAttacks
) const
{
	if(!boarderAttacks && !targetAttacks)
		throw invalid_argument("Invalid strategy: neither ship is attacking.");

	// Select a power vector for each combatant
	const vector<double> &boarderPower = boarderAttacks
		? totalBoarderAttackPower
		: totalBoarderDefensePower;
	const vector<double> &targetPower = targetAttacks
		? totalTargetAttackPower
		: totalTargetDefensePower;

	// Select an initial crew member count for each combatant
	unsigned initialBoarderCrew = boarderAttacks
		? initialBoarderInvaders
		: initialBoarderDefenders;
	unsigned initialTargetCrew = targetAttacks
		? initialTargetInvaders
		: initialTargetDefenders;

	// Initialise the vector result for our tree of Scenario structs,
	// so that we can use it throughout the calculation process and return
	// it at the end. Its size is 1 plus the maximum possible index value.
	vector<Scenario> result(
		1 + ScenarioIndex(
			initialBoarderCrew,
			initialTargetCrew,
			targetAttacks
		)
	);

	// First we need to store a Scenario for each possible base case,
	// which are the states where one combatant has achieved victory.
	// We do this up front so that the transitional state calculations
	// cannot reference base states before they have been defined.

	// Base cases where the boarder has achieved victory
	for (unsigned boarderCrew = 0; boarderCrew <= initialBoarderInvaders; ++boarderCrew) {
		result[ScenarioIndex(boarderCrew, 0, targetAttacks)] = Scenario{
				1.0, // boarderActionChance
				1.0, // boarderVictoryChance
				static_cast<double>(initialBoarderInvaders - boarderCrew), // boarderCasualties

				0.0, // targetActionChance
				0.0, // targetVictoryChance
				static_cast<double>(initialTargetDefenders) // targetCasualties
		};
	}

	// Base cases where the target has achieved victory
	for (unsigned targetCrew = 0; targetCrew <= initialTargetDefenders; ++targetCrew) {
		result[ScenarioIndex(0, targetCrew, targetAttacks)] = Scenario{
				0.0, // boarderActionChance
				0.0, // boarderVictoryChance
				static_cast<double>(initialBoarderInvaders), // boarderCasualties

				1.0, // targetActionChance
				1.0, // targetVictoryChance
				static_cast<double>(initialTargetDefenders - targetCrew) // targetCasualties
		};
	}

	unsigned index = 0;

	for (unsigned boarderCrew = 1; boarderCrew <= initialBoarderCrew; ++boarderCrew)
    for (unsigned targetCrew = 1; targetCrew <= initialTargetCrew; ++targetCrew)
		{
			index = ScenarioIndex(boarderCrew, targetCrew, targetAttacks);

			// Calculate the total power of both combatants
			double totalPower = boarderPower[boarderCrew] + targetPower[targetCrew];

			// Calculate the odds of who will suffer the next casualty.
			//
			// Total power should not be negative or zero unless something has
			// gone wrong, as we apply a minimum value when building the
			// lookup tables for each combatant's total power. If the game
			// crashes at this step, check for a problem in TotalPower().
			double boarderActionChance = boarderPower[boarderCrew] / totalPower;
			double targetActionChance = 1.0 - boarderActionChance;

			// Look up scenarios for each state that might result from here.
			const Scenario &boarderCasualty = result[
				ScenarioIndex(boarderCrew - 1, targetCrew, targetAttacks)
			];
			const Scenario &targetCasualty = result[
				ScenarioIndex(boarderCrew, targetCrew - 1, targetAttacks)
			];

			// In any given state, the probabilities of one combatant or the
			// other achieving victory add up to 1, as do the probabilities of
			// each combatant suffering the next casualty.
			//
			// This lets us calculate the probability of eventual victory for
			// each combatant by combining the probability of transitioning
			// to each possible next state, each multiplied by the probability
			// of victory that it would have upon reaching that state.
			//
			// Since we account for all of the possible cases each time we
			// calculate this for a given state, the probabilities continue to
			// add up to 1 whenever we repeat the process for another state
			// that might transition into the current one.
			double boarderVictoryChance =
				boarderActionChance * boarderCasualty.boarderVictoryChance +
				targetActionChance * targetCasualty.boarderVictoryChance;

			double targetVictoryChance =
				boarderActionChance * boarderCasualty.targetVictoryChance +
				targetActionChance * targetCasualty.targetVictoryChance;

			// We already know the expected number of casualties for each
			// state that we might transition into, and the transition itself
			// inflicts a casualty on one of the combatants.
			//
			// This lets us calculate the new expected casualty count for each
			// combatant by adding the number of casualties inflicted by the
			// transition (1), multiplied by the probability of stepping to
			// that outcome. We then add the expected casualties if the combat
			// progresses down the other path, multiplied by its probability.
			//
			// Since we're covering every possible case, this generates a
			// holistic estimate at any given step. Random chance is still
			// in play, though, so we cannot know for sure which path the
			// combat will take through the tree of Scenarios. The actual
			// result could be wildly different from the expected value.
			//
			// As a future enhancement, we might consider adding a supporting
			// measure like standard deviation to help the player and AI
			// evaluate risk. At the time of writing, that seems like overkill
			// and would probably add too much clutter to the BoardingPanel.
			double boarderCasualties =
				boarderActionChance * (1.0 + boarderCasualty.boarderCasualties) +
				targetActionChance * targetCasualty.boarderCasualties;

			double targetCasualties =
				boarderActionChance * targetCasualty.targetCasualties +
				targetActionChance * (1.0 + targetCasualty.targetCasualties);

			// Store the Scenario struct for this possibility and continue.
			result[index] = Scenario{
				boarderActionChance,
				boarderVictoryChance,
				boarderCasualties,
				targetActionChance,
				targetVictoryChance,
				targetCasualties
			};
    }

	return result;
}



/**
 * Build a lookup table for the individual power of each crew member
 * on a given ship, based on that ship's current state and whether or
 * not their objective is defensive in nature.
 *
 * @param ship The ship whose effective crew power is to be calculated.
 * @param isDefensive Whether or not the ship is attempting a defensive
 * 	objective.
 * @return A vector listing the power of each effective crew member on
 *  the ship, in descending order of their power value.
 */
vector<double> BoardingProbability::EffectiveCrewPower(
	const shared_ptr<Ship> &ship,
	bool isDefensive
) const
{
	vector<double> power;

	int effectiveCrewMembers = isDefensive
		? ship->Defenders()
		: ship->Invaders();

	if(!effectiveCrewMembers)
		return power;

	// Get the base crew power for this ship based on its government.
	const double baseCrewPower = isDefensive
		? ship->GetGovernment()->CrewDefense()
		: ship->GetGovernment()->CrewAttack();

	// Check for any outfits that assist with attacking or defending.
	// Each outfit can be wielded only once, and each effective crew member
	// can wield one outfit. Outfits are claimed in descending power order.
	// Each outfit's attribute value is added to the ship's base crew power.
	const string attribute = isDefensive ? "boarding defense" : "boarding attack";
	for(const auto &it : ship->Outfits())
	{
		double outfitPower = it.first->Get(attribute);
		if(outfitPower > 0. && it.second > 0)
			power.insert(power.end(), it.second, outfitPower + baseCrewPower);
	}

	// Sort the vector from highest to lowest power value.
	sort(power.begin(), power.end(), greater<double>());

	// Resize the vector to have exactly one entry per effective crew member.
	// If the ship has more effective crew members than usable outfits,
	// the remaining entries are filled using the base crew power.
	power.resize(effectiveCrewMembers, baseCrewPower);

	return power;
}



/**
 * Used to generate the lookup tables for the total power of each ship.
 *
 * Applies a minimum attack power of 0 and defense power of 0.001 to
 * prevent inappropriate values from being used in later calculations.
 * If we need to justify this in game terms, perhaps it can represent
 * a minimum level of risk to crew safety during boarding operations.
 * After all, accidents can still happen on a ship with no defenders.
 *
 * @param ship The ship whose total power is to be calculated.
 * @param effectiveCrewPower A vector listing the power of each effective
 * 	crew member on a given ship, in descending order of their value when
 * 	either invading or defending.
 * @param isAttacking Whether or not the ship is defending.
 *
 * @return A vector listing the total power of the ship when it has a
 * 	number of remaining effective crew members equal to the index of the
 * 	vector at that position.
 */
vector<double> BoardingProbability::TotalPower(
	const shared_ptr<Ship> &ship,
	const vector<double> &effectiveCrewPower,
	bool isAttacking
) const
{
	vector<double> result;
	int effectiveCrewMembers = effectiveCrewPower.size();

	double minimum = isAttacking ? 0. : 0.001;

	// The first entry is the minimum value, since a ship with no
	// effective crew members (index 0) cannot attack or defend itself.
	result.push_back(minimum);

	// If the ship has no effective crew members, we can stop.
	if(effectiveCrewMembers < 1)
		return result;

	// We add the base attack or defense power of the ship to the power
	// of the first effective crew member (index 1).
	result.push_back(
		max(minimum, effectiveCrewPower[1] + ship->Attributes().Get(
			isAttacking ? "base capture attack" : "base capture defense"
		))
	);

	// If the ship has only one effective crew member, we can stop.
	if(effectiveCrewMembers < 2)
		return result;

	// Loop through the remaining effective crew members, adding their
	// power to the total power of the ship at each step.
	for(unsigned i = 2; i < effectiveCrewMembers; ++i)
		result.push_back(max(minimum, result.back() + effectiveCrewPower[i]));

	return result;
}
