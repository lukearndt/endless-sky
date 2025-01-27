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
	Action targetAction
) :
	boarderAction(boarderAction),
	targetAction(targetAction),
	boarderCasualties(0),
	targetCasualties(0),
	state(combat.GuessNextState(combat.history->back()->state, boarderAction, targetAction))
{
	shared_ptr<Turn> latest = combat.history->back();

	// Check if the boarder is trying to take an invalid action.
	if(!combat.boarder->ValidActions(latest->state)->at(boarderAction))
		throw runtime_error("BoardingCombat::Turn - boarder tried to take an invalid action:" + to_string(static_cast<int>(boarderAction)));

	// Check if the target is trying to take an invalid action.
	if(!combat.boarder->ValidActions(latest->state)->at(targetAction))
		throw runtime_error("BoardingCombat::Turn - target tried to take an invalid action:" + to_string(static_cast<int>(targetAction)));

	// If either side has taken an action with a binary result,
	// we need to determine whether or not that action succeeds or fails.
	successfulBoarderAction = combat.boarder->AttemptBinaryAction(combat, boarderAction);
	successfulTargetAction = combat.target->AttemptBinaryAction(combat, targetAction);

	// Determine the number of casualties that we need to account for.
	int casualtyRolls = 0;

	if(boarderAction == Action::Attack)
		casualtyRolls += combat.boarder->CasualtyRollsPerAction();
	else if(boarderAction == Action::Defend && latest->state == State::TargetInvading)
		casualtyRolls += combat.target->CasualtyRollsPerAction();

	if(targetAction == Action::Attack)
		casualtyRolls += combat.target->CasualtyRollsPerAction();
	else if(targetAction == Action::Defend && latest->state == State::BoarderInvading)
		casualtyRolls += combat.boarder->CasualtyRollsPerAction();

	// Self destructing while being invaded increases the number of potential casualties.
	if(
		(successfulBoarderAction == Action::SelfDestruct && latest->state == State::TargetInvading)
	)
		casualtyRolls += (casualtyRolls + combat.boarder->CasualtyRollsPerAction());
	else if(
		(successfulTargetAction == Action::SelfDestruct && latest->state == State::BoarderInvading)
	)
		casualtyRolls += (casualtyRolls + combat.target->CasualtyRollsPerAction());

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

}



/**
 * Constructor for BoardingCombat::Turn that is used to resolve the
 * combat when the boarder and target have made an Agreement.
 *
 * Adds a Turn to the History where both combatants take the Resolve
 * action, and applies the Terms of the Agreement to the combatants.
 *
 * Changes the negotiationState of the combat to Successful.
 * The new state of the combat is based on the Terms of the Agreement.
 *
 * @param combat A reference to the BoardingCombat that is taking place.
 * @param agreement The Agreement that the boarder and target have made.
 *
 * @return A new Turn that models the negotiated resolution of the combat.
 */
