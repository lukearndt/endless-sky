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
#include <variant>



// This class contains several enumsand classes that are used to model
// how boarding takes place and the decisions that each combatant can make.
// It is separate from the BoardingCombat and BoardingPanel classes in
// order to prevent circular dependencies between the files that define
// the boarding combat system and other files that interact with it.
class Boarding {
public:
	// The actions that a combatant can take during a single Turn of combat.
	enum class Action {
		Null, // The combatant does not take an action. Used during Turn 0 or when defeated.

		// Section 1: Actions taken during active combat.

		Attack, // Try to capture the enemy or use attack power to repel invaders.
		Defend,  // Focus on preventing the enemy from invading.
		Negotiate, // Ask the enemy to negotiate. Might open a mission dialogue.
		Plunder, // Try to steal an outfit or cargo from the enemy.
		SelfDestruct, // Attempt to destroy yourself, denying technology and possibly killing some invaders.

		// Section 2: Actions that end the combat, applying a particular outcome.

		Capture, // Final. Requires victory. Repair the ship and take control of it, transferring crew.
		Destroy, // Final. Requires isolation or victory. Destroys the enemy ship, preventing any further boarding from others.
		Leave, // Final. Requires isolation or victory. Withdraws from the ship, leaving it disabled. Others can board afterward.
		Raid, // Final. Requires isolation or victory. Plunders as much value as possible. Destroys enemy if no plunder remains.
		Resolve // End the combat without further violence, applying the agreed-upon Terms.
	};

	// The state of a boarding combat during a given Turn.
	enum class State {
		// Isolated: the combatants are not attached to one another.
		// If the boarder chooses Attack, the state becomes BoarderInvading.
		Isolated,
		// BoarderInvading: the boarder has invaded the target with troops.
		// If both sides Defend, the state becomes Poised.
		BoarderInvading,
		// Poised: neither combatant is invading, but they are attached to one another.
		// If both sides Defend, the state becomes Isolated.
		Poised,
		// TargetInvading: the target has invaded the boarder with troops.
		// If both sides Defend, the state becomes Poised.
		TargetInvading,
		// BoarderVictory: the boarder has conquered the target.
		// Target can no longer take any actions.
		BoarderVictory,
		// TargetVictory: the target has conquered the boarder.
		// Boarder can no longer take any actions.
		// Target can repair itself using the boarder's resources.
		TargetVictory,
		// Ended: the combat is over, and no further actions can be taken.
		Ended
	};

	// During a boarding combat, each combatant may attempt to negotiate with the other.
	// If the Negotiate action succeeds, they can discuss terms and potentially end the combat.
	// If one combatant rejects the Negotiate action, the other combatant can no longer take it.
	// Combatants must share a language in order to negotiate.
	enum class NegotiationState {
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
		// Neither combatant can take the Negotiate action.
		// Either combatant can take the Forgive or Surrender actions.
		Active,
		// Successful: the combatants have agreed to a resolution.
		// Neither combatant can take the Negotiate or Attack action.
		// The agreed-upon terms will be applied to the combatants, which may include a victory condition.
		Successful,
		// Failed: either both combatants have rejected negotiation attempts, or they lack a shared language.
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



	// Some Actions require additional information in order to be resolved.
	using ActionDetails = std::variant<int, std::tuple<int, int>, Offer>;
};


