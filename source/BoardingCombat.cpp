/* BoardingCombat.h
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

#include "BoardingCombat.h"

#include "Boarding.h"
#include "Depreciation.h"
#include "text/Format.h"
#include "GameData.h"
#include "Gamerules.h"
#include "Government.h"
#include "Logger.h"
#include "Messages.h"
#include "Outfit.h"
#include "PlayerInfo.h"
#include "Preferences.h"
#include "Ship.h"
#include "ShipEvent.h"
#include "System.h"

#include <algorithm>
#include <utility>
#include <variant>

using namespace std;



// --- Start of BoardingCombat::Turn class implementation ---



/**
 * Primary constructor for BoardingCombat::Turn.
 *
 * This constructor is used to create a the next Turn in the boarding combat.
 * It calculates the number of casualties for each side, updates the state
 * of the combat, and generates a summary for display.
 *
 * This constructor will raise an error if the boarder or target tries to
 * take an invalid action. This should never happen, since the rest of the
 * game code should prevent the AI or player from taking invalid Actions.
 * However, since bugs are always possible, we want to make sure that the
 * game does not allow a boarding combat to proceed along an invalid path.
 *
 * @param combat A reference to the BoardingCombat that is taking place.
 * @param boarderAction The Action that the boarder will take this round.
 * @param targetAction The Action that the target will take this round.
 *
 * @throws An error if the boarder or target tries to take an invalid action.
 */
BoardingCombat::Turn::Turn(
	BoardingCombat &combat,
	Action::Activity boarderIntent,
	Action::Activity targetIntent
) :
	previous(combat.history->back()),
	state(previous->state),
	negotiation(previous->negotiation),
	boarderIntent(boarderIntent),
	targetIntent(targetIntent),
	boarderActionIndex(ActionIndex(state, true)),
	targetActionIndex(boarderActionIndex == 0 ? 1 : 0),
	actions({
		// The order of these is a performance optimization, since the
		// target almost always goes first.
		targetActionIndex == 0 ? Action(targetIntent) : Action(boarderIntent),
		boarderActionIndex == 1 ? Action(boarderIntent) : Action(targetIntent),
	}),
	messages({})
{
	// Validate the inputs before we go any further. An error here means
	// that there is a bug in the code causing a combatant to take an
	// invalid action.
	//
	// If we don't check this up front, the problem will be hidden by the
	// IsValidActivity check inside AttemptAction, and it will seem like
	// the combatant was prevented from taking their action due to the
	// prior action of their enemy. That can happen legitimately and does
	// not throw an error, so we need to separate those two situations.
	IsValidActivity(boarderIntent, previous->boarderSituationReport->validObjectives, true, true);
	IsValidActivity(targetIntent, previous->targetSituationReport->validObjectives, false, true);

	for(int i = 0; i < actions.size(); i++)
	{
		if(i == boarderActionIndex)
			actions[i] = combat.boarder->AttemptAction(
				combat,
				state,
				negotiation,
				boarderIntent,
				targetIntent,
				combat.target->ActionPower(targetIntent.objective)
			);
		else
			actions[i] = combat.target->AttemptAction(
				combat,
				state,
				negotiation,
				targetIntent,
				boarderIntent,
				combat.boarder->ActionPower(boarderIntent.objective)
			);

		state = actions[i].effect.state;
		negotiation = actions[i].effect.negotiation;
	}

	for(int i = 0; i < actions.size(); i++)
	{
		if(i == boarderActionIndex)
			actions[i].result = combat.ApplyAction(
				state,
				negotiation,
				true,
				actions[i],
				actions[!i]
			);
		else
			actions[i].result = combat.ApplyAction(
				state,
				negotiation,
				false,
				actions[i],
				actions[!i]
			);

		state = actions[1].result.state;
		negotiation = actions[1].result.negotiation;
	}

	// Generate a SituationReport for each combatant.
	boarderSituationReport = make_shared<SituationReport>(
		combat.boarder,
		combat.target,
		*this,
		previous->boarderSituationReport
	);
	targetSituationReport = make_shared<SituationReport>(
		combat.target,
		combat.boarder,
		*this,
		previous->targetSituationReport
	);
}



/**
 * Constructor for BoardingCombat::Turn that is used to resolve the
 * combat when both combatants use a Resolve intent with the same Offer.
 *
 * Adds a Turn to the History where both combatants take the Resolve
 * action, and applies the Terms of the Offer to the combatants.
 *
 * Changes the negotiation of the combat to Successful.
 * The new state of the combat is based on the Terms of the Offer.
 *
 * @param combat A reference to the BoardingCombat that is taking place.
 * @param agreement The Offer that the boarder and target have made.
 *
 * @return A new Turn that models the negotiated resolution of the combat.
 */
BoardingCombat::Turn::Turn(BoardingCombat &combat, Offer &agreement) :
	previous(combat.history->back()),
	state(previous->state),
	negotiation(Negotiation::Successful),
	boarderIntent(Action::Activity(Action::Objective::Resolve, agreement)),
	targetIntent(Action::Activity(Action::Objective::Resolve, agreement)),
	boarderActionIndex(ActionIndex(state, true)),
	targetActionIndex(boarderActionIndex == 0 ? 1 : 0),
	actions({
		// The order of these is a performance optimization, since the
		// target almost always goes first.
		targetActionIndex == 0 ? Action(targetIntent) : Action(boarderIntent),
		boarderActionIndex == 1 ? Action(boarderIntent) : Action(targetIntent),
	})
{
	// Apply the Terms of the Offer to the combatants.
	for(auto it : *agreement.GetTerms())
	{
		try
		{
			switch(it.first)
			{
				case Offer::Term::BoarderSurrender:
					state = State::TargetVictory;
					break;
				case Offer::Term::TargetSurrender:
					state = State::BoarderVictory;
					break;
				case Offer::Term::BoarderGovernmentPacified:
					// TODO: Implement this term. Perhaps it could work like bribing an enemy ship during a hail.
					break;
				case Offer::Term::TargetGovernmentPacified:
					// TODO: Implement this term. Perhaps it could work like bribing an enemy ship during a hail.
					break;
				case Offer::Term::CreditPaymentToBoarder:
					if(combat.boarder->IsPlayerControlled())
						combat.player.Accounts().AddCredits(get<int64_t>(it.second));
					else if(combat.target->IsPlayerControlled())
						combat.player.Accounts().AddCredits(-get<int64_t>(it.second));
					break;
				case Offer::Term::CreditPaymentToTarget:
					if(combat.target->IsPlayerControlled())
						combat.player.Accounts().AddCredits(get<int64_t>(it.second));
					else if(combat.boarder->IsPlayerControlled())
						combat.player.Accounts().AddCredits(-get<int64_t>(it.second));
					break;
				case Offer::Term::CrewFromBoarder:
					combat.boarder->GetShip()->AddCrew(-get<int>(it.second));
					combat.target->GetShip()->AddCrew(get<int>(it.second));
					break;
				case Offer::Term::CrewFromTarget:
					combat.target->GetShip()->AddCrew(-get<int>(it.second));
					combat.boarder->GetShip()->AddCrew(get<int>(it.second));
					break;
				case Offer::Term::PassengersFromBoarder:
					combat.boarder->GetShip()->AddCrew(-get<int>(it.second));
					// TODO: Implement this term. Might need to add a mission to drop them off nearby.
					break;
				case Offer::Term::PassengersFromTarget:
					combat.target->GetShip()->AddCrew(-get<int>(it.second));
					// TODO: Implement this term. Might need to add a mission to drop them off nearby.
					break;
				case Offer::Term::PrisonersFromBoarder:
					combat.boarder->GetShip()->AddCrew(-get<int>(it.second));
					// TODO: Implement this term. Might need to add a mission to drop them off nearby.
					break;
				case Offer::Term::PrisonersFromTarget:
					combat.target->GetShip()->AddCrew(-get<int>(it.second));
					// TODO: Implement this term. Might need to add a mission to drop them off nearby.
					break;
				default:
					Logger::LogError("BoardingCombat::Turn - Invalid Offer Term: " + to_string(static_cast<int>(it.first)));
			}
		}
		catch(const bad_variant_access &e)
		{
			Logger::LogError("BoardingCombat::Turn - Invalid Offer Term Value: " + to_string(static_cast<int>(it.first)));
		}
	}

	if(state != State::BoarderVictory && state != State::TargetVictory)
		state = State::Ended;

	// Generate a SituationReport for each combatant.
	boarderSituationReport = make_shared<SituationReport>(
		combat.boarder,
		combat.target,
		*this,
		previous->boarderSituationReport
	);
	targetSituationReport = make_shared<SituationReport>(
		combat.target,
		combat.boarder,
		*this,
		previous->targetSituationReport
	);
}



/**
 * Special BoardingCombat::Turn constructor for creating the first turn
 * of the combat. This lets us create a no-action turn at the start of the
 * History so that it is never empty. Having this in a separate constructor
 * prevents us from having to check for an empty History every time we
 * call the primary constructor.
 *
 * @param boarder The combatant that is boarding the target.
 * @param target The combatant that is being boarded.
 */
BoardingCombat::Turn::Turn(
	shared_ptr<Combatant> &boarder,
	shared_ptr<Combatant> &target
) :
	previous(nullptr),
	state(State::Isolated),
	negotiation(Negotiation::NotAttempted),
	boarderIntent(Action::Activity(Action::Objective::Null, false)),
	targetIntent(Action::Activity(Action::Objective::Null, false)),
	boarderActionIndex(ActionIndex(state, true)),
	targetActionIndex(boarderActionIndex == 0 ? 1 : 0),
	actions({
		// The order of these is a performance optimization, since the
		// target almost always goes first.
		targetActionIndex == 0 ? Action(targetIntent) : Action(boarderIntent),
		boarderActionIndex == 1 ? Action(boarderIntent) : Action(targetIntent),
	}),
	casualties({State::Isolated, 0, 0}),
	boarderSituationReport(make_shared<SituationReport>(
		boarder, target, *this, nullptr
	)),
	targetSituationReport(make_shared<SituationReport>(
		target, boarder, *this, nullptr
	)),
	messages({})
{
	bool isPlayerBoarding = boarder->IsPlayerControlled();

	shared_ptr<Ship> playerShip = isPlayerBoarding
		? boarder->GetShip()
		: target->GetShip();

	shared_ptr<Ship> enemyShip = isPlayerBoarding
		? target->GetShip()
		: boarder->GetShip();

	string enemyShipName = "A " + enemyShip->GetGovernment()->GetName()
		+ " " + enemyShip->DisplayModelName();

	string message = isPlayerBoarding
		? playerShip->QuotedName()
		: enemyShipName;

	message += " approaches ";

	message += isPlayerBoarding
		? enemyShipName
		: playerShip->QuotedName();

	message += " and matches velocity.";

	messages.push_back(message);
}



