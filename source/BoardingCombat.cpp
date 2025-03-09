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

#include "Depreciation.h"
#include "text/Format.h"
#include "GameData.h"
#include "Gamerules.h"
#include "Government.h"
#include "Logger.h"
#include "Outfit.h"
#include "PlayerInfo.h"
#include "Preferences.h"
#include "Ship.h"
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
	Action boarderAction,
	Action targetAction,
	ActionDetails boarderActionDetails,
	ActionDetails targetActionDetails
) :
	boarderAction(boarderAction),
	targetAction(targetAction),
	state(combat.history->back()->state),
	negotiationState(combat.history->back()->negotiationState),
	boarderCasualties(0),
	targetCasualties(0),
	messages({})
{
	shared_ptr<Turn> latest = combat.history->back();

	// Validate the inputs before we go any further. This will throw an error if we have
	// somehow supplied the function with invalid data. It shouldn't happen, but if there
	// is a problem due to programmer error, it's useful to know straight away.
	if(!IsValidActionDetails(boarderAction, boarderActionDetails))
		throw runtime_error(
			"BoardingCombat::Turn - boarder's Action has invalid ActionDetails. Action:"
				+ to_string(static_cast<int>(targetAction)) + "."
		);

	if(!IsValidActionDetails(targetAction, targetActionDetails))
		throw runtime_error(
			"BoardingCombat::Turn - target's Action has invalid ActionDetails. Action:"
				+ to_string(static_cast<int>(targetAction)) + "."
		);

	if(!combat.boarder->AvailableActions(latest->state)->at(boarderAction))
		throw runtime_error("BoardingCombat::Turn - boarder tried to take an invalid action:" + to_string(static_cast<int>(boarderAction)));

	if(!combat.boarder->AvailableActions(latest->state)->at(targetAction))
		throw runtime_error("BoardingCombat::Turn - target tried to take an invalid action:" + to_string(static_cast<int>(targetAction)));



	// If either side has taken an action with a binary result,
	// we need to determine whether or not that action succeeds or fails.
	successfulBoarderAction = combat.boarder->AttemptBinaryAction(combat, boarderAction);
	successfulTargetAction = combat.target->AttemptBinaryAction(combat, targetAction);



	// Determine the number of casualty rolls that we need to make.
	int casualtyRolls = 0;

	if(boarderAction == Action::Attack)
		casualtyRolls += combat.boarder->CasualtyRollsPerAction(boarderAction);
	else if(boarderAction == Action::Defend && latest->state == State::TargetInvading)
		casualtyRolls += combat.target->CasualtyRollsPerAction(targetAction);

	if(targetAction == Action::Attack)
		casualtyRolls += combat.target->CasualtyRollsPerAction(targetAction);
	else if(targetAction == Action::Defend && latest->state == State::BoarderInvading)
		casualtyRolls += combat.boarder->CasualtyRollsPerAction(boarderAction);

	// Self destructing while being invaded increases the number of potential casualties.
	if(
		(successfulBoarderAction == Action::SelfDestruct && latest->state == State::TargetInvading)
	)
		casualtyRolls += (casualtyRolls + combat.boarder->CasualtyRollsPerAction(successfulBoarderAction));
	else if(
		(successfulTargetAction == Action::SelfDestruct && latest->state == State::BoarderInvading)
	)
		casualtyRolls += (casualtyRolls + combat.target->CasualtyRollsPerAction(successfulTargetAction));

	// If an attempt to negotiate has succeeded, no casualties occur this turn.
	if(successfulBoarderAction == Action::Negotiate || successfulTargetAction == Action::Negotiate)
		casualtyRolls = 0;



	// Determine the number of casualties suffered by each side.
	// Cache the power of each side to avoid recalculating it unnecessarily.
	double boarderPower = combat.boarder->Power(boarderAction);
	double targetPower = combat.target->Power(targetAction);

	for(int i = 0; i < casualtyRolls; ++i) {
		// Roll to determine which side suffers a casualty.
		bool isBoarderCasualty = Random::Real() * (boarderPower + targetPower) >= boarderPower;

		// Apply the casualty to the combatant who lost the roll.
		if(isBoarderCasualty) {
			++boarderCasualties;
			if(!combat.boarder->ApplyCasualty(state == State::BoarderInvading)) {
				state = State::TargetVictory;
				break;
			}
			boarderPower = combat.boarder->Power(boarderAction);
		} else {
			++targetCasualties;
			if(!combat.target->ApplyCasualty(state == State::TargetInvading)) {
				state = State::BoarderVictory;
				break;
			}
			targetPower = combat.target->Power(targetAction);
		}
	}

	if(successfulBoarderAction == Action::SelfDestruct || successfulTargetAction == Action::SelfDestruct)
		state = State::Ended;

	// Generate a SituationReport for each combatant.
	boarderSituationReport = make_shared<SituationReport>(combat.boarder, combat.target, *this);
	targetSituationReport = make_shared<SituationReport>(combat.target, combat.boarder, *this);


}