BoardingCombat::Turn::Turn(BoardingCombat &combat, Agreement &agreement) :
	boarderAction(Action::Resolve),
	targetAction(Action::Resolve),
	successfulBoarderAction(Action::Resolve),
	successfulTargetAction(Action::Resolve),
	boarderCasualties(0),
	targetCasualties(0),
	negotiationState(NegotiationState::Successful)
{
	shared_ptr<Turn> latest = combat.history->back();

	// Apply the Terms of the Agreement to the combatants.
	for(auto it : *agreement.GetTerms())
	{
		try
		{
			switch(it.first)
			{
				case Agreement::Term::BoarderSurrender:
					state = State::TargetVictory;
					break;
				case Agreement::Term::TargetSurrender:
					state = State::BoarderVictory;
					break;
				case Agreement::Term::BoarderGovernmentPacified:
					// TODO: Implement this term.
					break;
				case Agreement::Term::TargetGovernmentPacified:
					// TODO: Implement this term.
					break;
				case Agreement::Term::CreditPaymentToBoarder:
					if(combat.boarder->IsPlayerControlled())
						combat.player.Accounts().AddCredits(get<int64_t>(it.second));
					else if(combat.target->IsPlayerControlled())
						combat.player.Accounts().AddCredits(-get<int64_t>(it.second));
					break;
				case Agreement::Term::CreditPaymentToTarget:
					if(combat.target->IsPlayerControlled())
						combat.player.Accounts().AddCredits(get<int64_t>(it.second));
					else if(combat.boarder->IsPlayerControlled())
						combat.player.Accounts().AddCredits(-get<int64_t>(it.second));
					break;
				case Agreement::Term::CrewFromBoarder:
					int count = get<int>(it.second);
					combat.boarder->GetShip()->AddCrew(-count);
					combat.target->GetShip()->AddCrew(count);
					break;
				case Agreement::Term::CrewFromTarget:
					int count = get<int>(it.second);
					combat.target->GetShip()->AddCrew(-count);
					combat.boarder->GetShip()->AddCrew(count);
					break;
				case Agreement::Term::PassengersFromBoarder:
					int count = get<int>(it.second);
					combat.boarder->GetShip()->AddCrew(-count);
					// TODO: Implement this term. Might need to add a mission to drop them off nearby.
					break;
				case Agreement::Term::PassengersFromTarget:
					int count = get<int>(it.second);
					combat.target->GetShip()->AddCrew(-count);
					// TODO: Implement this term. Might need to add a mission to drop them off nearby.
					break;
				case Agreement::Term::PrisonersFromBoarder:
					int count = get<int>(it.second);
					combat.boarder->GetShip()->AddCrew(-count);
					// TODO: Implement this term. Might need to add a mission to drop them off nearby.
					break;
				case Agreement::Term::PrisonersFromTarget:
					int count = get<int>(it.second);
					combat.target->GetShip()->AddCrew(-count);
					// TODO: Implement this term. Might need to add a mission to drop them off nearby.
					break;
				default:
					Logger::LogError("BoardingCombat::Turn - Invalid Agreement Term: " + to_string(static_cast<int>(it.first)));
			}
		}
		catch(const bad_variant_access &e)
		{
			Logger::LogError("BoardingCombat::Turn - Invalid Agreement Term Value: " + to_string(static_cast<int>(it.first)));
		}
	}

	if(state != State::BoarderVictory && state != State::TargetVictory)
		state = State::Ended;
}



/**
 * Special BoardingCombat::Turn constructor for creating the first turn
 * of the combat. This lets us create a no-action turn at the start of the
 * History so that it is never empty. Having this in a separate constructor
 * prevents us from having to check for an empty History every time we
 * call the primary constructor.
 *
 * @param isFirstTurn This is a dummy argument to ensure that this version
 * 	of the constructor can only be called deliberately. This ensures that
 * 	the compiler will raise an error if the programmer attempts to
 * 	instantiate a Turn but forgets to supply any arguments.
 */
BoardingCombat::Turn::Turn(bool isFirstTurn) :
	boarderAction(Action::Null),
	targetAction(Action::Null),
	state(State::Isolated),
	boarderCasualties(0),
	targetCasualties(0),
	summary("")
{}



// --- End of BoardingCombat::Turn class implementation ---



// --- Start of BoardingCombat::Combatant class implementation ---

/**
 * Constructor for BoardingCombat::Combatant
 *
 * @param ship A ship that is participating in the boarding combat.
 * @param attackStrategy The strategy that the ship will use to attack.
 * @param defenseStrategy The strategy that the ship will use to defend.
 * @param isPlayerControlled Whether or not the ship is controlled by the player.
 */
BoardingCombat::Combatant::Combatant(
	shared_ptr<Ship> &ship,
	shared_ptr<Ship> &enemyShip,
	AttackStrategy attackStrategy,
	DefenseStrategy defenseStrategy,
	bool isBoarder,
	bool isPlayerControlled,
	bool isPlayerFlagship
) :
	ship(ship),
	attackStrategy(attackStrategy),
	defenseStrategy(defenseStrategy),
	automatedDefenders(static_cast<int>(ship->Attributes().Get("automated defenders"))),
	automatedInvaders(static_cast<int>(ship->Attributes().Get("automated invaders"))),
	odds(*ship, *enemyShip),
	crewAnalysisBefore(make_shared<Crew::ShipAnalysis>(ship->Crew(), isPlayerFlagship)),
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
	isPlayerControlled(isPlayerControlled),
	isPlayerFlagship(isPlayerFlagship)
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

	return GetDefenders();
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
 * 	This is a flat percentage of the combatant's current crew count,
 * 	defined in the game rules as BoardingCasualtyPercentagePerAction.
 */