/**
 * @return The boarder's Action for this Turn, which may be in a Pending
 * 	state if that action has not yet been attempted.
 */
Boarding::Action BoardingCombat::Turn::BoarderAction() const
{
	if(actions.size() > boarderActionIndex)
		return actions[boarderActionIndex];
	else
		return Action(boarderIntent);
}



/**
 * @return The target's Action for this Turn, which may be in a Pending
 * 	state if that action has not yet been attempted.
 */
Boarding::Action BoardingCombat::Turn::TargetAction() const
{
	if(actions.size() > targetActionIndex)
		return actions[targetActionIndex];
	else
		return Action(targetIntent);
}



// --- End of BoardingCombat::Turn class implementation ---



// --- Start of BoardingCombat::Combatant class implementation ---

/**
 * Constructor for BoardingCombat::Combatant
 *
 * @param ship A ship that is participating in the boarding combat.
 * @param attackStrategy The strategy that the ship will use to attack.
 * @param defenseStrategy The strategy that the ship will use to defend.
 * @param isBoarder Whether or not the ship is the boarder in the combat.
 * @param isPlayerControlled Whether or not the ship is controlled by the player.
 * @param playerFleet The player's fleet. Only required if this combatant is player-controlled.
 */
BoardingCombat::Combatant::Combatant(
	shared_ptr<Ship> &ship,
	shared_ptr<Ship> &enemyShip,
	shared_ptr<BoardingOdds> &odds,
	bool isBoarder,
	bool usingBoardingPanel,
	const vector<shared_ptr<Ship>> &playerFleet
) :
	ship(ship),
	plunderSession(make_shared<Plunder::Session>(enemyShip, ship, playerFleet)),
	goal(ship->GetBoardingGoal(true)),
	attackStrategy(
		ship->IsYours()
			? static_cast<AttackStrategy>(Preferences::GetBoardingAttackStrategy())
			: ship->GetGovernment()->BoardingAttackStrategy()
	),
	defenseStrategy(
		ship->IsYours()
			? static_cast<DefenseStrategy>(Preferences::GetBoardingDefenseStrategy())
			: ship->GetGovernment()->BoardingDefenseStrategy()
	),
	automatedDefenders(static_cast<int>(ship->Attributes().Get("automated defenders"))),
	automatedInvaders(static_cast<int>(ship->Attributes().Get("automated invaders"))),
	odds(odds),
	crewAnalysisBefore(make_shared<Crew::ShipAnalysis>(ship, ship->IsPlayerFlagship())),
	crewDisplayNameMidSentence(BoardingCombat::BuildCrewDisplayName(ship, false)),
	crewDisplayNameStartOfSentence(BoardingCombat::BuildCrewDisplayName(ship, true)),
	captureValue(ship->Cost() * Depreciation::Full()),
	chassisValue(ship->ChassisCost() * Depreciation::Full()),
	protectedPlunderValue(0),
	expectedCostPerBoardingCasualty(Crew::ExpectedCostPerCasualty(true)),
	expectedCostPerCasualtyPostCapture(Crew::ExpectedCostPerCasualty(false)),
	hasCaptureGoal(goal == Ship::BoardingGoal::CAPTURE),
	hasPlunderGoal(goal == Ship::BoardingGoal::PLUNDER),
	isBoarder(isBoarder),
	isPlayerControlled(isBoarder ? ship->IsYours() : enemyShip->IsYours())
{
	// Calculate the value of the protected outfits on the ship.
	auto protectedOutfits = ship->ProtectedOutfits();
	if(protectedOutfits->size() > 0)
		for(auto &it : *protectedOutfits)
			protectedPlunderValue += it.first->Cost() * it.second * Depreciation::Full();

	// Look up and cache the odds that the ship will survive post-capture.
	auto survivalOddsForCategory = BoardingCombat::postCaptureSurvivalOddsByCategory.find(ship->Attributes().Category());
	if(survivalOddsForCategory != BoardingCombat::postCaptureSurvivalOddsByCategory.end())
		postCaptureSurvivalOdds = survivalOddsForCategory->second;
	else {
		postCaptureSurvivalOdds = BoardingCombat::postCaptureSurvivalOddsByCategory.at("UnknownCategory");
		Logger::LogError(
			"No post-capture survival odds found for ship category: " + ship->Attributes().Category()
			+ ". Defaulting to " + Format::Number(postCaptureSurvivalOdds * 100) + "%."
		);
	}

	expectedPostCaptureCasualtyCosts = ship->RequiredCrew() * (1 - postCaptureSurvivalOdds) * expectedCostPerCasualtyPostCapture;
}



/**
 * Applies a single casualty to the combatant.
 *
 * Automated systems are always disabled first, though they are not destroyed.
 *
 * Once there are no relevant automated systems left, each casualty removes
 * a crew member from the ship.
 *
 * @param isInvading True if the combatant is invading, otherwise false.
 * @return The number of defenders that the combatant has remaining.
 */
int BoardingCombat::Combatant::ApplyCasualty(bool isInvading)
{
  if(isInvading && automatedInvaders)
		--automatedInvaders;
	else if(!isInvading && automatedDefenders)
		--automatedDefenders;
	else
		ship->AddCrew(-1);

	return Defenders();
}



/**
 * Builds an analysis of the crew members that died during the combat.
 *
 * Note that this function should be called before any other factors
 * change the combatant's crew, such as transferring crew members to
 * the enemy ship upon capture.
 *
 * The main goal of the CasualtyAnalysis is to provide the information
 * needed to calculate the financial impact of the combatant's crew
 * members dying, such as any death benefits and death shares owed.
 *
 * @return A shared pointer to a Crew::CasualtyAnalysis object.
 */
shared_ptr<Crew::CasualtyAnalysis> BoardingCombat::Combatant::CasualtyAnalysis()
{
  return make_shared<Crew::CasualtyAnalysis>(crewAnalysisBefore, ship);
}



/**
 * @param objective The Objective to determine the number of casualty rolls for.
 *
 * @return The number of casualty rolls that the objective adds to the
 * 	pool for the turn. This is a flat percentage of the combatant's
 * 	current attacker or defender count, as defined in the game rules
 * 	as BoardingCasualtyPercentagePerAction.
 */
int BoardingCombat::Combatant::CasualtyRolls(
	State state,
	Negotiation negotiation,
	Action::Objective objective
) const
{
	if(
		casualtiesPreventedByState.at(state)
			|| casualtiesPreventedByNegotiation.at(negotiation)
			|| Action::casualtiesPreventedByObjective.at(objective)
		)
		return 0;

  return max(
		1,
		static_cast<int>(
			GameData::GetGamerules().BoardingCasualtyPercentagePerAction()
			* (Action::isObjectiveDefensive.at(objective) ? Defenders() : Invaders())
		)
	);
}



/**
 * Evaluates the odds of victory, the expected casualties, and the potential
 * profit of invading the enemy. Compares them with the combatant's
 * strategy to determine if it is willing to risk mounting an invasion.
 *
 * The ranking crew members making this decision are assumed to be capable
 * and experienced, having a good understanding of the risks of boarding.
 * They are expected to know what kinds of ships tend to have self-destruct
 * systems, since the player is able to learn those same patterns after
 * trying to board a few marauders and alien drones.
 *
 * Those ranking crew members are also expected to have a working knowledge
 * of the relative value of various kinds of ships, outfits, and cargo
 * and be able to weigh the balance those rewards. Given the common
 * availability of outfit, cargo, and tactical scanners, we also assume
 * that at least one of the ships in their fleet has been able to scan
 * the enemy vessel and communicate its findings to the rest of the fleet.
 *
 * In theory we could check if the combatant's fleet has actually
 * performed various kinds of scans on the enemy vessel, but that would
 * make this process a great deal more complex for very little real gain.
 * The player already gets a clear view of the enemy's cargo and
 * outfits when they board it, so it's not unreasonable to assume that
 * the AI crew members have that same information.
 *
 * @param report A shared pointer to the combatant's latest SituationReport.
 *
 * @return Whether or not the combatant is willing to attack.
 */
