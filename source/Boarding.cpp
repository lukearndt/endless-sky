/* Boarding.cpp
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

#include "Boarding.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <variant>


using namespace std;



// A map of all possible State enum values and whether or not casualties
// are prevented in that state.
const std::map<Boarding::State, bool> Boarding::casualtiesPreventedByState
{
	{State::Isolated, true},
	{State::Poised, false},
	{State::Withdrawing, true},
	{State::BoarderInvading, false},
	{State::TargetInvading, false},
	{State::BoarderVictory, true},
	{State::TargetVictory, true},
	{State::Ended, true}
};



// A map of all possible Negotiation enum values and whether or not
// casualties are prevented with that negotiation status.
const std::map<Boarding::Negotiation, bool> Boarding::casualtiesPreventedByNegotiation
{
	{Negotiation::NotAttempted, false},
	{Negotiation::BoarderRejected, false},
	{Negotiation::TargetRejected, false},
	{Negotiation::Active, true},
	{Negotiation::Successful, false},
	{Negotiation::Failed, false}
};



// Determines whether or not a given Action::Objective can produce
// casualties during its execution. If the value is false, an Action
// with that Objective does not contribute casualty rolls to the turn.
const Boarding::Action::ObjectiveCondition Boarding::Action::casualtiesPreventedByObjective
{
	{Action::Objective::Null, true},
	// Pending is omitted because it is merely a placeholder for an
	// objective rather than one that can be performed.
	// Trying to check for it in this map signals that we have a bug.
	{Action::Objective::Attack, false},
	{Action::Objective::Defend, false},
	{Action::Objective::Plunder, true},
	{Action::Objective::SelfDestruct, false},
	{Action::Objective::Negotiate, true},
	{Action::Objective::Reject, true},
	{Action::Objective::Resolve, true},
	{Action::Objective::Capture, true},
	{Action::Objective::Leave, true},
	{Action::Objective::Destroy, false}
};



// A map of all possible State enum values and the name of each
// of their underlying constants. This is intended for use in error
// messages rather than in the game itself.
const std::map<Boarding::State, std::string> Boarding::stateConstNames
{
	{State::Isolated, "Isolated"},
	{State::Poised, "Poised"},
	{State::Withdrawing, "Withdrawing"},
	{State::BoarderInvading, "BoarderInvading"},
	{State::TargetInvading, "TargetInvading"},
	{State::BoarderVictory, "BoarderVictory"},
	{State::TargetVictory, "TargetVictory"},
	{State::Ended, "Ended"}
};



/**
 * State is an enum class. This is great for performance, but not very
 * human-readable when we need to display the value in an error message.
 *
 * @return The name of a given State as a string, based on its entry
 * 	in the stateConstNames constant.
 */
std::string Boarding::GetStateName(State state)
{
	try {
		return stateConstNames.at(state);
	} catch(const std::out_of_range &e) {
		return "Invalid State value:" + to_string(static_cast<int>(state));
	}
}



// Determines whether or not a given Action::Objective is considered
// defensive, allowing the combatant to use their defense power when
// performing that action.
// If the Objective is not considered defensive, the combatant will use
// their attack power instead.
const Boarding::Action::ObjectiveCondition Boarding::Action::isObjectiveDefensive
{
	// Null and Pending are not on this list because we ought not to
	// use them in any of the calculations that make use of this map.
	// If this results in an error, it signals that we have a bug.
	// If that principle changes, we will need to add them to map.
	{Action::Objective::Attack, false},
	{Action::Objective::Defend, true},
	{Action::Objective::Plunder, false},
	{Action::Objective::SelfDestruct, true},
	{Action::Objective::Negotiate, true},
	{Action::Objective::Reject, false},
	{Action::Objective::Resolve, true},
	{Action::Objective::Capture, false},
	{Action::Objective::Leave, true},
	{Action::Objective::Destroy, false}
};



// A map of all possible Action::Objective enum values and the name of each
// of their underlying constants. This is intended for use in error
// messages rather than in the game itself.
const std::map<Boarding::Action::Objective, std::string> Boarding::Action::objectiveConstNames
{
	{Objective::Null, "Null"},
	{Objective::Pending, "Pending"},
	{Objective::Attack, "Attack"},
	{Objective::Defend, "Defend"},
	{Objective::Plunder, "Plunder"},
	{Objective::SelfDestruct, "SelfDestruct"},
	{Objective::Negotiate, "Negotiate"},
	{Objective::Reject, "Reject"},
	{Objective::Resolve, "Resolve"},
	{Objective::Capture, "Capture"},
	{Objective::Leave, "Leave"},
	{Objective::Destroy, "Destroy"}
};



/**
 * Objective is an enum class. This is great for performance, but not very
 * human-readable when we need to display the value in an error message.
 *
 * @return The name of a given Objective as a string, based on its entry
 * 	in the objectiveConstNames constant.
 */
std::string Boarding::Action::GetObjectiveName(Objective objective)
{
	try {
		return objectiveConstNames.at(objective);
	} catch(const std::out_of_range &e) {
		return "Invalid Objective value:" + to_string(static_cast<int>(objective));
	}
}