int BoardingCombat::Combatant::CasualtyRollsPerAction() const
{
  return min(1, static_cast<int>(GameData::GetGamerules().BoardingCasualtyPercentagePerAction() * ship->Crew()));
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

	shared_ptr<Combatant> enemy = isBoarder ? combat.target : combat.boarder;

	double expectedCasualties = odds.AttackerCasualties(ship->Crew(), enemy->GetShip()->Crew());
	double victoryOdds = odds.Odds(ship->Crew(), enemy->GetShip()->Crew());

	int64_t expectedInvasionProfit = max(
		enemy->ExpectedCaptureProfit(expectedCasualties, victoryOdds),
		enemy->ExpectedProtectedPlunderProfit(expectedCasualties, victoryOdds)
	);

	bool withinRiskTolerance = false;

	switch(attackStrategy)
	{
		case(AttackStrategy::Cautious):
			withinRiskTolerance = victoryOdds > 0.99 && expectedCasualties < 0.5;
		case(AttackStrategy::Aggressive):
			withinRiskTolerance = victoryOdds > 0.99 && expectedCasualties < ship->ExtraCrew();
		case(AttackStrategy::Reckless):
			withinRiskTolerance = victoryOdds > 0.5;
		case(AttackStrategy::Fanatical):
			withinRiskTolerance = victoryOdds > 0.01;
		default:
		{
			Logger::LogError(
				"BoardingCombat::ConsiderAttacking cannot handle AttackStrategy: "
					+ to_string(static_cast<int>(attackStrategy)) + ". Defaulting to false."
			);
			withinRiskTolerance = false;
		}
	}

	return withinRiskTolerance && expectedInvasionProfit > 0;
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
	double expectedInvasionCasualties = odds.AttackerCasualties(ship->Crew(), enemy->Crew());
	double expectedDefenseCasualties = odds.DefenderCasualties(ship->Crew(), enemy->Crew());
	double victoryOdds = odds.Odds(ship->Crew(), enemy->Crew());
	double defeatOdds = enemy->GetOdds().Odds(enemy->Crew(), ship->Crew());
	bool isAttackingStronger = odds.AttackerPower(ship->Crew()) > odds.DefenderPower(ship->Crew());
	double selfDestructChance = ship->Attributes().Get("self destruct");
	double enemySelfDestructChance = enemy->GetShip()->Attributes().Get("self destruct");
	bool willingToAttack = ConsiderAttacking(combat);

	shared_ptr<Turn> latest = combat.GetHistory()->back();

	shared_ptr<map<Action, bool>> validActions = ValidActions(latest->state);

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
			if(defenseStrategy == DefenseStrategy::Deny && selfDestructChance)
				return Action::SelfDestruct;
			else
				return Action::Defend;
		}
	}

	// If we reach this point, at least one turn has taken place so we can
	// read the state directly without risking an error.
	State state = latest->state;
	Action latestAction = isBoarder ? latest->boarderAction : latest->targetAction;
	Action latestEnemyAction = isBoarder ? latest->targetAction : latest->boarderAction;

	// Neither combatant has invaded the other with troops, but their airlocks
	// are connected so they are can try to invade one another.
	if(state == State::Poised)
	{
		switch(defenseStrategy)
		{
			case(DefenseStrategy::Repel):
				return willingToAttack ? Action::Attack : Action::Defend;
			case(DefenseStrategy::Counter):
				return willingToAttack && (latestEnemyAction == Action::Defend || latestEnemyAction == Action::Plunder)
					? Action::Attack
					: Action::Defend;
			case(DefenseStrategy::Deny):
				return willingToAttack
					? Action::Attack
					: latestEnemyAction == Action::Attack && defeatOdds > 0.01 && selfDestructChance
						? Action::SelfDestruct
						: Action::Defend;
			default:
			{
				Logger::LogError("BoardingCombat::DetermineAction with State::Poised cannot handle DefenseStrategy: " + to_string(static_cast<int>(defenseStrategy)) + ". Defaulting to Defend action.");
				return Action::Defend;
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
			return Action::Defend;
	}

	// This combatant has fully conquered the enemy combatant.
	if(isBoarder ? state == State::BoarderVictory : state == State::TargetVictory)
	{
		if(hasCaptureObjective && ship->ExtraCrew() >= enemy->GetShip()->RequiredCrew())
			return Action::Capture;
		else if(hasPlunderObjective && ship->Cargo().Free() > 0)
			return Action::Raid;
		else
			return Action::Leave;
	}

	// This combatant been has fully conquered the enemy combatant.
	if(isBoarder ? state == State::TargetVictory : state == State::BoarderVictory)
		return Action::Null;
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



int BoardingCombat::Combatant::GetDefenders() const
{
  return ship->Crew() + static_cast<int>(ship->Attributes().Get("automated defenders"));
}



int BoardingCombat::Combatant::GetInvaders() const
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
 * @return Whether or not the combatant is directly controlled by the
 * 	player, either because it is the player's flagship or because they
 * 	have chosen to control it directly via the Boarding Panel.
 */
bool BoardingCombat::Combatant::IsPlayerControlled() const
{
  return false;
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
		return odds.AttackerPower(GetInvaders());

	double defenseMultiplier = 1.0;

	if(successfulBinaryAction == Action::SelfDestruct)
		defenseMultiplier *= GameData::GetGamerules().BoardingSelfDestructCasualtyPowerMultiplier();

	return odds.DefenderPower(GetDefenders() * defenseMultiplier);
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

	double power = Power(action);
	shared_ptr<Combatant> enemy = isBoarder ? combat.target : combat.boarder;
	double enemyPower = enemy->Power(action);
	double totalPower = power + enemyPower;

	bool succeeded = false;

	switch(action)
	{
		case Action::Negotiate:
			succeeded = combat.IsLanguageShared() && !combat.HasNegotiationFailed();
			break;
		case Action::Resolve:
			succeeded = negotiationState == NegotiationState::Active;
			break;
		case Action::Plunder:
			succeeded = Random::Real() * totalPower >= power;
			break;
		case Action::SelfDestruct:
			succeeded = Random::Real() * totalPower >= power && Random::Real() < ship->Attributes().Get("self destruct");
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
shared_ptr<map<BoardingCombat::Action, bool>> &BoardingCombat::Combatant::ValidActions(State state) const
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



// --- Start of BoardingCombat::Agreement class implementation ---


BoardingCombat::Agreement::Agreement(VariantMap<Term> &terms) :
	terms(terms)
{
}


/**
 * Adds a new Term to the Agreement, or amends an existing Term with a new value.
 *
 * @param term The Term to add or amend.
 * @param value The value to assign to the Term.
 *
 * @return A reference to the Agreement object.
 */
BoardingCombat::Agreement &BoardingCombat::Agreement::AddOrAmendTerm(
	Term term,
	variant<bool, double, int> value
) {
	terms->insert_or_assign(term, value);
	return *this;
}



/**
 * Removes a Term from the Agreement.
 * If the Term does not exist, this function will have no effect.
 * If the Term exists, it will be removed from the Agreement.
 *
 * @param term The Term to remove.
 *
 * @return A reference to the Agreement object.
 */
BoardingCombat::Agreement &BoardingCombat::Agreement::RemoveTerm(Term term)
{
  terms->erase(term);
	return *this;
}



const BoardingCombat::VariantMap<BoardingCombat::Agreement::Term> &BoardingCombat::Agreement::GetTerms() const
{
  return terms;
}

const bool BoardingCombat::Agreement::HasTerm(Term term) const
{
  return terms->find(term) != terms->end();
}

// --- End of BoardingCombat::Agreement class implementation ---



// --- Start of BoardingCombat static member initialization ---


/**
 * Some actions are considered aggressive, while others are considered
 * defensive. This is used to determine whether a combatant uses their
 * attack or defense power when performing a given action.
 */
const std::map<BoardingCombat::Action, bool> BoardingCombat::aggressionByAction = {
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



// --- End of BoardingCombat static member initialisation ---



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
 * @param boarderAttackStrategy The strategy that the boarder will use to attack.
 * @param targetAttackStrategy The strategy that the target will use to attack.
 * @param boarderDefenseStrategy The strategy that the boarder will use to defend.
 * @param targetDefenseStrategy The strategy that the target will use to defend.
 * @param boarderIsPlayerControlled Whether or not the boarder is controlled by the player.
 * @param boarderIsPlayerFlagship Whether or not the boarder is the player's flagship.
 * @param targetIsPlayerControlled Whether or not the target is controlled by the player.
 * @param targetIsPlayerFlagship Whether or not the target is the player's flagship.
 */
BoardingCombat::BoardingCombat(
	PlayerInfo &player,
	shared_ptr<Ship> &boarderShip,
	shared_ptr<Ship> &targetShip,
	AttackStrategy boarderAttackStrategy,
	AttackStrategy targetAttackStrategy,
	DefenseStrategy boarderDefenseStrategy,
	DefenseStrategy targetDefenseStrategy,
	bool boarderIsPlayerControlled,
	bool boarderIsPlayerFlagship,
	bool targetIsPlayerControlled,
	bool targetIsPlayerFlagship
) :
	player(player),
	boarder(make_shared<BoardingCombat::Combatant>(
		boarderShip,
		targetShip,
		boarderAttackStrategy,
		boarderDefenseStrategy,
		true,
		boarderIsPlayerControlled,
		boarderIsPlayerFlagship
	)),
	target(make_shared<BoardingCombat::Combatant>(
		targetShip,
		boarderShip,
		targetAttackStrategy,
		targetDefenseStrategy,
		false,
		targetIsPlayerControlled,
		targetIsPlayerFlagship
	)),
	history(make_shared<History<Turn>>(make_shared<Turn>(true)))
{
	// Determine whether or not the combatants share a language, and can therefore negotiate.
	string boarderLanguage = boarder->GetShip()->GetGovernment()->Language();
	string targetLanguage = target->GetShip()->GetGovernment()->Language();

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
	}
}



const std::shared_ptr<BoardingCombat::History<BoardingCombat::Turn>> &BoardingCombat::GetHistory() const
{
  return history;
}



const std::shared_ptr<BoardingCombat::Combatant> &BoardingCombat::GetPlayerCombatant() const
{
  if(boarderIsPlayerControlled)
		return boarder;
	else if(targetIsPlayerControlled)
		return target;
	else
		return nullptr;
}



const std::shared_ptr<BoardingCombat::Combatant> &BoardingCombat::GetPlayerEnemy() const
{
  if(boarderIsPlayerControlled)
		return target;
	else if(targetIsPlayerControlled)
		return boarder;
	else
		return nullptr;
}



bool BoardingCombat::HasNegotiationFailed() const
{
  return hasNegotiationFailed;
}



bool BoardingCombat::IsLanguageShared() const
{
	return isLanguageShared;
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
 */
const shared_ptr<BoardingCombat::Turn> BoardingCombat::Step(Action playerAction)
{
	// Check if the player is trying to take an invalid action.
	if(!GetPlayerCombatant()->ValidActions(history->back()->state)->at(playerAction))
	{
		Logger::LogError("BoardingCombat::Step - player tried to take an invalid action:" + to_string(static_cast<int>(playerAction)));
		return nullptr;
	}

	// Create a new Turn object to represent the next step in the combat.
	shared_ptr<Turn> next = boarderIsPlayerControlled
		? make_shared<Turn>(*this, playerAction, target->DetermineAction(*this))
		: make_shared<Turn>(*this, boarder->DetermineAction(*this), playerAction);

	// Add the new Turn to the History.
	history->push_back(next);

	return next;
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