bool BoardingCombat::Combatant::ConsiderAttacking(const shared_ptr<SituationReport> &report) const
{
	if(!report->validObjectives->at(Action::Objective::Attack))
		return false;

	const Turn latest = report->turn;

	// If the combatant's priority is to deny the enemy its resources,
	// they will only attack if doing so is a more effective way to
	// achieve that objective than defending or self-destructing would be.
	if(defenseStrategy == DefenseStrategy::Deny)
	{
		if(report->enemyValidObjectives->at(Action::Objective::Plunder))
			return report->invasionVictoryProbability > report->selfDestructProbability;
		else if(report->isEnemyInvading)
			return report->invasionVictoryProbability > report->defensiveVictoryProbability
				&& report->invasionVictoryProbability > report->selfDestructProbability;
	}

	// While being invaded, a combatant's priority is to repel invaders,
	// but sometimes attacking is the best possible defense.
	if(report->isEnemyInvading)
	{
		// If the combatant has more attack power than defense, they attack
		// instead of defending. This can sometimes happen when the combatant
		// has access to automated invaders or weapons with high attack power
		// and low defense power.
		if(report->attackPower > report->defensePower)
			return true;

		// Similarly to the previous case, the combatant might be better off
		// launching a counter-invasion if that would result in fewer overall
		// casualites than defending. For example, if the enemy has a lot of
		// automated attackers but few defenders or real crew, the combatant
		// might be able to push through and quickly conquer the enemy ship.
		if(report->expectedInvasionCasualties < report->expectedDefensiveCasualties)
			return true;
	}

	// Don't invade if it's not likely to be profitable.
	if(report->expectedInvasionProfit <= 0)
		return false;

	// During the Poised state, if the combatant using the Counter
	// strategy would take fewer (or inflict more) casualties by defending
	// than by attacking, they first create an opportunity for the enemy
	// to make a mistake. If the enemy takes the bait, the combatant
	// naturally delays their own invasion until the enemy withdraws or
	// the situation changes.
	if(
		defenseStrategy == DefenseStrategy::Counter
			&& latest.state == State::Poised
			&& (
				report->expectedInvasionCasualties > report->expectedDefensiveCasualties
					|| (
					report->defensePower / report->enemyAttackPower
						> report->attackPower / report->enemyDefensePower
				)
			)
	)
		return false;

	// Otherwise, the combatant will attack if they believe that they have
	// an acceptable chance of victory and that they won't take too many
	// casualties in the process. The definitions of "acceptable" and
	// "too many" vary greatly with the combatant's AttackStrategy.
	switch(attackStrategy)
	{
		case(AttackStrategy::Cautious):
			return report->invasionVictoryProbability > 0.99
				&& report->expectedInvasionCasualties < 0.5;
		case(AttackStrategy::Aggressive):
			return report->invasionVictoryProbability > 0.99
				&& report->expectedInvasionCasualties < ship->ExtraCrew();
		case(AttackStrategy::Reckless):
			return report->invasionVictoryProbability > 0.5;
		case(AttackStrategy::Fanatical):
			return report->invasionVictoryProbability > 0.01;
		default:
		{
			Logger::LogError(
				"BoardingCombat::ConsiderAttacking cannot handle AttackStrategy: "
					+ to_string(static_cast<int>(attackStrategy)) + ". Defaulting to false."
			);
			return false;
		}
	}
}



/**
 * Determines if the combatant is willing to capture the enemy ship.
 *
 * Note that this is specifically about using the Capture objective to
 * immediately take control of a conquered ship, not about invading it
 * with a view to capture it. That would be an Attack objective.
 *
 * @param report A shared pointer to the combatant's latest SituationReport.
 *
 * @return Whether or not the combatant is willing to capture the enemy ship.
 */
bool BoardingCombat::Combatant::ConsiderCapturing(const shared_ptr<SituationReport> &report) const
{
	if(!report->validObjectives->at(Action::Objective::Capture))
		return false;

	if(report->combatant->hasCaptureGoal)
		return report->expectedCaptureProfit > 0;
	else
		return report->expectedCaptureProfit > report->expectedPlunderProfit;
}



/**
 * Determines if the combatant is willing to destroy the enemy ship.
 *
 * This helps keep the system clear of ships that are no longer useful
 * to the game, which aids performance and prevents the player and other
 * ships from trying to interact with them for no reason.
 *
 * @param report A shared pointer to the combatant's latest SituationReport.
 *
 * @return Whether or not the combatant is willing to capture the enemy ship.
 */
bool BoardingCombat::Combatant::ConsiderDestroying(const shared_ptr<SituationReport> &report) const
{
  return report->validObjectives->at(Action::Objective::Destroy)
		&& report->plunderOptions.empty();
}



/**
 * Determines if the combatant is willing to plunder the enemy ship.
 * This assumes that the combatant has already determined that they will
 * not attack the enemy, capture it, or attempt to self-destruct.
 *
 * @param report A shared pointer to the combatant's latest SituationReport.
 *
 * @return Whether or not the combatant is willing to plunder the enemy ship.
 */
bool BoardingCombat::Combatant::ConsiderPlundering(const shared_ptr<SituationReport>& report) const
{
	return report->validObjectives->at(Action::Objective::Plunder)
		&& report->expectedPlunderProfit > 0;
}



/**
 * Determines if the combatant is willing to self-destruct. This is a
 * last-ditch effort to prevent the enemy from capturing or plundering.
 *
 * This function's logic assumes that the combatant has already
 * determined that they are either not capable of attacking the enemy
 * or that they are not willing to do so.
 *
 * @param report A shared pointer to the combatant's latest SituationReport.
 *
 * @return Whether or not the combatant is willing to self-destruct.
 */
bool BoardingCombat::Combatant::ConsiderSelfDestructing(const shared_ptr<SituationReport> &report) const
{
	if(report->selfDestructProbability < 0.001)
		return false;

	switch(defenseStrategy)
	{
		case(DefenseStrategy::Deny):
			if(report->enemyValidObjectives->at(Action::Objective::Plunder))
				return true;
			return report->isEnemyInvading
				&& report->selfDestructProbability > report->defensiveVictoryProbability;
		case(DefenseStrategy::Counter):
			return report->isEnemyInvading
				&& report->selfDestructProbability > report->defensiveVictoryProbability
				&& report->defensiveVictoryProbability < 0.1;
		case(DefenseStrategy::Repel):
			return report->isEnemyInvading
				&& report->selfDestructProbability > report->defensiveVictoryProbability
				&& report->defensiveVictoryProbability < 0.1;
		default:
		{
			Logger::LogError(
				"BoardingCombat::ConsiderSelfDestructing cannot handle DefenseStrategy: "
					+ to_string(static_cast<int>(defenseStrategy)) + ". Defaulting to false."
			);
			return false;
		}
	}
}



/**
 * @return The number of crew members aboard the combatant's ship.
 */
int BoardingCombat::Combatant::Crew() const
{
	return ship->Crew();
}



const string &BoardingCombat::Combatant::CrewDisplayName(bool startOfSentence)
{
  return startOfSentence ? crewDisplayNameStartOfSentence : crewDisplayNameMidSentence;
}



/**
 * Determines the Activity that the combatant should attempt to perform
 * during the next turn, based on their current situation and goal.
 *
 * @param report A shared pointer to the combatant's latest SituationReport.
 *
 * @return An Activity for combatant to attempt during the next turn.
 */
Boarding::Action::Activity BoardingCombat::Combatant::DetermineIntent(const shared_ptr<SituationReport> &report) const
{
	switch (goal)
	{
		case Ship::BoardingGoal::CAPTURE:
			return DetermineCaptureIntent(report);
		case Ship::BoardingGoal::PLUNDER:
			return DeterminePlunderIntent(report);
		default:
			return DetermineDefaultIntent(report);
	}
}



/**
 * Determines the Activity that the combatant should attempt to perform
 * during the next turn if their goal is to capture the enemy.
 *
 * @param report A shared pointer to the combatant's latest SituationReport.
 *
 * @return An Activity for combatant to attempt during the next turn.
 */
Boarding::Action::Activity BoardingCombat::Combatant::DetermineCaptureIntent(const shared_ptr<SituationReport> &report) const
{
	Action::Activity intent = Action::Activity(Action::Objective::Null, false);

	if(ConsiderCapturing(report))
		intent.objective = Action::Objective::Capture;
	else if(ConsiderAttacking(report))
		intent.objective = Action::Objective::Attack;
	else if(ConsiderSelfDestructing(report))
		intent.objective = Action::Objective::SelfDestruct;
	else if(report->validObjectives->at(Action::Objective::Leave))
		intent.objective = Action::Objective::Leave;
	else if(report->validObjectives->at(Action::Objective::Defend))
		intent.objective = Action::Objective::Defend;

	return intent;
}



/**
 * Determines the Activity that the combatant should attempt to perform
 * during the next turn if they lack a goal with more specific behaviour.
 *
 * @param report A shared pointer to the combatant's latest SituationReport.
 *
 * @return An Activity for combatant to attempt during the next turn.
 */
Boarding::Action::Activity BoardingCombat::Combatant::DetermineDefaultIntent(const shared_ptr<SituationReport> &report) const
{
	Action::Activity intent = Action::Activity(Action::Objective::Null, false);

	if(ConsiderCapturing(report))
		intent.objective = Action::Objective::Capture;
	else if(ConsiderAttacking(report))
		intent.objective = Action::Objective::Attack;
	else if(ConsiderSelfDestructing(report))
		intent.objective = Action::Objective::SelfDestruct;
	else if(ConsiderPlundering(report))
	{
		intent.objective = Action::Objective::Plunder;
		intent.details = make_tuple<int, int>(-1, -1); // Signals the Raid variant.
	}
	else if(ConsiderDestroying(report))
		intent.objective = Action::Objective::Destroy;
	else if(report->validObjectives->at(Action::Objective::Leave))
		intent.objective = Action::Objective::Leave;
	else if(report->validObjectives->at(Action::Objective::Defend))
		intent.objective = Action::Objective::Defend;

	return intent;
}



/**
 * Determines the Activity that the combatant should attempt to perform
 * during the next turn if their goal is to plunder the enemy.
 *
 * @param report A shared pointer to the combatant's latest SituationReport.
 *
 * @return An Activity for combatant to attempt during the next turn.
 */
Boarding::Action::Activity BoardingCombat::Combatant::DeterminePlunderIntent(const shared_ptr<SituationReport> &report) const
{
	Action::Activity intent = Action::Activity(Action::Objective::Null, false);

	if(ConsiderDestroying(report))
		intent.objective = Action::Objective::Destroy;
	else if(report->isPlunderFinished && report->validObjectives->at(Action::Objective::Leave))
		intent.objective = Action::Objective::Leave;
	else if(ConsiderAttacking(report))
		intent.objective = Action::Objective::Attack;
	else if(ConsiderSelfDestructing(report))
		intent.objective = Action::Objective::SelfDestruct;
	else if(report->validObjectives->at(Action::Objective::Plunder))
	{
		intent.objective = Action::Objective::Plunder;
		intent.details = make_tuple<int, int>(-1, -1); // Signals the Raid variant.
	}
	else if(report->validObjectives->at(Action::Objective::Leave))
		intent.objective = Action::Objective::Leave;
	else if(report->validObjectives->at(Action::Objective::Defend))
		intent.objective = Action::Objective::Defend;

	return intent;
}