/**
 * Builds a map of all possible Action::Objective options and whether or not
 * a combatant is allowed to choose each one, based on the current state
 * of the boarding combat, any ongoing negotiations, and whether or not
 * the combatant is the boarder.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The current state of the negotiation.
 * @param isBoarder Whether or not the combatant is the boarder.
 *
 * @return Shared pointer to a Boarding::Action::ObjectiveCondition map.
 *
 * 	Key: Each possible Action::Objective option.
 *
 * 	Value: Whether or not the combatant is allowed to choose that option.
 */
const shared_ptr<Boarding::Action::ObjectiveCondition> Boarding::Action::ValidObjectives(
	State state,
	Negotiation negotiation,
	bool isBoarder
)
{
	// While there is an active negotiation, the combatants are not allowed to
	// take any actions other than to Negotiate, Reject, or Resolve.
	if(negotiation == Negotiation::Active) {
		return make_shared<Action::ObjectiveCondition>(
			Action::ObjectiveCondition({
				{Action::Objective::Attack, false},
				{Action::Objective::Defend, false},
				{Action::Objective::Plunder, false},
				{Action::Objective::SelfDestruct, false},
				{Action::Objective::Negotiate, true},
				{Action::Objective::Reject, true},
				{Action::Objective::Resolve, true},
				{Action::Objective::Capture, false},
				{Action::Objective::Leave, false},
				{Action::Objective::Destroy, false}
			})
		);
	}

	bool canAttack = (state == State::Isolated && isBoarder)
		|| state == State::BoarderInvading
		|| state == State::Poised
		|| state == State::TargetInvading;

	bool canDefend = (state == State::Isolated && !isBoarder)
		|| state == State::BoarderInvading
		|| state == State::Poised
		|| state == State::TargetInvading;

	bool canSelfDestruct = (state == State::Isolated && !isBoarder)
		|| (state == State::BoarderInvading && !isBoarder)
		|| state == State::Poised
		|| (state == State::TargetInvading && isBoarder);

	bool canPlunder = (state == State::Isolated && isBoarder)
		|| (state == State::BoarderVictory && isBoarder)
		|| (state == State::TargetVictory && !isBoarder);

	bool stateAllowsNegotiation = state == State::Isolated
		|| state == State::Poised
		|| state == State::Withdrawing
		|| state == State::BoarderInvading
		|| state == State::TargetInvading;

	// The case where the combatants are already negotiating is handled
	// above, so this expression determines whether or not the combatant
	// can make an opening Offer using the Negotiate action.
	bool canNegotiate = stateAllowsNegotiation && (
		negotiation == Negotiation::NotAttempted
		|| (negotiation == Negotiation::BoarderRejected && isBoarder)
		|| (negotiation == Negotiation::TargetRejected && !isBoarder)
	);
	bool canResolve = false;
	bool canReject = false;

	bool canCapture = (state == State::BoarderVictory && isBoarder)
			|| (state == State::TargetVictory && !isBoarder);

	bool canLeave = (state == State::Isolated && isBoarder)
			|| (state == State::BoarderVictory && isBoarder)
			|| (state == State::TargetVictory && !isBoarder);

	bool canDestroy = (state == State::Isolated && isBoarder)
			|| (state == State::BoarderVictory && isBoarder)
			|| (state == State::TargetVictory && !isBoarder);

	return make_shared<Action::ObjectiveCondition>(
		Action::ObjectiveCondition({
			{Action::Objective::Attack, canAttack},
			{Action::Objective::Defend, canDefend},
			{Action::Objective::Plunder, canPlunder},
			{Action::Objective::SelfDestruct, canSelfDestruct},
			{Action::Objective::Negotiate, canNegotiate},
			{Action::Objective::Reject, canReject},
			{Action::Objective::Resolve, canResolve},
			{Action::Objective::Capture, canCapture},
			{Action::Objective::Leave, canLeave},
			{Action::Objective::Destroy, canDestroy}
		})
	);
}



/**
 * Validates that Action::Details are valid for a given Action::Objective.
 *
 * @param objective The objective that the details are to be validated against.
 * @param details The details of the combatant's intended objective.
 *
 * @return Whether or not the details are valid for the objective.
 */
bool Boarding::Action::IsValidDetails(Action::Objective objective, Action::Details details)
{
	if(holds_alternative<bool>(details)) {
		return IsValidDetails(objective, get<bool>(details));
	}	else if(holds_alternative<tuple<int, int>>(details)) {
		return IsValidDetails(objective, get<tuple<int, int>>(details));
	} else if(holds_alternative<Offer>(details)) {
		return IsValidDetails(objective, get<Offer>(details));
	}
	return false; // If none of the types match, return false
}



/**
 * Validates that Action::Details are valid for a given Action::Objective.
 *
 * This is a specific overload for when the details are a tuple<int, int>.
 *
 * @param objective The objective that the details are to be validated against.
 * @param details The details of the combatant's intended objective.
 *
 * @return Whether or not the details are valid for the objective.
 */