/**
 * Constructor for BoardingCombat::Turn that is used to resolve the
 * combat when the boarder and target have made an Offer.
 *
 * Adds a Turn to the History where both combatants take the Resolve
 * action, and applies the Terms of the Offer to the combatants.
 *
 * Changes the negotiationState of the combat to Successful.
 * The new state of the combat is based on the Terms of the Offer.
 *
 * @param combat A reference to the BoardingCombat that is taking place.
 * @param agreement The Offer that the boarder and target have made.
 *
 * @return A new Turn that models the negotiated resolution of the combat.
 */
BoardingCombat::Turn::Turn(BoardingCombat &combat, Offer &agreement) :
	boarderAction(Action::Resolve),
	targetAction(Action::Resolve),
	successfulBoarderAction(Action::Resolve),
	successfulTargetAction(Action::Resolve),
	state(combat.history->back()->state),
	negotiationState(NegotiationState::Successful),
	boarderCasualties(0),
	targetCasualties(0)
{
	shared_ptr<Turn> latest = combat.history->back();

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
	boarderSituationReport = make_shared<SituationReport>(combat.boarder, combat.target, *this);
	targetSituationReport = make_shared<SituationReport>(combat.target, combat.boarder, *this);
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
	const shared_ptr<Combatant> &boarder,
	const shared_ptr<Combatant> &target
) :
	boarderAction(Action::Null),
	targetAction(Action::Null),
	state(State::Isolated),
	negotiationState(NegotiationState::NotAttempted),
	boarderCasualties(0),
	targetCasualties(0),
	boarderSituationReport(make_shared<SituationReport>(
		boarder, target, *this
	)),
	targetSituationReport(make_shared<SituationReport>(
		target, boarder, *this
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
	bool isBoarder,
	bool usingBoardingPanel,
	const vector<shared_ptr<Ship>> &playerFleet
) :
	ship(ship),
	plunderSession(make_shared<Plunder::Session>(enemyShip, ship, playerFleet)),
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
	odds(*ship, *enemyShip),
	crewAnalysisBefore(make_shared<Crew::ShipAnalysis>(ship, ship->IsPlayerFlagship())),
	crewDisplayNameMidSentence(BoardingCombat::BuildCrewDisplayName(ship, false)),
	crewDisplayNameStartOfSentence(BoardingCombat::BuildCrewDisplayName(ship, true)),
	captureValue(ship->Cost() * Depreciation::Full()),
	chassisValue(ship->ChassisCost() * Depreciation::Full()),
	protectedPlunderValue(0),
	expectedCostPerBoardingCasualty(Crew::ExpectedCostPerCasualty(true)),
	expectedCostPerCasualtyPostCapture(Crew::ExpectedCostPerCasualty(false)),
	hasCaptureObjective(ship->GetBoardingObjective() == Ship::BoardingObjective::CAPTURE),
	hasPlunderObjective(ship->GetBoardingObjective() == Ship::BoardingObjective::PLUNDER),
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
 * The main purpose of the CasualtyAnalysis is to provide the information
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
 * @return The number of casualty rolls that the combatant makes each time
 * 	it takes an action that can kill enemy crew members (minimum 1).
 * 	This is a flat percentage of the combatant's current attacker or
 * 	defender count, as relevant for the action, as defined in the game
 * 	rules as BoardingCasualtyPercentagePerAction.
 */
int BoardingCombat::Combatant::CasualtyRollsPerAction(Action action) const
{
  return min(
		1,
		static_cast<int>(
			GameData::GetGamerules().BoardingCasualtyPercentagePerAction()
			* (BoardingCombat::aggressionByAction.at(action) ? Invaders() : Defenders())
		)
	);
}



/**
 * Evaluates the odds of victory, the expected casualties, and the potential
 * profit of invading the enemy. Compares them with the combatant's
 * AttackStrategy to determine if it is willing to risk an attack.
 *
 * Does not take into account other strategic factors, such as the
 * Counter defense strategy. Those are expected to be handled by the
 * calling function.
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
 * @param combat A reference to the combat that is taking place.
 *
 * @return True if the ship wants to attack, false otherwise.
 */
bool BoardingCombat::Combatant::ConsiderAttacking(const BoardingCombat &combat)
{
	shared_ptr<Turn> latest = combat.GetHistory()->back();

	// Only the boarder can attack when the combatants are isolated.
	if(!isBoarder && latest->state == State::Isolated)
	return false;

	shared_ptr<SituationReport> report = isBoarder ? latest->boarderSituationReport : latest->targetSituationReport;

	bool withinRiskTolerance = false;

	switch(attackStrategy)
	{
		case(AttackStrategy::Cautious):
			withinRiskTolerance = report->invasionVictoryProbability > 0.99
				&& report->expectedInvasionCasualties < 0.5;
		case(AttackStrategy::Aggressive):
			withinRiskTolerance = report->invasionVictoryProbability > 0.99
				&& report->expectedInvasionCasualties < ship->ExtraCrew();
		case(AttackStrategy::Reckless):
			withinRiskTolerance = report->invasionVictoryProbability > 0.5;
		case(AttackStrategy::Fanatical):
			withinRiskTolerance = report->invasionVictoryProbability > 0.01;
		default:
		{
			Logger::LogError(
				"BoardingCombat::ConsiderAttacking cannot handle AttackStrategy: "
					+ to_string(static_cast<int>(attackStrategy)) + ". Defaulting to false."
			);
			withinRiskTolerance = false;
		}
	}

	return withinRiskTolerance && report->expectedInvasionProfit > 0;
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
 * Determines the action that the combatant should take during next turn.
 * The action is based on the combatant's current state, the state of the
 * enemy ship, the combat history, and their strategy.
 *
 * @param combat A const reference to the BoardingCombat that is taking place.
 *
 * @return The action that the combatant should take during the next turn.
 */
BoardingCombat::Action BoardingCombat::Combatant::DetermineAction(const BoardingCombat &combat)
{
	shared_ptr<Combatant> enemy = isBoarder ? combat.target : combat.boarder;
	double defeatOdds = enemy->GetOdds().Odds(enemy->Crew(), ship->Crew());
	double selfDestructAttribute = ship->Attributes().Get("self destruct");
	// If the combatant has more attack power than defense, they attack when
	// they would otherwise defend. This is not usually the case, but it can
	// easily happen if the combatant has access to automated invaders.
	Action defenseAction = AttackPower() > DefensePower() ? Action::Attack : Action::Defend;
	bool willingToAttack = ConsiderAttacking(combat);

	shared_ptr<Turn> latest = combat.GetHistory()->back();

	const shared_ptr<map<Action, bool>> availableActions = AvailableActions(latest->state);

	// When combatants are isolated from one another, their options are more limited.
	// Combatants are also considered isolated during the first turn of combat.
	if(!latest || latest->state == State::Isolated)
	{
		if(isBoarder)
		{
			if((hasCaptureObjective || hasPlunderObjective) && willingToAttack)
				return Action::Attack;
			else if(hasPlunderObjective && ship->Cargo().Free() > 0)
				return Action::Raid;
			else
				return Action::Leave;
		}
		else
		{
			if(defenseStrategy == DefenseStrategy::Deny && selfDestructAttribute > 0.0)
				return Action::SelfDestruct;
			else
				return defenseAction;
		}
	}

	// If we reach this point, at least one turn has taken place so we can
	// read the state directly without risking an error.
	State state = latest->state;
	Action latestEnemyAction = isBoarder ? latest->targetAction : latest->boarderAction;

	// Neither combatant has invaded the other with troops, but their airlocks
	// are connected so they are can try to invade one another.
	if(state == State::Poised)
	{
		switch(defenseStrategy)
		{
			case(DefenseStrategy::Repel):
				return willingToAttack ? Action::Attack : defenseAction;
			case(DefenseStrategy::Counter):
				return willingToAttack && (latestEnemyAction == defenseAction || latestEnemyAction == Action::Plunder)
					? Action::Attack
					: defenseAction;
			case(DefenseStrategy::Deny):
				return willingToAttack
					? Action::Attack
					: latestEnemyAction == Action::Attack && defeatOdds > 0.01 && selfDestructAttribute > 0.0
						? Action::SelfDestruct
						: defenseAction;
			default:
			{
				Logger::LogError("BoardingCombat::DetermineAction with State::Poised cannot handle DefenseStrategy: " + to_string(static_cast<int>(defenseStrategy)) + ". Defaulting to Defend action.");
				return defenseAction;
			}
		}
	}

	// This combatant has invaded the enemy with troops.
	if(isBoarder ? state == State::BoarderInvading : state == State::TargetInvading)
	{
		if(willingToAttack)
			return Action::Attack;
		else if(hasPlunderObjective && ship->Cargo().Free() > 0)
			return Action::Plunder;
		else
			return defenseAction;
	}

	// This combatant has fully conquered the enemy combatant.
	if(isBoarder ? state == State::BoarderVictory : state == State::TargetVictory)
	{
		if(hasCaptureObjective && ship->Crew() > 2)
			return Action::Capture;
		else if(hasPlunderObjective && ship->Cargo().Free() > 0)
			return Action::Raid;
		else
			return Action::Leave;
	}

	// This combatant been has fully conquered the enemy combatant.
	if(isBoarder ? state == State::TargetVictory : state == State::BoarderVictory)
		return Action::Null;

	// Something unexpected has happened, so we default to the defenseAction.
	Logger::LogError("BoardingCombat::DetermineAction reached an unexpected state. Defaulting to defenseAction.");
	return defenseAction;
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
int64_t BoardingCombat::Combatant::ExpectedCaptureProfit(double expectedInvasionCasualties, double victoryOdds)
{
	return captureValue * victoryOdds
		- expectedInvasionCasualties * expectedCostPerBoardingCasualty
		- expectedPostCaptureCasualtyCosts;
}



/**
 * Calculates the the expected financial gain from invading and conquering
 * this combatant, factoring in the probability of success and whether
 * the combatant is planning to capture the ship or plunder it.
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
	int64_t expectedProtectedPlunderProfit
)
{
  switch(enemy->GetShip()->GetBoardingObjective())
	{
		case Ship::BoardingObjective::CAPTURE:
			return expectedCaptureProfit;
		case Ship::BoardingObjective::PLUNDER:
			return expectedProtectedPlunderProfit;
		case Ship::BoardingObjective::CAPTURE_MANUALLY:
			return max(expectedCaptureProfit, expectedProtectedPlunderProfit);
		case Ship::BoardingObjective::PLUNDER_MANUALLY:
			return max(expectedCaptureProfit, expectedProtectedPlunderProfit);
		default:
			Logger::LogError(
				"BoardingCombat::Combatant::ExpectedInvasionProfit - Invalid BoardingObjective: "
					+ to_string(static_cast<int>(enemy->GetShip()->GetBoardingObjective()))
			);
			return 0;
	}
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
int64_t BoardingCombat::Combatant::ExpectedProtectedPlunderProfit(double expectedInvasionCasualties, double victoryOdds)
{
  return protectedPlunderValue * victoryOdds
		- expectedInvasionCasualties * expectedCostPerBoardingCasualty;
}



/**
 * @return How powerful the combatant is when attacking.
 */
double BoardingCombat::Combatant::AttackPower() const
{
  return odds.AttackerPower(Invaders());
}



/**
 * @return How powerful the combatant is when defending.
 */
double BoardingCombat::Combatant::DefensePower() const
{
  return odds.DefenderPower(Defenders());
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
	double power = aggressionByAction.at(Action::SelfDestruct) ? AttackPower() : DefensePower();

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
double BoardingCombat::Combatant::ExpectedSelfDestructCasualties(const shared_ptr<Combatant> &enemy) const
{
	double power = Power(Action::SelfDestruct, Action::SelfDestruct);

  return CasualtyRollsPerAction(Action::SelfDestruct)
		* (power / (power + enemy->AttackPower()));
}



/**
 * @return The total number of defenders that the combatant has available.
 * 	This includes both crew members and automated defenders.
 */
int BoardingCombat::Combatant::Defenders() const
{
  return ship->Crew() + static_cast<int>(ship->Attributes().Get("automated defenders"));
}



/**
 * @return The total number of invaders that the combatant has available.
 * 	This includes both crew members and automated invaders.
 */
int BoardingCombat::Combatant::Invaders() const
{
  return ship->Crew() + static_cast<int>(ship->Attributes().Get("automated invaders"));
}



/**
 * @return The CaptureOdds from this combatant's perspective.
 */
CaptureOdds BoardingCombat::Combatant::GetOdds() const
{
  return odds;
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
 * Calculates the power of the combatant when performing a specific action.
 * The power is based on the number of invaders or defenders available to
 * the combatant, and whether or not the action is aggressive.
 *
 * The power can be further modified based on whether or not the combatant
 * has successfully performed a relevant binary action. For example, if the
 * combatant has successfully activated their self-destruct system, their
 * power is increased by the self-destruct power multiplier defined in
 * the game rules.
 *
 * @param action The action that the combatant is attempting.
 * @param successfulBinaryAction (optional) A binary action that the
 * 	combatant has successfully performed this turn, such as SelfDestruct.
 *
 * @return The power of the combatant when performing the specified action.
 */
double BoardingCombat::Combatant::Power(Action action, Action successfulBinaryAction) const
{
  if(BoardingCombat::aggressionByAction.at(action))
		return AttackPower();

	double defenseMultiplier = 1.0;

	if(successfulBinaryAction == Action::SelfDestruct)
		defenseMultiplier *= GameData::GetGamerules().BoardingSelfDestructCasualtyPowerMultiplier();

	return DefensePower() * defenseMultiplier;
}



/**
 * Used to determine whether or not the combatant's action is successful.
 * If the action does not have a binary success/failure outcome that can
 * be determined during a single turn, the function returns false.
 *
 * This function does not apply the effect of the action. That must be
 * performed separately based on whether or not the action is successful.
 *
 * @param combat A reference to the BoardingCombat that is taking place.
 * @param action The action that the combatant is attempting.
 *
 * @return The action that succeeded, or Action::Null if the action failed.
 */
BoardingCombat::Action BoardingCombat::Combatant::AttemptBinaryAction(const BoardingCombat &combat, Action action)
{
	NegotiationState negotiationState = combat.history->back()->negotiationState;
	State state = combat.GetHistory()->back()->state;

	double power = Power(action);
	shared_ptr<Combatant> enemy = isBoarder ? combat.target : combat.boarder;
	double enemyPower = enemy->Power(action);
	double totalPower = power + enemyPower;

	bool wonPowerRoll = Random::Real() * totalPower >= power;
	bool succeeded = false;

	switch(action)
	{
		case Action::Negotiate:
			succeeded = combat.IsLanguageShared() && (
				negotiationState == NegotiationState::NotAttempted
				|| negotiationState == NegotiationState::Successful
			);
			break;
		case Action::Plunder:
			if(state == State::Isolated || state == State::BoarderVictory)
				succeeded = isBoarder;
			else if(state == State::TargetVictory)
				succeeded = !isBoarder;
			else if(state == State::BoarderInvading)
				succeeded = isBoarder ? wonPowerRoll : false;
			else if(state == State::TargetInvading)
				succeeded = isBoarder ? false : wonPowerRoll;
			else if(state == State::Poised)
				succeeded = wonPowerRoll;
			else
				succeeded = false;
			break;
		case Action::Resolve:
			succeeded = negotiationState == NegotiationState::Active;
			break;
		case Action::SelfDestruct:
			succeeded = wonPowerRoll && Random::Real() < ship->Attributes().Get("self destruct");
			break;
		default:
			succeeded = false;
	}

	return succeeded ? action : Action::Null;
}



/**
 * @param state The current state of the boarding combat.
 *
 * @return A map of each Action that a ship could possibly take during a
 * 	boarding combat, and whether or not the combatant is allowed to take
 * 	that action during the next turn of combat.
 */
const shared_ptr<map<BoardingCombat::Action, bool>> BoardingCombat::Combatant::AvailableActions(State state) const
{
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
		|| (state == State::BoarderInvading && isBoarder)
		|| (state == State::TargetInvading && !isBoarder)
		|| (state == State::BoarderVictory && isBoarder)
		|| (state == State::TargetVictory && !isBoarder);

	bool canNegotiate = false; // Not implemented yet.
	bool canResolve = false; // Not implemented yet.

	bool canCapture = (state == State::BoarderVictory && isBoarder)
		|| (state == State::TargetVictory && !isBoarder);
	bool canRaid = (state == State::Isolated && isBoarder)
		|| (state == State::BoarderVictory && isBoarder)
		|| (state == State::TargetVictory && !isBoarder);
	bool canLeave = (state == State::Isolated && isBoarder)
		|| (state == State::BoarderVictory && isBoarder)
		|| (state == State::TargetVictory && !isBoarder);
	bool canDestroy = (state == State::Isolated && isBoarder)
		|| (state == State::BoarderVictory && isBoarder)
		|| (state == State::TargetVictory && !isBoarder);

	return make_shared<map<Action, bool>>(map<Action, bool>
		{
			{Action::Attack, canAttack},
			{Action::Defend, canDefend},
			{Action::Plunder, canPlunder},
			{Action::SelfDestruct, canSelfDestruct},
			{Action::Negotiate, canNegotiate},
			{Action::Resolve, canResolve},
			{Action::Capture, canCapture},
			{Action::Raid, canRaid},
			{Action::Leave, canLeave},
			{Action::Destroy, canDestroy}
		}
	);
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
 * @param combat A reference to the BoardingCombat that is taking place.
 * @param isBoarder Whether or not the combatant is the boarder.
 *
 * @return A SituationReport object that describes the combat as of the
 * 	latest turn, from the perspective of one of the combatants.
 */
BoardingCombat::SituationReport::SituationReport(
	const shared_ptr<Combatant> &combatant,
	const shared_ptr<Combatant> &enemy,
	const Turn &turn
):
	combatant(combatant),
	enemy(enemy),
	turn(turn),
	ship(combatant->GetShip()),
	enemyShip(enemy->GetShip()),
	isConquered(
		combatant->IsBoarder()
			? turn.state == State::TargetVictory
			: turn.state == State::BoarderVictory
	),
	isEnemyConquered(
		combatant->IsBoarder()
			? turn.state == State::BoarderVictory
			: turn.state == State::TargetVictory
	),
	invaders(combatant->Invaders()),
	defenders(enemy->Defenders()),
	crew(combatant->Crew()),
	enemyInvaders(enemy->Invaders()),
	enemyDefenders(combatant->Defenders()),
	enemyCrew(enemy->Crew()),
	cargoSpace(combatant->GetShip()->Cargo().Free()),
	attackPower(combatant->AttackPower()),
	defensePower(combatant->DefensePower()),
	enemyAttackPower(enemy->AttackPower()),
	enemyDefensePower(enemy->DefensePower()),
	attackingTotalPower(attackPower + enemyDefensePower),
	defendingTotalPower(defensePower + enemyAttackPower),
	minimumTurnsToVictory(invaders / (combatant->CasualtyRollsPerAction(Action::Attack) * (attackPower / attackingTotalPower))),
	minimumTurnsToDefeat(enemyInvaders / (enemy->CasualtyRollsPerAction(Action::Attack) * (enemyAttackPower / defendingTotalPower))),
	enemySelfDestructProbability(enemy->SelfDestructProbability(combatant)),
	cumulativeSelfDestructProbability(1 - pow(1 - enemySelfDestructProbability, minimumTurnsToVictory)),
	selfDestructCasualtyPower(enemy->Power(Action::SelfDestruct, Action::SelfDestruct)),
	expectedSelfDestructCasualties(combatant->ExpectedSelfDestructCasualties(enemy)),
	expectedInvasionCasualties(
		combatant->GetOdds().AttackerCasualties(invaders, enemyDefenders)
		+ cumulativeSelfDestructProbability * expectedSelfDestructCasualties
	),
	expectedDefensiveCasualties(
		enemy->GetOdds().AttackerCasualties(enemyInvaders, defenders)
	),
	invasionVictoryProbability(
		combatant->GetOdds().Odds(invaders, enemyDefenders) * (1 - cumulativeSelfDestructProbability)
	),
	defensiveVictoryProbability(
		enemy->GetOdds().Odds(enemyInvaders, defenders)
	),
	postCaptureSurvivalProbability(
		combatant->PostCaptureSurvivalOdds()
	),
	expectedCaptureProfit(
		enemy->ExpectedCaptureProfit(expectedInvasionCasualties, invasionVictoryProbability)
	),
	expectedProtectedPlunderProfit(
		enemy->ExpectedProtectedPlunderProfit(expectedInvasionCasualties, invasionVictoryProbability)
	),
	expectedInvasionProfit(
		enemy->ExpectedInvasionProfit(
			combatant,
			expectedCaptureProfit,
			expectedProtectedPlunderProfit
		)
	),
	plunderOptions(combatant->PlunderOptions()),
	availableActions(*combatant->AvailableActions(turn.state))
{
}




// --- End of BoardingCombat::SituationReport class implementation ---



// --- Start of BoardingCombat static members and functions ---


/**
 * Some actions are considered aggressive, while others are considered
 * defensive. This is used to determine whether a combatant uses their
 * attack or defense power when performing a given action.
 */
const map<BoardingCombat::Action, bool> BoardingCombat::aggressionByAction = {
	{Action::Attack, true},
	{Action::Defend, false},
	{Action::Plunder, true},
	{Action::SelfDestruct, false},
	{Action::Negotiate, false},
	{Action::Resolve, false},
	{Action::Capture, true},
	{Action::Raid, true},
	{Action::Leave, false},
	{Action::Destroy, true}
};



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



/**
 * Validates that ActionDetails are valid for a given Action.
 *
 * @param action The action that the combatant is attempting.
 * @param details The details of the action that the combatant is attempting.
 *
 * @return True if the details are valid for the action, false otherwise.
 */
bool BoardingCombat::IsValidActionDetails(Action action, ActionDetails details)
{
	if(holds_alternative<int>(details)) {
		return IsValidActionDetails(action, get<int>(details));
	}	else if(holds_alternative<tuple<int, int>>(details)) {
		return IsValidActionDetails(action, get<tuple<int, int>>(details));
	} else if(holds_alternative<Offer>(details)) {
		return IsValidActionDetails(action, get<Offer>(details));
	}
	return false; // If none of the types match, return false
}



/**
 * Validates that int ActionDetails are valid for a given Action.
 *
 * @param action The action that the combatant is attempting.
 * @param details The details of the action that the combatant is attempting.
 *
 * @return True if the details are valid for the action, false otherwise.
 */
bool BoardingCombat::IsValidActionDetails(Action action, int details)
{
	switch(action)
	{
		case Action::Plunder : return true; break;
		default : return false;
	}
}



/**
 * Validates that Offer ActionDetails are valid for a given Action.
 *
 * @param action The action that the combatant is attempting.
 * @param details The details of the action that the combatant is attempting.
 *
 * @return True if the details are valid for the action, false otherwise.
 */
bool BoardingCombat::IsValidActionDetails(Action action, Offer details)
{
	switch(action)
	{
		case Action::Negotiate : return true; break;
		case Action::Resolve : return true; break;
		default : return false;
	}
}



/**
 * Validates that bool ActionDetails are valid for a given Action.
 * A false bool is the default value for an Action that does not require details.
 *
 * @param action The action that the combatant is attempting.
 * @param details The details of the action that the combatant is attempting.
 *
 * @return True if the details are valid for the action, false otherwise.
 */
bool BoardingCombat::IsValidActionDetails(Action action, bool details)
{
	switch(action)
	{
		case Action::Plunder : return false; break;
		case Action::Negotiate : return false; break;
		case Action::Resolve : return false; break;
		default : return !details;
	}
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
	boardingObjective(boarderShip->GetBoardingObjective()),
	usingBoardingPanel(
		boarderShip->IsPlayerFlagship()
		|| targetShip->IsPlayerFlagship()
		|| boardingObjective == Ship::BoardingObjective::CAPTURE_MANUALLY
		|| boardingObjective == Ship::BoardingObjective::PLUNDER_MANUALLY
	),
	boarder(make_shared<BoardingCombat::Combatant>(
		boarderShip,
		targetShip,
		true,
		usingBoardingPanel,
		player.Ships()
	)),
	target(make_shared<BoardingCombat::Combatant>(
		targetShip,
		boarderShip,
		false,
		usingBoardingPanel,
		player.Ships()
	)),
	history(make_shared<History>(make_shared<Turn>(boarder, target)))
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
}



/**
 * Determines the most likely next state of the boarding combat based on
 * the current state and the actions taken by the boarder and the target.
 *
 * The state might end up being different based on the outcomes of the
 * actions taken by the combatants. For instance, if one of the combatants
 * conquers the other or succeeds at an action with a binary result, the
 * actual next state will reflect that.
 *
 * @param state The state of the boarding combat as of the previous turn.
 * @param boarderAction The action taken by the boarder during the last turn.
 * @param targetAction The action taken by the target during the last turn.
 *
 * @return The next state of the boarding combat.
 */
BoardingCombat::State BoardingCombat::GuessNextState(State state, Action boarderAction, Action targetAction) const
{
  switch(state) {
		case State::Isolated:
			if(boarderAction == Action::Attack)
				return State::BoarderInvading;
			else
				return State::Isolated;

		case State::BoarderInvading:
			if(boarderAction == Action::Defend)
				return State::Poised;
			else
				return State::BoarderInvading;

		case State::Poised:
			if(boarderAction == Action::Attack && targetAction == Action::Attack)
				return State::Poised;
			else if(boarderAction == Action::Attack)
				return State::BoarderInvading;
			else if(targetAction == Action::Attack)
				return State::TargetInvading;
			else
				return State::Poised;

		case State::TargetInvading:
			if(targetAction == Action::Defend)
				return State::Poised;
			else
				return State::TargetInvading;

		case State::BoarderVictory:
			return State::BoarderVictory;

		case State::TargetVictory:
			return State::TargetVictory;

		case State::Ended:
			return State::Ended;
	}
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
const shared_ptr<BoardingCombat::Turn> BoardingCombat::Step(Action playerAction, ActionDetails playerActionDetails)
{
	// Prevent the player from taking an invalid action.
	if(!GetPlayerCombatant()->AvailableActions(history->back()->state)->at(playerAction))
	{
		throw invalid_argument(
			"BoardingCombat::Step - player tried to take an invalid action:"
				+ to_string(static_cast<int>(playerAction))
				+ " while in state "
				+ to_string(static_cast<int>(history->back()->state))
		);
	}

	// Create a new Turn object to represent the next step in the combat.
	shared_ptr<Turn> next = boarder->IsPlayerControlled()
		? make_shared<Turn>(*this, playerAction, target->DetermineAction(*this), playerActionDetails)
		: make_shared<Turn>(*this, boarder->DetermineAction(*this), playerAction);

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
	// Create a new Turn object to represent the next step in the combat.
	shared_ptr<Turn> next = make_shared<Turn>(*this, boarder->DetermineAction(*this), target->DetermineAction(*this));

	// Add the new Turn to the History.
	history->push_back(next);

	return next;
}