/**
 * During a Turn, a combatant's Activity might immediately change the
 * state of the combat. Since this can make the other combatant's
 * Activity invalid, we need to check it after each one is performed.
 *
 * Intended for use during the resolution of a Turn. The state returned
 * by this function is not guaranteed to be the final state of the
 * Turn, since it might change again in response to the enemy's Activity
 * or during the process of finalising the Turn.
 *
 * @param state The state of the combat at this point in the Turn.
 * @param actual The actual activity that this combatant is taking.
 * @param enemyIntent The intent of the enemy combatant.
 *
 * @return The state of the combat immediately following this
 * 	combatant's Activity having been performed.
 */
Boarding::State BoardingCombat::Combatant::MaybeChangeState(
	State state,
	Action::Activity actual,
	const Action::Activity enemyIntent
) const
{
	if(actual.objective == Action::Objective::Attack)
	{
		bool enemyAttacking = enemyIntent.objective == Action::Objective::Attack;
		bool isInvading = isBoarder
			? state == State::BoarderInvading
			: state == State::TargetInvading;
		bool enemyInvading = isBoarder
			? state == State::TargetInvading
			: state == State::BoarderInvading;

		if(isInvading)
			return state;

		if(enemyInvading)
			return enemyAttacking ? state : State::Poised;

		if(state == State::Isolated && isBoarder)
			return State::Poised;

		if(state == State::Poised || state == State::Withdrawing)
			return enemyAttacking
				? State::Poised
				: isBoarder
					? State::BoarderInvading
					: State::TargetInvading;

		return state;
	}

	if(actual.objective == Action::Objective::SelfDestruct)
	  return State::Ended;

	if(actual.objective == Action::Objective::Capture)
		return State::Ended;

	if(actual.objective == Action::Objective::Destroy)
		return State::Ended;

	if(actual.objective == Action::Objective::Leave)
		return State::Ended;

	return state;
}



/**
 * During a Turn, a combatant's Activity might immediately change the
 * negotiation status. Since this can make the other combatant's
 * Activity invalid, we need to check it after each one is performed.
 *
 * Intended for use during the resolution of a Turn. The negotiation
 * status returned by this function is not guaranteed to be the final
 * negotation status of the Turn, since it might change again in
 * response to the enemy's Activity or during the process of finalising
 * the Turn.
 */
Boarding::Negotiation BoardingCombat::Combatant::MaybeChangeNegotiation(
	Negotiation negotiation,
	Action::Activity actual
) const
{
  if(actual.objective == Action::Objective::Negotiate)
		return Negotiation::Active;

	if(actual.objective == Action::Objective::Reject)
		return isBoarder
			? Negotiation::BoarderRejected
			: Negotiation::TargetRejected;

	return negotiation;
}



/**
 * @return The number of casualties that this combatant expects to
 * 	suffer if they invade the enemy ship.
 */
double BoardingCombat::Combatant::ExpectedInvasionCasualties(int enemyDefenders) const
{
  return isBoarder
		? odds->BoarderInvasionCasualties(Invaders(), enemyDefenders)
		: odds->TargetInvasionCasualties(Defenders(), enemyDefenders);
}



/**
 * Calculates the the expected financial gain from capturing this
 * combatant's ship, minus the likely financial loss from the expected
 * casualties or possible failure; either during the boarding action or
 * through the loss of the ship post-capture.
 *
 * Like any callous evaluation of the value of a sapient life, this
 * process is not especially accurate. However, it's a useful estimate
 * for the AI to use when deciding whether or not to attempt a capture.
 *
 * @param expectedInvasionCasualties The expected number of casualties that
 * 	the considering ship is likely to suffer if it invades this ship.
 * @param victoryOdds The odds of the invasion succeeding.
 *
 * @return The expected financial impact of capturing this ship.
 */
int64_t BoardingCombat::Combatant::ExpectedCaptureProfit(
	double expectedInvasionCasualties,
	double victoryOdds
)
{
	return captureValue * victoryOdds
		- expectedInvasionCasualties * expectedCostPerBoardingCasualty
		- expectedPostCaptureCasualtyCosts;
}



/**
 * Calculates the the expected financial gain from invading and conquering
 * this combatant, factoring in the probability of success and whether
 * the combatant's overall goal is to capture the ship or plunder it.
 *
 * @param enemy The combatant that is considering invading this ship.
 * @param expectedCaptureProfit The expected financial gain from capturing
 * 	this ship, assuming that the fleet is able to claim all of that plunder.
 * @param expectedProtectedPlunderProfit The expected financial gain from
 * 	gaining access to the protected plunder on this combatant's ship,
 * 	minus the likely financial loss from the expected casualties or
 *
 */
int64_t BoardingCombat::Combatant::ExpectedInvasionProfit(
	const shared_ptr<Combatant> &enemy,
	int64_t expectedCaptureProfit,
	int64_t expectedPlunderProfit,
	int64_t expectedProtectedPlunderProfit
)
{
  switch(enemy->GetShip()->GetBoardingGoal(true))
	{
		case Ship::BoardingGoal::CAPTURE:
			return expectedCaptureProfit;
		case Ship::BoardingGoal::PLUNDER:
			return expectedProtectedPlunderProfit;
		case Ship::BoardingGoal::CAPTURE_MANUALLY:
			return max(expectedCaptureProfit, expectedProtectedPlunderProfit);
		case Ship::BoardingGoal::PLUNDER_MANUALLY:
			return max(expectedCaptureProfit, expectedProtectedPlunderProfit);
		default:
			// Represents the case where the enemy has no specific capture or
			// plunder goal, but might invade the enemy anyway.
			// One possible reason for this is that the combatant has been
			// boarded and is considering a counter-invasion.
			return max(
				expectedCaptureProfit,
				// If the enemy is not the boarder, they can only plunder if
				// they perform a successful counter-invasion. This means that
				// their expected profit from invading is based on the total
				// plunder profit instead of just that of the protected items.
				enemy->IsBoarder()
					? expectedProtectedPlunderProfit
					: expectedPlunderProfit
			);
	}
}



/**
 * Calculates the the expected financial gain from plundering this
 * combatant's ship, based on the total value of the plunder that is
 * currently available to the enemy combatant.
 */
int64_t BoardingCombat::Combatant::ExpectedPlunderProfit()
{
	return plunderSession->ExpectedTotalRaidValue();
}



/**
 * Calculates the the expected financial gain from gaining access to the
 * protected plunder on this combatant's ship, minus the likely financial
 * loss from the expected casualties or possible failure.
 *
 * Like any callous evaluation of the value of a sapient life, this
 * process is not especially accurate. However, it's a useful estimate
 * for the AI to use when deciding whether or not to attempt a capture.
 *
 * @param expectedInvasionCasualties The expected number of casualties that
 * 	the considering ship is likely to suffer if it invades this ship.
 * @param victoryOdds The odds of the invasion succeeding.
 *
 * @return The expected financial impact of invading the ship for plunder,
 * 	assuming that the fleet is able to claim all of that plunder.
 */
int64_t BoardingCombat::Combatant::ExpectedProtectedPlunderProfit(
	double expectedInvasionCasualties,
	double victoryOdds
)
{
  return protectedPlunderValue * victoryOdds
		- expectedInvasionCasualties * expectedCostPerBoardingCasualty;
}



/**
 * @return How powerful the combatant is when attacking.
 */
double BoardingCombat::Combatant::AttackPower() const
{
  return isBoarder
		? odds->BoarderAttackPower(Invaders())
		:	odds->TargetAttackPower(Defenders());
}



/**
 * @return How powerful the combatant is when defending.
 */
double BoardingCombat::Combatant::DefensePower() const
{
  return isBoarder
		? odds->BoarderDefensePower(Defenders())
		:	odds->TargetDefensePower(Invaders());
}


/**
 * Activating a self-destruct system requires the following:
 *
 * 1. The presence of a self-destruct system, which is a ship attribute.
 *
 * 2. The combatant must win a power roll to interact with the system
 * 	while the enemy attempts to jam, sabotage, or otherwise prevent it.
 *
 * 3. The combatant must roll a percentage that is lower than their
 * 	"self destruct" attribute. This models how difficult and
 * 	time-consuming the system is to trigger, since it most likely has
 * 	safety checks to prevent accidental or unauthorised activation.
 *
 * @return The probability that this combatant can successfully activate
 * 	its self-destruct system during the next turn.
 */
double BoardingCombat::Combatant::SelfDestructProbability(const shared_ptr<Combatant> &enemy) const
{
	double attribute = GetShip()->Attributes().Get("self destruct");

	if(attribute <= 0.0)
		return 0.0;

	// The power of this combatant when activating the self-destruct system.
	double power = ActionPower(Action::Objective::SelfDestruct);

	// We assume here that the enemy is taking an aggressive action, since
	// this function is mostly used by the enemy to determine whether or
	// not to risk invading.
	//
	// If the enemy defends instead, their power will probably be higher,
	// which would reduce the activation probability. This is not likely
	// to matter, since this combatant has no reason to self-destruct if
	// they are not being invaded or plundered.
	double totalPower = power + enemy->AttackPower();

	return attribute * (power / totalPower);
}



/**
 * If this combatant successfully activates its self-destruct system
 * while being invaded, it can inflict additional casualties on the
 * enemy combatant. This happens because the enemy invaders are caught
 * in the blast when the ship explodes.
 *
 * Mechanically, the additional casualties occur due to two factors:
 *
 * 1. Each Turn, each combatant contributes a number of casualty rolls
 * 	to the total based on how many invaders or defenders they have. For
 * 	each roll, the combatant who loses the roll suffers one casualty.
 * 	If the combatant succeeds at self-destructing, they contribute twice
 * 	as many casualty rolls to the total for the turn.
 *
 * 2. The casualty rolls themselves are a contest between the power of
 * 	each combatant. A self-destructing combatant receives a multiplier
 * 	to their power for the remainder of the turn, which increases the
 * 	number of rolls that they are likely to win. The multiplier itself
 * 	is defined in the game rules.
 *
 * @param enemy An enemy combatant that is invading this combatant when
 * 	the self-destruct system is activated.
 *
 * @return The total number of expected casualties that the enemy will
 * 	suffer if this combatant self-destructs during the next turn.
 * 	This includes both the normal casualties for the turn and the
 * 	additional casualties from the explosion.
 */
