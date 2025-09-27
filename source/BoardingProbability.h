/* BoardingProbability.h
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

#pragma once

#include <memory>
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
 * Each invader or defender will also make use of up to one boarding
 * outfit installed on their ship, such as a hand-to-hand weapon or
 * defensive implacement. Each outfit can only be used by one crew member.
 * More powerful outfits are chosen first, in order to gain the highest
 * possible power for their current action. Defending also grants each
 * defender a +1 power bonus.
 */
class BoardingProbability {
public:
	BoardingProbability(
		const std::shared_ptr<Ship> &boarder,
		const std::shared_ptr<Ship> &target
	);

	struct Forces {
		unsigned invaders;
		unsigned defenders;
		double attackPower;
		double defensePower;
	};

	struct Scenario {
		double boarderActionChance;
		double boarderSelfDestructChance;
		double boarderVictoryChance;
		double boarderCasualties;

		double targetActionChance;
		double targetSelfDestructChance;
		double targetVictoryChance;
		double targetCasualties;
	};

	// A summary of the forces currently available to each combatant,
	// along with a Scenario for each strategy that they can employ to
	// attempt victory.
	//
	// We don't include a Scenario for neither combatant attacking, as
	// that strategy simply ends the combat, so we don't need to predict
	// any victory chance or expected casualties.
	struct Report {
		Forces boarderForces;
		Forces targetForces;

		Scenario boarderInvadesTargetDefends;
		Scenario boarderInvadesTargetSelfDestructs;

		Scenario targetInvadesBoarderDefends;
		Scenario targetInvadesBoarderSelfDestructs;

		Scenario bothAttack;
	};

	// The Scenarios type represents the complete range of potential
	// scenarios for a given strategy. Each Scenario describes the attack
	// and defense power of each combatant, along with their probability
	// of victory and expected number of casualties suffered.
	//
	// Each Scenarios collection is conceptually a two-dimensional
	// vector of Scenario structs, indexed by the number of effective crew
	// members remaining to each combatant. Unless a Scenario represents
	// total victory for a combatant, the combat can transition from that
	// Scenario to one of two other possible Scenario nodes: one where the
	// next casualty is suffered by the boarder, and the other where the
	// target does. In this way, the combat navigates the Scenarios
	// similarly to a binary tree structure, with a root node for each
	// possible victory outcome - where a combatant is reduced to 0 crew.
	//
	// As a performance optimisation, we store the Scenarios for each
	// strategy as a single flattened vector. This lets the Scenarios
	// occupy a single contiguous location in memory, and prevents the
	// game from instantiating a new vector for each possible value of
	// boarderCrew. It uses a calculated index, which we can generate with
	// the ScenarioIndex function.
	using Scenarios = std::vector<Scenario>;

	Report GetReport() const;

private:

	// Map crew numbers into an index in the lookup table.
	unsigned ScenarioIndex(
		unsigned boarderCrew,
		unsigned targetCrew,
		bool targetAttacks
	) const;

	std::vector<double> EffectiveCrewPower(
		const std::shared_ptr<Ship> &ship,
		bool isAttacking
	) const;
	Scenarios PrepareStrategy(
		bool boarderAttacks,
		bool targetAttacks
	) const;
	std::vector<double> TotalPower(
		const std::shared_ptr<Ship> &ship,
		const std::vector<double> &effectiveCrewPower,
		bool isAttacking
	) const;


private:
	// These references allow us to look up each ship's remaining crew
	// directly instead of having to pass them into every function.
	const std::shared_ptr<Ship> boarder;
	const std::shared_ptr<Ship> target;

	// The number of effective crew members on each ship at the start of
	// the combat. This is used to determine the size of the lookup tables.
	unsigned initialBoarderInvaders;
	unsigned initialBoarderDefenders;
	unsigned initialTargetInvaders;
	unsigned initialTargetDefenders;

	// Whether or not each action is considered defensive, which is used
	// to calculate a combatant's power when performing that action.
	// This is determined by Boarding::Action, but we cache it here for
	// each instance of BoardingProbability as a performance optimisation.
	bool isDefensiveAttack;
	bool isDefensiveDefend;
	bool isDefensiveSelfDestruct;

	// List of individual power values for each member of a ship's effective crew,
	// in descending order of power value when performing that role.
	std::vector<double> boarderInvaderPower;
	std::vector<double> boarderDefenderPower;
	std::vector<double> targetInvaderPower;
	std::vector<double> targetDefenderPower;

	// Lookup tables for the total power of each combatant, indexed by the
	// number of effective crew members they have remaining.
	std::vector<double> totalBoarderAttackPower;
	std::vector<double> totalBoarderDefensePower;
	std::vector<double> totalTargetAttackPower;
	std::vector<double> totalTargetDefensePower;

	// These contain all of the possible Scenarios for each strategy that
	// the combatants can apply to try to win the combat.
	//
	// The term 'strategy' here describes the choices taken by both of the
	// combatants, not just one. For example, when the boarder attacks and
	// the target defends, they are following boarderInvadesTargetDefends.
	Scenarios boarderInvadesTargetDefends;
	Scenarios boarderInvadesTargetSelfDestructs;
	Scenarios targetInvadesBoarderDefends;
	Scenarios targetInvadesBoarderSelfDestructs;
	Scenarios bothAttack;
};
