/* Boarding.h
Copyright (c) 2025 by Luke Arndt
Some logic extracted from BoardingPanel.h, copyright (c) 2014 by Michael Zahniser

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

#include <map>
#include <memory>
#include <string>
#include <variant>



// This class contains several enums and classes that are used to model
// how boarding takes place and the decisions that each combatant can make.
// It is separate from the BoardingCombat and BoardingPanel classes so
// that other files can include it without creating a circular dependency.
class Boarding {
public:
	// The state of a boarding combat during a given Turn.
	// This is used to determine what actions are available to each combatant.
	// Combat begins in the Isolated state, and can progress through the
	// various other states as the combatants take various actions.
	enum class State {
		Isolated, // The combatants are not attached to one another.
		// If the boarder Attacks, the state becomes Poised.
		Poised, // Combat is active, but neither combatant is invading.
		// If one side Attacks, the state becomes Invading for them.
		// If both sides Attack, the state continues as Poised.
		// If both sides Defend, the state becomes Withdrawing.
		Withdrawing, // Combat has ceased, but combatants are still attached.
		// If one side Attacks, the state becomes Invading for them.
		// If both sides Defend, the state becomes Isolated.
		BoarderInvading, // The boarder has invaded the target with troops.
		// If both sides Defend, the state becomes Poised.
		TargetInvading, // The target has invaded the boarder with troops.
		// If both sides Defend, the state becomes Poised.
		BoarderVictory, // The boarder has conquered the target.
		// Target can no longer take any actions.
		TargetVictory, // The target has conquered the boarder.
		// Boarder can no longer take any actions.
		// Target can repair itself using the boarder's resources.
		Ended, // The combat is over, and no further actions can be taken.
	};

	// During a boarding combat, a combatant may attempt to Negotiate.
	// If the combatants do not share a language, the Negotiate action fails.
	//
	// If the Negotiate action succeeds, the combat is paused while the
	// combatants attempt to find an Offer that they can both agree to.
	//
	// While a negotiation is Active, each combatant can do the following:
	//
	// - Negotiate: Make a non-binding Offer for the other combatant to consider.
	//
	// - Reject: Cease negotiations and resume the combat. Afterward, the
	//   other combatant can no longer take the Negotiate action, but the
	//   rejecting combatant can resume talks using the Negotiate action.
	//
	// - Resolve: Make a binding Offer. If both combatants Resolve with
	// 	 the same Offer, the negotiation succeeds and its Terms are applied.
	//
	// During an active negotiation, neither combatant can take other actions.
	enum class Negotiation {
		// NotAttempted: neither combatant as attempted to negotiate with the other.
		// Either combatant can take the Negotiate action.
		NotAttempted,
		// BoarderRejected: the target has attempted to negotiate, but the boarder has refused.
		// The boarder can take the Negotiate action.
		// The target can no longer take the Negotiate action.
		BoarderRejected,
		// TargetRejected: the boarder has attempted to negotiate, but the target has refused.
		// The boarder can no longer take the Negotiate action.
		// The target can take the Negotiate action.
		TargetRejected,
		// Active: the combatants are currently negotiating.
		// Either combatant can take the Negotiate, Reject, or Resolve actions.
		// If both combatants Resolve with the same Offer, the negotiation succeeds.
		// If at least one combatant Rejects, the negotiation fails.
		Active,
		// Successful: the combatants have agreed to a resolution.
		// The agreed-upon Terms will be applied to the combat,
		// which will most likely change its state.
		Successful,
		// Failed: either both combatants have rejected negotiation attempts,
		// or they lack a shared language.
		// Neither combatant can take the Negotiate action.
		Failed
	};

	// This defines the conditions under which a combatant will attack.
	// A subset of this is repeated in Preferences.h, so changes here should be reflected there.
	enum class AttackStrategy {
		Cautious, // Only attack if victory is assured and no friendly casualties are expected.
		Aggressive, // Attack if victory is assured and expected casualties don't exceed extra crew.
		Reckless, // Attack if victory is likely, regardless of the casualties.
		Fanatical // Attack as long as victory is possible.
	};

	// This defines the broader strategy that the combatant uses to protect itself or its allies.
	// A subset of this is repeated in Preferences.h, so changes here should be reflected there.
	enum class DefenseStrategy {
		Repel, // Focus on preventing enemy invaders from taking over the ship. If defeat is likely, try to negotiate.
		Counter, // As Repel, but also try to lure enemies into the ship's defenses before attacking.
		Deny // Attempt to self-destruct if the ship might be captured or plundered.
	};



	// During a negotiation, the combatants can make an Offer to resolve
	// the combat without any further violence. This class is used to model
	// a set of Terms of that can be enacted if both combatants agree.
	class Offer	{
	public:
		enum class Term {
			BoarderSurrender,
			TargetSurrender,
			BoarderGovernmentPacified,
			TargetGovernmentPacified,
			CreditPaymentToBoarder,
			CreditPaymentToTarget,
			CrewFromBoarder,
			CrewFromTarget,
			PassengersFromBoarder,
			PassengersFromTarget,
			PrisonersFromBoarder,
			PrisonersFromTarget
		};

		using TermDetails = std::variant<bool, int, int64_t>;
		using Terms = std::shared_ptr<std::map<Term, TermDetails>>;

		Offer(Terms &terms);

		Offer &AddOrAmendTerm(Term term, TermDetails details);

		Offer &RemoveTerm(Term term);

		const Terms &GetTerms() const;

		const bool HasTerm(Term term) const;

	private:
		Terms terms;
	};



	/**
	 * Models a combatant's behaviour during a single Turn of boarding combat.
	 */
	class Action {
	public:
		// An overall objective for the combatant's behaviour during the Turn.
		enum class Objective {
			// Section 1: Placeholders.

			Null, // The combatant's did not specify a objective, or their intended objective was prevented.
			Pending, // The combatant's actual objective has not yet been determined.

			// Section 2: Progressing the combat in some way.

			Attack, // Try to capture the enemy or use attack power to repel invaders.
			Defend,  // Focus on preventing the enemy from invading.
			Negotiate, // Ask the enemy to negotiate, providing an Offer for consideration.
			Plunder, // Try to steal an outfit or cargo from the enemy.
			Reject, // Cease negotiations and return to the previous combat state.
			Resolve, // Agree to a given Offer. If both combatants Resolve with the same Offer, the negotiations succeed.
			SelfDestruct, // Attempt to destroy yourself, denying technology and possibly killing invaders.

			// Section 3: Special actions that are used at the end of combat.

			Capture, // Final. Requires victory. Repair the ship and take control of it, transferring crew.
			Destroy, // Final. Requires isolation or victory. Destroys the enemy ship, preventing any further boarding from others.
			Leave // Final. Requires isolation or victory. Withdraws from the ship, leaving it disabled. Others can board afterward.
		};

		// Some Objectives are more complex than others, and require some additional information.
		using Details = std::variant<
			// Usually just 'false' to indicate that the Objective has no additional details.
			bool,
			// A pair of integers, for example a plunder index and a quantity.
			// If the Objective is Plunder, the first integer is the index of
			// the outfit or cargo to plunder, and the second is the quantity.
			// Specifying -1 for either value indicates that the system should
			// determine that value automatically.
			std::tuple<int, int>,
			// An Offer, which is a set of Terms that can be enacted if both
			// combatants agree to them.
			// When supplied with a Negotiate action, the combatant is offering
			// a new set of Terms to the enemy to see if they are agreeable.
			// When supplied with a Resolve action, the combatant is making a
			// binding agreement to end the combat on these terms.
			// If both combatants take the Resolve action with the same Offer,
			// the combat will end with the agreed-upon Terms.
			Offer
		>;

		// The result of a combatant having taken an Action.
		using Effect = struct {
			State state;
			Negotiation negotiation;
			Objective casualtyObjective;
			int casualtyRolls;
		};

		using Result = struct {
			State state;
			Negotiation negotiation;
			int casualties;
			int enemyCasualties;
		};

		// What a combatant is attempting to do with the Turn.
		using Activity = struct {
			Objective objective;
			Details details;
		};

		// A map of all possible Objectives and whether or not a condition is
		// true of false for that Objective.
		using ObjectiveCondition = std::map<Objective, bool>;


		static bool IsValidDetails(Objective objective, Details details);
		static bool IsValidDetails(Objective objective, bool details);
		static bool IsValidDetails(Objective objective, std::tuple<int, int> details);
		static bool IsValidDetails(Objective objective, Offer details);

		static const ObjectiveCondition casualtiesPreventedByObjective;
		static const ObjectiveCondition isObjectiveDefensive;

		static const std::map<Objective, std::string> objectiveConstNames;
		static std::string GetObjectiveName(Objective objective);

		static const std::shared_ptr<ObjectiveCondition> ValidObjectives(
			State state,
			Negotiation negotiation,
			bool isBoarder
		);

		Action(
			Activity intended,
			Activity actual,
			Effect effect
		);

		Action(Activity intent);

		Action();

		// What the combatant attempted to do during the Turn.
		Activity intent;
		// What they actually did during the Turn, which may differ from their intention.
		Activity actual;
		// The effect that the Action has on the Turn's proceedings.
		Effect effect;
		// The result of the Action after its Effect has been applied.
		Result result;
	};

	static const std::map<State, bool> casualtiesPreventedByState;
	static const std::map<Negotiation, bool> casualtiesPreventedByNegotiation;

	static const std::map<State, std::string> stateConstNames;
	static std::string GetStateName(State state);

	static int ActionIndex(State state, bool isBoarder);

	static bool IsValidActivity(
		Action::Activity intent,
		std::shared_ptr<Action::ObjectiveCondition> validObjectives,
		bool isBoarder,
		bool shouldThrow = false
	);
};