double BoardingCombat::Combatant::ExpectedSelfDestructCasualtiesInflicted(const shared_ptr<Combatant> &enemy) const
{
	double power = CasualtyPower(Action::Objective::SelfDestruct);

  return CasualtyRolls(
		isBoarder ? State::TargetInvading : State::BoarderInvading,
		Negotiation::NotAttempted,
		Action::Objective::SelfDestruct
	)
		* (power / (power + enemy->AttackPower()));
}



/**
 * @return The total number of defenders that the combatant has available.
 * 	This includes both crew members and automated defenders.
 */
int BoardingCombat::Combatant::Defenders() const
{
  return ship->Defenders();
}



/**
 * @return The total number of invaders that the combatant has available.
 * 	This includes both crew members and automated invaders.
 */
int BoardingCombat::Combatant::Invaders() const
{
  return ship->Invaders();
}



/**
 * @return A shared pointer to this combatant's ship.
 */
shared_ptr<Ship> &BoardingCombat::Combatant::GetShip()
{
  return ship;
}



/**
 * @return A const shared pointer to this combatant's ship.
 */
const shared_ptr<Ship> &BoardingCombat::Combatant::GetShip() const
{
  return ship;
}



/**
 * @return Whether or not this combatant is the boarder in the combat.
 */
bool BoardingCombat::Combatant::IsBoarder() const
{
  return isBoarder;
}



/**
 * @return Whether or not the combatant is directly controlled by the
 * 	player, either because it is the player's flagship or because they
 * 	have chosen to control it directly via the Boarding Panel.
 */
bool BoardingCombat::Combatant::IsPlayerControlled() const
{
  return isPlayerControlled;
}



/**
 * @return Whether or not this combatant has finished plundering the enemy.
 */
bool BoardingCombat::Combatant::IsPlunderFinished() const
{
  return plunderSession->IsFinished();
}



/**
 * @return The Plunder::Session that this combatant is using to plunder
 * 	the enemy.
 */
shared_ptr<Plunder::Session> &BoardingCombat::Combatant::PlunderSession()
{
  return plunderSession;
}



/**
 * @return A vector of Plunder items that the combatant can attempt to
 * 	take from the enemy vessel. The list includes all of the enemy's
 * 	outfits and cargo, but some of the items might not be accessible
 * 	until the enemy combatant has been conquered.
 */
const vector<shared_ptr<Plunder>> &BoardingCombat::Combatant::PlunderOptions() const
{
  return plunderSession->RemainingPlunder();
}



/**
 * @return The odds that the combatant's ship will survive post-capture,
 * 	based on its ship category. Not determined by any kind of scientific
 * 	process, but just a rough guess based on how easy it is to bring
 * 	that kind of ship safely to port when it's already heavily damaged.
 */
double BoardingCombat::Combatant::PostCaptureSurvivalOdds() const
{
  return postCaptureSurvivalOdds;
}



/**
 * Calculates the power of the combatant when attempting to perform an
 * Activity with the given Objective.
 * The power is based on the number of invaders or defenders available to
 * the combatant, and whether or not the action is considered defensive.
 *
 * @param objective The Objective to determine the power for.
 *
 * @return The power of the combatant when performing the specified action.
 */
double BoardingCombat::Combatant::ActionPower(Action::Objective objective) const
{
  if(Action::isObjectiveDefensive.at(objective))
		return DefensePower();
	else
		return AttackPower();
}



/**
 * Calculates the power of the combatant when determining the casualties
 * that are inflicted by an Action with the given Objective.
 *
 * The power is based on the number of invaders or defenders available to
 * the combatant, and whether or not the Objective.
 *
 * In the case of a SelfDestruct Objective, the casualty power is
 * multiplied by the self destruct casualty power multiplier defined in
 * the game rules.
 *
 * @param objective The Objective of the combatant's performed Activity.
 *
 * @return The effective power of the combatant when casualties are
 * 	rolled for the turn.
 */
double BoardingCombat::Combatant::CasualtyPower(Action::Objective objective) const
{
	double multiplier = 1.0;

	if(objective == Action::Objective::SelfDestruct)
		multiplier *= GameData::GetGamerules().BoardingSelfDestructCasualtyPowerMultiplier();

  if(Action::isObjectiveDefensive.at(objective))
		return DefensePower() * multiplier;
	else
		return AttackPower() * multiplier;
}



/**
 * Used to determine the Action that a combatant takes during a turn.
 * While each combatant indicates an intended Activity to perform, that
 * Activity might be prevented by the other combatant. If that happens,
 * the combatant will instead perform a different Activity.
 *
 * This can happen for two reasons:
 *
 * 1. The enemy combatant takes an action that changes the state or the
 * 	negotiation status of the combat, which then makes the intended
 * 	Activity invalid.
 *
 * 2. The combatant's intended Activity might require a successful power
 *	roll to perform, and they might fail that roll.
 *
 * This function returns an Action, but does not apply its effect to the
 * boarding combat or its combatants.
 *
 * @param combat A reference to the BoardingCombat that is taking place.
 * @param state The state of the combat at the time the action is attempted.
 * 	This might be different from the state as of the most recent turn,
 * 	since the enemy might have taken a binary action that changes it.
 * @param negotiation The state of the negotiation at the time the action
 * 	is attempted. As with state, this might be different from last turn.
 * @param intent The Activity that the combatant is attempting.
 * @param enemyIntent The Activity that the enemy combatant is attempting.
 * @param enemyPower The enemy combatant's current action power.
 *
 * @return A Boarding::Action object containing the Activity that the
 * 	combatant intended, the Activity that they actually performed, and
 * 	information about the effect of that Activity.
 */
Boarding::Action BoardingCombat::Combatant::AttemptAction(
	const BoardingCombat &combat,
	State state,
	Negotiation negotiation,
	Action::Activity intent,
	Action::Activity enemyIntent,
	double enemyPower
)
{
	int casualtyRolls = 0;

	// First check if the combatant is still able to take the action.
	// The action might have been valid when the combatant decided to
	// take it, but it could have become invalid by the time the action
	// is actually attempted. For example, the enemy might have already
	// ended the combat by activating their self-destruct system.
	if(!IsValidActivity(intent, ValidObjectives(state, negotiation), isBoarder))
	{
		return Action(
			intent,
			Action::Activity(Action::Objective::Null, false),
			// While the combatant's now invalid objective does not contribute
			// to the number of casualty rolls, they still use it when
			// determining their casualty power against other combatants.
			Action::Effect(state, negotiation, intent.objective, casualtyRolls)
		);
	}

	double power = ActionPower(intent.objective);
	double totalPower = power + enemyPower;

	bool wonPowerRoll = Random::Real() * totalPower <= power;

	bool canPerformIntent = true;

	if(intent.objective == Action::Objective::Defend)
		canPerformIntent = enemyIntent.objective == Action::Objective::Attack;
	else if(intent.objective == Action::Objective::Negotiate)
		canPerformIntent = combat.IsLanguageShared();
	else if(intent.objective == Action::Objective::Resolve)
		canPerformIntent = enemyIntent.objective != Action::Objective::Reject;
	else if(intent.objective == Action::Objective::SelfDestruct)
		canPerformIntent = wonPowerRoll && Random::Real() < ship->Attributes().Get("self destruct");

	Action::Activity actual = canPerformIntent
		? intent
		: Action::Activity(Action::Objective::Null, false);

	casualtyRolls = CasualtyRolls(state, negotiation, actual.objective);

	Action::Effect effect(
		MaybeChangeState(state, actual, enemyIntent),
		MaybeChangeNegotiation(negotiation, actual),
		actual.objective,
		casualtyRolls
	);

	return Action(intent, actual, effect);
}



/**
 * @return A shared pointer to a map of each possible Objective and
 * 	whether or not the combatant is able to attempt it based on the
 * 	given state and negotiation status.
 */
const shared_ptr<Boarding::Action::ObjectiveCondition> BoardingCombat::Combatant::ValidObjectives(
	State state,
	Negotiation negotiation
) const
{
	return Action::ValidObjectives(state, negotiation, isBoarder);
}



// --- End of BoardingCombat::Combatant class implementation ---



// --- Start of BoardingCombat::Offer class implementation ---


BoardingCombat::Offer::Offer(Offer::Terms &terms) :
	terms(terms)
{
}


/**
 * Adds a new Term to the Offer, or amends an existing Term with new details.
 *
 * @param term The Term to add or amend.
 * @param details The details to assign to the Term.
 *
 * @return A reference to the Offer object.
 */
BoardingCombat::Offer &BoardingCombat::Offer::AddOrAmendTerm(
	Term term,
	TermDetails details
) {
	terms->insert_or_assign(term, details);
	return *this;
}



/**
 * Removes a Term from the Offer.
 * If the Term does not exist, this function will have no effect.
 * If the Term exists, it will be removed from the Offer.
 *
 * @param term The Term to remove.
 *
 * @return A reference to the Offer object.
 */
BoardingCombat::Offer &BoardingCombat::Offer::RemoveTerm(Term term)
{
  terms->erase(term);
	return *this;
}



const BoardingCombat::Offer::Terms &BoardingCombat::Offer::GetTerms() const
{
  return terms;
}



const bool BoardingCombat::Offer::HasTerm(Term term) const
{
  return terms->find(term) != terms->end();
}



// --- End of BoardingCombat::Offer class implementation ---



// --- Start of BoardingCombat::SituationReport class implementation ---



/**
 * When a combatant is deciding what action to take during the next
 * turn, they need a lot of information about the situation as it stands.
 * This class provides a way to encapsulate all of that information in
 * a single object and calculate it once per turn, rather than having
 * to recalculate it every time the combatant needs refer to it.
 *
 * This is especially helpful for the BoardingPanel, as it removes the
 * need for that UI component to reach into the BoardingCombat system
 * and perform these kinds of calculations.
 *
 * It also boosts performance by keeping the calculations out of the
 * BoardingPanel::Draw() function, which is called every frame.
 *
 * @param combatant The combatant whose point of view this report is from.
 * @param enemy The enemy of the combatant.
 * @param turn The latest turn of the combat.
 * @param previousReport The SituationReport from the previous turn.
 *
 * @return A SituationReport object that describes the combat as of the
 * 	latest turn, from the perspective of one of the combatants.
 */