bool Boarding::Action::IsValidDetails(Action::Objective objective, tuple<int, int> details)
{
	switch(objective)
	{
		case Action::Objective::Plunder : return true; break;
		default : return false;
	}
}



/**
 * Validates that Action::Details are valid for a given Action::Objective.
 *
 * This is a specific overload for when the details are an Offer.
 *
 * @param objective The objective that the details are to be validated against.
 * @param details The details of the combatant's intended objective.
 *
 * @return Whether or not the details are valid for the objective.
 */
bool Boarding::Action::IsValidDetails(Action::Objective objective, Offer details)
{
	switch(objective)
	{
		case Action::Objective::Negotiate : return true; break;
		case Action::Objective::Resolve : return true; break;
		default : return false;
	}
}



/**
 * Validates that Action::Details are valid for a given Action::Objective.
 *
 * This is a specific overload for when the details are a boolean value,
 * which also happens to be case for any objective that does not require
 * any additional information.
 *
 * If you add a new objective that requires additional details, you will
 * need to update this function accordingly.
 *
 * @param objective The objective that the details are to be validated against.
 * @param details The details of the combatant's intended objective.
 *
 * @return Whether or not the details are valid for the objective.
 */
bool Boarding::Action::IsValidDetails(Action::Objective objective, bool details)
{
	switch(objective)
	{
		case Action::Objective::Plunder : return false; break;
		case Action::Objective::Negotiate : return false; break;
		case Action::Objective::Resolve : return false; break;
		default : return !details;
	}
}



// --- End of Boarding::Action class implementation ---



// --- Start of Boarding static functions ---



/**
 * @param state The state of the combat at the time the action is attempted.
 * @param isBoarder Whether or not the combatant is the boarder.
 *
 * @return The index of the combatant's action in the Turn's actions vector.
 */
int Boarding::ActionIndex(State state, bool isBoarder)
{
  switch(state)
	{
		case State::TargetInvading:
			return isBoarder ? 0 : 1;
		default:
			return isBoarder ? 1 : 0;
	}
}



/**
 * Checks that an Action::Activity is valid for a given State and Negotiation.
 *
 * This has two requirements:
 *
 * 1. The Activity must contain a valid objective for the situation.
 * 2. The details of the Activity must be valid for that objective.
 *
 * @param activity The Activity to be validated.
 * @param validObjectives The valid objectives that the combatant can make.
 * @param isBoarder Whether or not the combatant is the boarder.
 * 	This is not used to determine the validity of the Activity,
 * 	but it helps us provide a more informative error message.
 * @param shouldThrow If set to true, the function throws an error when
 * 	the Activity is invalid instead of returning false. Useful when we
 * 	need to abort anyway, as it produces a more specific error message.
 *
 * @throws invalid_argument If the Activity fails to meet any of the
 * 	requirements for validity. The message will describe the problem.
 */
bool Boarding::IsValidActivity(
	Action::Activity activity,
	shared_ptr<Boarding::Action::ObjectiveCondition> validObjectives,
	bool isBoarder,
	bool shouldThrow
)
{
	string combatantName = isBoarder ? "the boarder" : "the target";
  if(!validObjectives->at(activity.objective))
	{
		if(!shouldThrow)
			return false;

		throw invalid_argument(
			"Boarding::IsValidActivity (invalid objective) - "
			+ combatantName
			+ " has supplied an Activity with the "
			+ Action::GetObjectiveName(activity.objective)
			+ " objective, which is not valid in the current situation."
		);
	}

	if(!Action::IsValidDetails(activity.objective, activity.details))
	{
		if(!shouldThrow)
			return false;

		throw invalid_argument(
			"Boarding::IsValidActivity (invalid details) - "
			+ combatantName
			+ " has supplied an Activity with the "
			+ Action::GetObjectiveName(activity.objective)
			+ " objective, but the details are not valid for that objective."
		);
	}

	return true;
}

// --- End of Boarding static functions ---



/**
 * Constructs a new Boarding::Action object.
 *
 * Keep logic out of this constructor. It should only be used to create
 * the Action object once we have all the information we need to do so.
 *
 * @param intent The Activity that the combatant intended to perform.
 * @param actual The Activity that the combatant actually performed.
 * 	combatant's casualty power.
 * @param effect An Action::Effect describing the action's overall impact.
 */
Boarding::Action::Action(
	Activity intent,
	Activity actual,
	Effect effect
) :
	intent(intent),
	actual(actual),
	effect(effect)
{}



/**
 * Constructs an unresolved Boarding::Action object that only knows
 * the intended Objective and its supporting Details. Used to represent an
 * action that has not yet been taken, such as when a different combatant
 * is acting before the one represented by this object.
 *
 * @param intent The Activity that the combatant intends to perform.
 */
Boarding::Action::Action(Activity intent)	:
	intent(intent),
	actual(Activity(Objective::Pending, false)),
	effect({})
{}



/**
 * Default constructor for the Boarding::Action class.
 * Only used when we need to create an empty Action object, such as
 * during the first turn of a boarding combat.
 */
Boarding::Action::Action() :
	intent(Activity(Objective::Null, false)),
	actual(Activity(Objective::Null, false)),
	effect({})
{}