BoardingCombat::SituationReport::SituationReport(
	const shared_ptr<Combatant> &combatant,
	const shared_ptr<Combatant> &enemy,
	const Turn &turn,
	const shared_ptr<SituationReport> &previousReport
) :
	combatant(combatant),
	enemy(enemy),
	turn(turn),
	previousReport(previousReport),
	ship(combatant->GetShip()),
	enemyShip(enemy->GetShip()),
	isBoarder(combatant->IsBoarder()),
	actedFirst(isBoarder && turn.boarderActionIndex < turn.targetActionIndex),
	isConquered(
		isBoarder
			? turn.state == State::TargetVictory
			: turn.state == State::BoarderVictory
	),
	isEnemyConquered(
		isBoarder
			? turn.state == State::BoarderVictory
			: turn.state == State::TargetVictory
	),
	isEnemyInvading(
		isBoarder
			? turn.state == State::TargetInvading
			: turn.state == State::BoarderInvading
	),
	latestAction(
		isBoarder
			? turn.actions[turn.boarderActionIndex]
			: turn.actions[turn.targetActionIndex]
	),
	enemyLatestAction(
		isBoarder
			? turn.actions[turn.targetActionIndex]
			: turn.actions[turn.boarderActionIndex]),
	invaders(combatant->Invaders()),
	defenders(enemy->Defenders()),
	crew(combatant->Crew()),
	enemyInvaders(enemy->Invaders()),
	enemyDefenders(combatant->Defenders()),
	enemyCrew(enemy->Crew()),
	cargoSpace(combatant->GetShip()->Cargo().Free()),
	enemyCargoSpace(enemy->GetShip()->Cargo().Free()),
	oddsAnalysis(),
	attackPower(combatant->AttackPower()),
	defensePower(combatant->DefensePower()),
	enemyAttackPower(enemy->AttackPower()),
	enemyDefensePower(enemy->DefensePower()),
	attackingTotalPower(attackPower + enemyDefensePower),
	defendingTotalPower(defensePower + enemyAttackPower),
	minimumTurnsToVictory(invaders / (
		combatant->CasualtyRolls(
			isBoarder ? State::BoarderInvading : State::TargetInvading,
			Negotiation::NotAttempted,
			Action::Objective::Attack
		) * (attackPower / attackingTotalPower))),
	minimumTurnsToDefeat(enemyInvaders / (
		enemy->CasualtyRolls(
			isBoarder ? State::TargetInvading : State::BoarderInvading,
			Negotiation::NotAttempted,
			Action::Objective::Attack
		) * (enemyAttackPower / defendingTotalPower))),
	selfDestructProbability(combatant->SelfDestructProbability(enemy)),
	enemySelfDestructProbability(enemy->SelfDestructProbability(combatant)),
	enemyCumulativeSelfDestructProbability(1 - pow(1 - enemySelfDestructProbability, minimumTurnsToVictory)),
	enemySelfDestructCasualtyPower(enemy->CasualtyPower(Action::Objective::SelfDestruct)),
	expectedSelfDestructCasualties(combatant->ExpectedSelfDestructCasualtiesInflicted(enemy)),
	expectedInvasionCasualties(
		combatant->ExpectedInvasionCasualties(enemyDefenders)
		+ enemyCumulativeSelfDestructProbability * expectedSelfDestructCasualties
	),
	expectedDefensiveCasualties(
		combatant->ExpectedDefensiveCasualties(enemyInvaders)
	),
	invasionVictoryProbability(
		combatant->InvasionVictoryProbability(enemyDefenders) * (1 - enemyCumulativeSelfDestructProbability)
	),
	defensiveVictoryProbability(
		combatant->DefensiveVictoryProbability(enemyInvaders)
	),
	postCaptureSurvivalProbability(
		combatant->PostCaptureSurvivalOdds()
	),
	expectedCaptureProfit(
		enemy->ExpectedCaptureProfit(expectedInvasionCasualties, invasionVictoryProbability)
	),
	expectedPlunderProfit(enemy->ExpectedPlunderProfit()),
	expectedProtectedPlunderProfit(
		enemy->ExpectedProtectedPlunderProfit(expectedInvasionCasualties, invasionVictoryProbability)
	),
	expectedInvasionProfit(
		enemy->ExpectedInvasionProfit(
			combatant,
			expectedCaptureProfit,
			expectedPlunderProfit,
			expectedProtectedPlunderProfit
		)
	),
	isPlunderFinished(combatant->IsPlunderFinished()),
	plunderOptions(combatant->PlunderOptions()),
	validObjectives(combatant->ValidObjectives(turn.state, turn.negotiation)),
	enemyValidObjectives(enemy->ValidObjectives(turn.state, turn.negotiation))
{
}




// --- End of BoardingCombat::SituationReport class implementation ---



// --- Start of BoardingCombat static members and functions ---



/**
 * Capturing a ship is especially risky. Not only can crew members be
 * killed during the boarding action, a freshly captured ship is heavily
 * damaged and might be destroyed before it can be brought to safety.
 * This is especially true for ships that are slow, fragile, or both.
 *
 * For this reason, the AI needs to be able to consider the likelihood
 * of a ship being destroyed post-capture. This allows it to factor that
 * possibility into its risk calculations when deciding whether or not
 * capturing a given ship might be financially worthwhile.
 *
 * This could be defined in the game rules instead of here, which would
 * allow people to tweak the AI's decision making to their liking.
 */
const map<string, double> BoardingCombat::postCaptureSurvivalOddsByCategory = {
	{"Transport", 0.5},
	{"Space Liner", 0.6},
	{"Light Freighter", 0.5},
	{"Heavy Freighter", 0.5},
	{"Utility", 0.8},
	{"Interceptor", 0.5},
	{"Light Warship", 0.6},
	{"Medium Warship", 0.7},
	{"Heavy Warship", 0.8},
	{"Superheavy", 0.8},
	{"Fighter", 0.9},
	{"Drone", 0.9},
	{"UnknownCategory", 0.5}
};



/**
 * Builds a string for display in the BoardingPanel whenever we need to
 * refer to the crew members that are carrying out a boarding action.
 *
 * For instance: "Your crew", "Republic marines", "Pirate crew", etc.
 *
 * @param ship The ship whose crew members we are referring to.
 * @param startOfSentence Whether or not the name is at the start of a
 * 	sentence, and therefore needs to be capitalised. This is primarily
 * 	just so that we know whether or not to capitalise the word "your".
 *
 * @return A string that describes the crew members of a given ship
 * 	in a way that is suitable for display in the BoardingPanel.
 */
string BoardingCombat::BuildCrewDisplayName(shared_ptr<Ship> &ship, bool startOfSentence)
{
	string displayName = "";

	if(ship->IsYours())
		displayName += startOfSentence ? "Your " : "your ";
	else
		// Government names are already capitalised, so no need to check startOfSentence.
		displayName += ship->GetGovernment()->GetName() + " ";

	displayName += ship->ExtraCrew() ? "marines" : "crew";

	return displayName;
}



// --- End of BoardingCombat static members and functions ---



// --- Start of BoardingCombat class implementation ---



/**
 * Constructor for BoardingCombat.
 *
 * Make an instance of this class each time one ship boards another.
 * The BoardingCombat object will track the progress of the boarding
 * combat, maintaining a turn-by-turn history of each action taken by
 * the boarder and the target.
 *
 * When the player's flagship is involved in a boarding combat, or when
 * the player specifies that they want to control the combat directly,
 * we need to raise the BoardingPanel so that the player can make
 * decisions about how to proceed.
 *
 * The BoardingCombat object will automatically make decisions for any
 * combatants that are not directly controlled by the player via the
 * BoardingPanel. This allows the player to command their escorts to
 * capture enemy ships without having to micromanage them, as long as
 * they are willing to accept the AI's decisions. It also allows
 * non-player ships to board the player's escorts, and one another, and
 * engage in boarding combat just like the player can. This makes
 * defensive anti-boarding outfits much more important for the player to
 * consider for their fleet, where previously they were only useful as a
 * way to make capturing more difficult for the player.
 *
 * Prior to this change, the player's flagship was the only entity in
 * the game that could capture other ships. This not only gave the
 * player an unfair advantage, but also forced them to fly some kind of
 * lumbering troop transport if they wanted to be able to capture ships
 * effectively. Since capturing other ships was the most profitable way
 * to benefit from a combat-heavy playstyle, this made it difficult for
 * the player to actively participate in the space battles that their
 * strategy was build around. With this change, the player can now
 * capture ships with any ship in their fleet, allowing them to
 * personally pilot whatever ship they like and still reap the spoils of
 * their victories.
 *
 * Prior to the crew system upgrades that introduced profit sharing and
 * death payments, this would have wildly unbalanced the game in the
 * player's favour because they could simply throw crew members at any
 * enemy ship until they eventually captured it. Now the player has to
 * pay their crew members a share of the fleet's profits, and those
 * profit shares are quite significant on troop transports due to their
 * high crew counts. Paired with the death benefits and extra profit
 * shares incurred when the player's crew are slain, the player is no
 * longer financially motivated to repeatedly throw people's lives away
 * to steal ships. Instead, they are encouraged to keep their crew alive
 * and to capture ships only when their crew is well equipped to do so.
 *
 * In short, the "will I capture that ship" question is now all about
 * whether or not that ship will be worth the risk of people dying to
 * claim it, rather than whether or not the capture is possible.
 * Plus, since the target can now fight back more effectively, the
 * player must consider whether or not they can afford to lose any of
 * the ships that they send in to attempt the capture.
 *
 * @param player A reference to the PlayerInfo object.
 * @param boarderShip A shared pointer to the ship that is boarding the target ship.
 * @param targetShip A shared pointer to the ship that is being boarded.
 */
BoardingCombat::BoardingCombat(
	PlayerInfo &player,
	shared_ptr<Ship> &boarderShip,
	shared_ptr<Ship> &targetShip
) :
	player(player),
	boardingObjective(boarderShip->GetBoardingGoal(true)),
	usingBoardingPanel(
		boarderShip->IsPlayerFlagship()
		|| targetShip->IsPlayerFlagship()
		|| boardingObjective == Ship::BoardingGoal::CAPTURE_MANUALLY
		|| boardingObjective == Ship::BoardingGoal::PLUNDER_MANUALLY
	),
	odds(make_shared<BoardingOdds>(boarderShip, targetShip)),
	boarder(make_shared<BoardingCombat::Combatant>(
		boarderShip,
		targetShip,
		odds,
		true,
		usingBoardingPanel,
		player.Ships()
	)),
	target(make_shared<BoardingCombat::Combatant>(
		targetShip,
		boarderShip,
		odds,
		false,
		usingBoardingPanel,
		player.Ships()
	)),
	history(make_shared<History>())
{
	// Determine whether or not the combatants share a language, and can therefore negotiate.
	string boarderLanguage = boarderShip->GetGovernment()->Language();
	string targetLanguage = targetShip->GetGovernment()->Language();

	if(boarder->IsPlayerControlled())
		isLanguageShared = targetLanguage.empty() || player.Conditions().Get("language: " + targetLanguage);
	else if(target->IsPlayerControlled())
		isLanguageShared = boarderLanguage.empty() || player.Conditions().Get("language: " + boarderLanguage);
	else
		isLanguageShared = boarderLanguage == targetLanguage;

	history->push_back(make_shared<Turn>(boarder, target));
}



/**
 * At the end of the combat, or when one of the combatants is captured,
 * we need to tally the casualties and apply any consequences for them.
 *
 * Make sure that you call this before anything changes the number of
 * crew members that are on the player combatant's ship, as that will
 * result in an incorrect analysis of the casualties.
 *
 * @return Whether or not the consequences have now been applied.
 */
bool BoardingCombat::ApplyCasualtyConsequences()
{
  if(!pendingCasualtyConsequences)
		return true;

	auto playerCombatant = GetPlayerCombatant();

	// There aren't any consequences for casualties that occur during
	// boarding combats between non-player ships. If that ever changes,
	// this function will need to be updated to handle those consequences.
	if(!playerCombatant)
	{
		pendingCasualtyConsequences = false;
		return true;
	}

	auto casualtyAnalysis = playerCombatant->CasualtyAnalysis();

	ostringstream out;
	out << "During a boarding conflict involving " + playerCombatant->GetShip()->QuotedName() + " , " << Format::Number(casualtyAnalysis->casualtyCount) << " crew members were killed.";

	if(casualtyAnalysis->deathBenefits || casualtyAnalysis->deathShares)
		out << " You owe their estates ";

	if(casualtyAnalysis->deathBenefits)
	{
		out << Format::Credits(casualtyAnalysis->deathBenefits) << " credits in death benefits";
		player.Accounts().AddDeathBenefits(casualtyAnalysis->deathBenefits);

		if(casualtyAnalysis->deathShares)
			out << ", and ";
	}

	if(casualtyAnalysis->deathShares)
	{
		out << Format::Number(casualtyAnalysis->deathShares) << " extra shares in today's profits (if any).";
		player.Accounts().AddDeathShares(casualtyAnalysis->deathShares);
	}

	Messages::Add(out.str(), Messages::Importance::Highest);

	pendingCasualtyConsequences = false;
	return true;
}



/**
 * Determines the casualties that are suffered by each side during an
 * Action performed by one of the combatants.
 *
 * Only rolls the casualties for one Action, so it needs to be called
 * once for each Action that can produce casualties.
 *
 * This is not a pure function. Each time a casualty is rolled, it is
 * applied to the corresponding combatant. This is necessary because the
 * combatant's new crew count is used to determine their casualty power
 * for the subsequent roll.
 *
 * It also sets the combat's pendingCasualtyConsequences flag to true
 * if at least one casualty occurs.
 *
 * It does not directly alter the state of the combat, instead returning
 * a new state as part of the casualty report.
 *
 * @param state The current state of the boarding combat.
 * @param isBoarder Whether or not the Action is being performed by the
 * 	boarder.
 * @param boarderAction The action performed by the boarder.
 * @param targetAction The action performed by the target.
 *
 * @return A report of the casualties suffered by each side during the
 * 	Action, including the new state and negotiation status.
 */
BoardingCombat::CasualtyReport BoardingCombat::RollForCasualties(
	State state,
	bool isBoarder,
	const Action &boarderAction,
	const Action &targetAction
)
{
	CasualtyReport report(state, 0, 0);

	// Determine the number of casualty rolls that we need to make.
	int casualtyRolls = isBoarder
		? boarderAction.effect.casualtyRolls
		: targetAction.effect.casualtyRolls;

	if(casualtyRolls < 1)
		return report;

	Action::Objective boarderObjective = boarderAction.effect.casualtyObjective;
	Action::Objective targetObjective = targetAction.effect.casualtyObjective;

	// Cache the power of each side to avoid recalculating it unnecessarily.
	double boarderPower = boarder->CasualtyPower(boarderObjective);
	double targetPower = target->CasualtyPower(targetObjective);

	// Determine the number of casualties suffered by each side.
	for(int i = 0; i < casualtyRolls; ++i) {
		// Roll to determine which side suffers a casualty.
		bool isBoarderCasualty = Random::Real() * (boarderPower + targetPower) >= boarderPower;

		// Apply the casualty to the combatant who lost the roll.
		if(isBoarderCasualty) {
			report.boarderCasualties++;
			if(!boarder->ApplyCasualty(report.state == State::BoarderInvading)) {
				report.state = State::TargetVictory;
				break;
			}
			boarderPower = boarder->CasualtyPower(boarderObjective);
		} else {
			report.targetCasualties++;
			if(!target->ApplyCasualty(report.state == State::TargetInvading)) {
				report.state = State::BoarderVictory;
				break;
			}
			targetPower = target->CasualtyPower(targetObjective);
		}
	}

	pendingCasualtyConsequences = true;

	return report;
}



const shared_ptr<BoardingCombat::History> &BoardingCombat::GetHistory() const
{
  return history;
}



int BoardingCombat::CountInactiveFrames() const
{
  return history->size() * GameData::GetGamerules().BoardingInactiveFramesPerTurn();
}



const shared_ptr<BoardingCombat::Combatant> BoardingCombat::GetPlayerCombatant() const
{
  if(boarder->IsPlayerControlled())
		return boarder;
	else if(target->IsPlayerControlled())
		return target;
	else
		return nullptr;
}



const shared_ptr<BoardingCombat::Combatant> BoardingCombat::GetPlayerEnemy() const
{
  if(boarder->IsPlayerControlled())
		return target;
	else if(target->IsPlayerControlled())
		return boarder;
	else
		return nullptr;
}



bool BoardingCombat::IsLanguageShared() const
{
	return isLanguageShared;
}



/**
 * @return Whether or not the player's combatant has been conquered.
 */
bool BoardingCombat::IsPlayerConquered() const
{
  if(boarder->IsPlayerControlled())
		return GetHistory()->back()->state == State::TargetVictory;
	else if(target->IsPlayerControlled())
		return GetHistory()->back()->state == State::BoarderVictory;
	else
		return false;
}



/**
 * When a binary action succeds, it can have a variety of effects on the
 * combatants involved and may change the state of the combat.
 *
 * @param state The current state of the boarding combat.
 * @param actor The combatant that performed the action.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return The combat's new state and negotiation state.
 */
Boarding::Action::Result BoardingCombat::ApplyAction(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	switch(action.actual.objective)
	{
		case Action::Objective::Null: return ApplyNull(state, negotiation, isBoarder, action, enemyAction);

		case Action::Objective::Attack: return ApplyAttack(state, negotiation, isBoarder, action, enemyAction);
		case Action::Objective::Defend: return ApplyDefend(state, negotiation, isBoarder, action, enemyAction);
		case Action::Objective::Negotiate: return ApplyNegotiate(state, negotiation, isBoarder, action, enemyAction);
		case Action::Objective::Plunder: return ApplyPlunder(state, negotiation, isBoarder, action, enemyAction);
		case Action::Objective::Reject: return ApplyReject(state, negotiation, isBoarder, action, enemyAction);
		case Action::Objective::Resolve: return ApplyResolve(state, negotiation, isBoarder, action, enemyAction);
		case Action::Objective::SelfDestruct: return ApplySelfDestruct(state, negotiation, isBoarder, action, enemyAction);

		case Action::Objective::Capture: return ApplyCapture(state, negotiation, isBoarder, action, enemyAction);
		case Action::Objective::Destroy: return ApplyDestroy(state, negotiation, isBoarder, action, enemyAction);
		case Action::Objective::Leave: return ApplyLeave(state, negotiation, isBoarder, action, enemyAction);

		default:
			throw invalid_argument("Invalid action objective: " + Action::GetObjectiveName(action.actual.objective));
	}
}



/**
 * Apply the effects of a Null action to the combatants.
 *
 * This does not do anything, and just returns an empty Action::Result.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplyNull(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	int casualties = 0;
	int enemyCasualties = 0;

	return Action::Result(
		state,
		negotiation,
		casualties,
		enemyCasualties
	);
}



/**
 * Apply the effects of an Attack action to the combatants.
 *
 * This rolls for casualties and applies them to the combatants.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplyAttack(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	CasualtyReport report = RollForCasualties(state, isBoarder, action, enemyAction);

	return Action::Result(
		report.state,
		negotiation,
		isBoarder ? report.boarderCasualties : report.targetCasualties,
		isBoarder ? report.targetCasualties : report.boarderCasualties
	);
}



/**
 * Apply the effects of an Defend action to the combatants.
 *
 * If appropriate, this rolls for casualties and applies them to the
 * combatants.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplyDefend(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	CasualtyReport report = RollForCasualties(state, isBoarder, action, enemyAction);

	return Action::Result(
		report.state,
		negotiation,
		isBoarder ? report.boarderCasualties : report.targetCasualties,
		isBoarder ? report.targetCasualties : report.boarderCasualties
	);
}



/**
 * Apply the effects of an Negotiate action to the combatants.
 *
 * Taking a successful Negotiate action immediately changes the
 * negotiation status mid-turn, so we don't need to do that here.
 *
 * All that this really does it create an Action::Result object so that
 * we can follow a standard format for all of the Apply* functions.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplyNegotiate(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	int casualties = 0;
	int enemyCasualties = 0;

	return Action::Result(
		state,
		negotiation,
		casualties,
		enemyCasualties
	);
}



/**
 * Apply the effects of an Reject action to the combatants.
 *
 * Taking a successful Reject action immediately changes the
 * negotiation status mid-turn, so we don't need to do that here.
 *
 * All that this really does it create an Action::Result object so that
 * we can follow a standard format for all of the Apply* functions.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplyReject(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	int casualties = 0;
	int enemyCasualties = 0;

	return Action::Result(
		state,
		negotiation,
		casualties,
		enemyCasualties
	);
}



/**
 * Apply the effects of an Resolve action to the combatants.
 *
 * All that this really does it create an Action::Result object so that
 * we can follow a standard format for all of the Apply* functions.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplyResolve(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	int casualties = 0;
	int enemyCasualties = 0;

	return Action::Result(
		state,
		negotiation,
		casualties,
		enemyCasualties
	);
}



/**
 * Apply the effects of a successful Plunder action to the combatants.
 *
 * If the actor takes the last item from the enemy, this also destroys
 * their ship and ends the combat.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplyPlunder(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	Action::Result result(state, negotiation, 0, 0);

	auto actor = isBoarder ? boarder : target;
	auto enemy = isBoarder ? target : boarder;

	if(holds_alternative<tuple<int, int>>(action.actual.details))
	{
		auto [index, quantity] = get<tuple<int, int>>(action.actual.details);
		actor->PlunderSession()->Take(index, quantity);
	}
	else
		actor->PlunderSession()->Raid();

	// Taking the last item from the enemy also destroys their ship.
	if(enemy->PlunderSession()->RemainingPlunder().empty())
	{
		result.state = State::Ended;
		result.enemyCasualties = enemy->GetShip()->Crew();
		enemy->GetShip()->Destroy();
	}

	return result;
}



/**
 * Apply the effects of a successful SelfDestruct action to the combatants.
 *
 * This destroys the actor's ship and rolls for casualties.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplySelfDestruct(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	CasualtyReport report = RollForCasualties(state, isBoarder, action, enemyAction);

	auto actor = isBoarder ? boarder : target;
	auto enemy = isBoarder ? target : boarder;

	int casualties = actor->GetShip()->Crew() + (
		isBoarder
			? report.boarderCasualties
			: report.targetCasualties
	);

	if(casualties > 0)
		pendingCasualtyConsequences = true;

	actor->GetShip()->SelfDestruct();

	return Action::Result(
		State::Ended,
		negotiation,
		casualties,
		isBoarder ? report.targetCasualties : report.boarderCasualties
	);
}



/**
 * Apply the effects of a successful Capture action to the combatants.
 *
 * This takes ownership of the enemy ship, transfers crew members as
 * required, triggers various consequences, and outputs messages.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplyCapture(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	auto actor = isBoarder ? boarder : target;
	auto enemy = isBoarder ? target : boarder;

	auto enemyShip = enemy->GetShip();
	if(actor->IsPlayerControlled())
		enemyShip->GetGovernment()->Offend(ShipEvent::CAPTURE, enemyShip->CrewValue());

	// We need to do this before we transfer crew members to the other ship,
	// otherwise we won't be able to count the casualties properly.
	ApplyCasualtyConsequences();

	int transferredCrew = enemyShip->WasCaptured(actor->GetShip());
	int missingCrew = enemyShip->RequiredCrew() - enemyShip->Crew();

	if(actor->IsPlayerControlled())
	{
		ostringstream out;
		out << actor->GetShip()->QuotedName() << " has captured the ";
		out << enemyShip->DisplayModelName() << " " + enemyShip->QuotedName();
		out << ", transferring " << Format::Number(transferredCrew) << " crew members.";
		Messages::Add(out.str(), Messages::Importance::High);

		if(missingCrew)
		{
			out << enemyShip->QuotedName() + " needs " << Format::Number(missingCrew);
			out << " more crew members. You can order your fleet to distribute their extra crew by pressing " + Command::TRANSFER_CREW.KeyName() + ".";
			Messages::Add(out.str(), Messages::Importance::High);
		}
	}
	else if(enemy->IsPlayerControlled())
	{
		ostringstream out;
		out << "An enemy " + actor->GetShip()->DisplayModelName() + " " + actor->GetShip()->QuotedName() << " has boarded and captured your ";
		out << enemyShip->DisplayModelName() << " " + enemyShip->QuotedName();
		Messages::Add(out.str(), Messages::Importance::Highest);
	}

	int casualties = 0;
	int enemyCasualties = 0;

	return Action::Result(
		State::Ended,
		negotiation,
		casualties,
		enemyCasualties
	);
}



/**
 * Apply the effects of a successful Destroy action to the combatants.
 *
 * This destroys the enemy's ship and ends the combat.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplyDestroy(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	auto actor = isBoarder ? boarder : target;
	auto enemy = isBoarder ? target : boarder;

	int casualties = 0;
	int enemyCasualties = actor->GetShip()->Crew();

	if(enemyCasualties > 0)
		pendingCasualtyConsequences = true;

	enemy->GetShip()->Destroy();

	return Action::Result(
		State::Ended,
		negotiation,
		casualties,
		enemyCasualties
	);
}



/**
 * Apply the effects of a successful Leave action to the combatants.
 *
 * This immediately ends the combat.
 *
 * @param state The current state of the boarding combat.
 * @param negotiation The combatant that the action was performed against.
 * @param isBoarder Whether or not the action was performed by the boarder.
 * @param action The action performed by the combatant.
 * @param enemyAction The action performed by the enemy combatant.
 *
 * @return An Action::Result object that reports what happened.
 */
Boarding::Action::Result BoardingCombat::ApplyLeave(
	State state,
	Negotiation negotiation,
	bool isBoarder,
	const Action &action,
	const Action &enemyAction
)
{
	int casualties = 0;
	int enemyCasualties = 0;

	return Action::Result(
		State::Ended,
		negotiation,
		casualties,
		enemyCasualties
	);
}



/**
 * @return Whether or not the player's enemy combatant has been conquered.
 */
bool BoardingCombat::IsPlayerEnemyConquered() const
{
  if(boarder->IsPlayerControlled())
		return GetHistory()->back()->state == State::BoarderVictory;
	else if(target->IsPlayerControlled())
		return GetHistory()->back()->state == State::TargetVictory;
	else
		return false;
}



/**
 * When the player takes control of their side of a boarding combat,
 * we cannot resolve the whole thing automatically. Instead, we need
 * to present it to the player turn by turn via the BoardingPanel.
 * Each time the player makes a decision, we call this function to
 * resolve the consequences of that decision, progressing the combat
 * by one turn.
 *
 * If this function is called with an action that the player cannot
 * take in the current state of the combat, it will not progress to the
 * next turn. Instead, it will return a message explaining why the
 * action is invalid and log an error to the game's log file.
 *
 * This should never happen in practice, since the BoardingPanel
 * should only allow the player to take valid actions. Bugs are always
 * possible, though, so we want to make sure that the game does not
 * reach an invalid state if the player somehow manages to trigger one.
 *
 * @param playerAction The action that the player has chosen to take.
 *
 * @return A shared pointer to the new Turn that has been added to the History.
 *
 * @throws invalid_argument if the player tries to take an invalid action.
 */
const shared_ptr<BoardingCombat::Turn> BoardingCombat::Step(
	Action::Activity playerIntent
)
{
	auto latest = history->back();

	// Prevent the player from taking an invalid action.
	if(!GetPlayerCombatant()->ValidObjectives(latest->state, latest->negotiation)->at(playerIntent.objective))
	{
		throw invalid_argument(
			"BoardingCombat::Step - player tried to take an invalid action: "
				+ Action::GetObjectiveName(playerIntent.objective)
				+ " while in state "
				+ Boarding::GetStateName(history->back()->state)
		);
	}

	// Prevent the player from taking actions with invalid details.
	if(!Action::IsValidDetails(playerIntent.objective, playerIntent.details))
	{
		throw invalid_argument(
			"BoardingCombat::Step - invalid details for player action: "
				+ Action::GetObjectiveName(playerIntent.objective)
				+ " while in state "
				+ Boarding::GetStateName(history->back()->state)
		);
	}

	// Create a new Turn object to represent the next step in the combat.
	shared_ptr<Turn> next = boarder->IsPlayerControlled()
		? make_shared<Turn>(
			*this,
			playerIntent,
			target->DetermineIntent(latest->targetSituationReport)
		)
		: make_shared<Turn>(
			*this,
			boarder->DetermineIntent(latest->boarderSituationReport),
			playerIntent
		);

	// Add the new Turn to the History.
	history->push_back(next);

	return next;
}



/**
 * Resolve the entire boarding combat automatically, without any player
 * input. This is used when the player is not controlling the combat.
 *
 * @return A string explaining the outcome of the combat.
 */
const string BoardingCombat::ResolveAutomatically()
{
	bool ended = false;

	while (!ended)
	{
		auto latest = Step();
		auto state = latest->state;

		ended = state == State::Ended;
	}

	return "";
}



/**
 * Resolve the next Turn of combat, selecting actions automatically for
 * each combatant. Used when the player is not controlling the combat.
 *
 * @return A shared pointer to the new Turn that has been added to the History.
 */
const shared_ptr<BoardingCombat::Turn> BoardingCombat::Step()
{
	auto latest = history->back();

	// Create a new Turn object to represent the next step in the combat.
	shared_ptr<Turn> next = make_shared<Turn>(
		*this,
		boarder->DetermineIntent(latest->boarderSituationReport),
		target->DetermineIntent(latest->targetSituationReport)
	);

	// Add the new Turn to the History.
	history->push_back(next);

	return next;
}


