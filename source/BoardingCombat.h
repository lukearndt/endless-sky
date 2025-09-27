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

#pragma once

#include "Boarding.h"
#include "BoardingProbability.h"
#include "Crew.h"
#include "Outfit.h"
#include "PlayerInfo.h"
#include "Plunder.h"
#include "Ship.h"

#include <memory>
#include <string>
#include <vector>
#include <variant>

class Outfit;
class PlayerInfo;
class Ship;


// This class represents a hostile boarding action between two combatants.
class BoardingCombat : public Boarding {
public:
	// Forward declarations.
	class Combatant;
	class SituationReport;
	class Turn;

	// The result of the process that rolls for casualties for an Action.
	using CasualtyReport = struct {
		State state;
		int boarderCasualties;
		int targetCasualties;
	};

	/**
	 * This class represents a single step in the overall combat.
	 *
	 * A Turn contains a single Action from each combatant. The result of
	 * each action is resolved immediately by the constructor, so that
	 * there is no need to mutate a Turn after its creation. However,
	 * resolving a Turn of combat often mutates the combatants themselves.
	 *
	 * While each combatant can only take one Action per Turn, resolving
	 * those Actions can involve a number of Rolls. For example, when two
	 * ships with large populations are in combat and at least one of them
	 * chooses to Attack, the ensuing Turn perform several Rolls in order
	 * to determine the casualties.
	 *
	 * There are a couple of key reasons for this one-to-many relationship:
	 *
	 * Firstly, boarding would be lengthy and tedious if the player had to
	 * press the Attack button hundreds of times to resolve a large combat.
	 *
	 * Secondly, it allows each Action to represent a similar amount of
	 * time. For instance, it should take longer to Plunder an 80-ton
	 * outfit than to eliminate a single enemy crew member. Similarly, it
	 * prevents ships with self destruct systems from attempting to
	 * activate them once for every crew member on the enemy ship.
	 *
	 * Not all Actions are available at all times. For example, a disabled
	 * ship that has been boarded cannot Plunder unless the boarder tries
	 * to invade them, they repel the invasion, and then they invade the
	 * boarder in turn and achieve victory.
	 *
	 * Some Actions are Final, meaning that they end the combat. These
	 * include Capture, Raid, Leave, and Destroy, and they can only be
	 * performed when the enemy combatant is unable to prevent them.
	 *
	 * If a combatant tries to take an Action that was not available to
	 * them at all, the Turn will not be created. The constructor will
	 * instead raise an error, which we immediately catch and log.
	 * Ideally this would never happen, since the rest of the game code
	 * should prevent the AI or player from taking invalid Actions, but
	 * bugs are always possible and we want to make sure that the game
	 * does not allow boarding combats to proceed along invalid paths.
	 *
	 * On the other hand, if a combatant tries to take an Action that was
	 * available to them at the time they took it, but becomes invalid due
	 * to the other combatant's Action, the Turn will be created and that
	 * combatant will be assigned a different Action. The new Action will
	 * usually be something passive like Null.
	 */
	class Turn {
	public:
		Turn(
			BoardingCombat &combat,
			Action::Activity boarderIntent,
			Action::Activity targetIntent
		);

		Turn(BoardingCombat &combat, Offer &agreement);

		Turn(
			std::shared_ptr<Combatant> &boarder,
			std::shared_ptr<Combatant> &target
		);

		std::shared_ptr<Turn> previous;
		State state;
		Negotiation negotiation;

		Action::Activity boarderIntent;
		Action::Activity targetIntent;

		int boarderActionIndex;
		int targetActionIndex;

		std::vector<Action> actions;

		Action BoarderAction() const;
		Action TargetAction() const;

		CasualtyReport casualties;

		std::shared_ptr<SituationReport> boarderSituationReport;
		std::shared_ptr<SituationReport> targetSituationReport;
		std::vector<std::string> messages;
	};

	// The history of the combat, represented as a list of Turns.
	using History = std::vector<std::shared_ptr<Turn>>;

	/**
	 * This class represents one of the participants of the boarding combat.
	 * We need to cache a lot of information about each combatant, such as
	 * crew, strategy, and objectives. This allows them to make decisions
	 * each turn without having to recalculate everything each time.
	 */
	class Combatant {
	public:
		Combatant(
			std::shared_ptr<Ship> &ship,
			std::shared_ptr<Ship> &enemyShip,
			std::shared_ptr<BoardingProbability> &probability,
			bool isBoarder,
			bool isPlayerControlled, // True if the player is using the BoardingPanel.
			const std::vector<std::shared_ptr<Ship>> &playerFleet = {} // Unfortunate dependency, used for displaying a cargo space message.
		);

		int ApplyCasualty(bool isDefender);

		Action AttemptAction(
			const BoardingCombat &combat,
			State state,
			Negotiation negotiation,
			Action::Activity intent,
			Action::Activity enemyIntent,
			double enemyPower
		);

		const std::shared_ptr<Action::ObjectiveCondition> ValidObjectives(
			State state,
			Negotiation negotiation
		) const;

		std::shared_ptr<Crew::CasualtyAnalysis> CasualtyAnalysis();

		int CasualtyRolls(
			State state,
			Negotiation negotiation,
			Action::Objective objective
		) const;

		bool ConsiderAttacking(const std::shared_ptr<SituationReport> &report) const;
		bool ConsiderCapturing(const std::shared_ptr<SituationReport> &report) const;
		bool ConsiderDestroying(const std::shared_ptr<SituationReport> &report) const;
		bool ConsiderPlundering(const std::shared_ptr<SituationReport> &report) const;
		bool ConsiderSelfDestructing(const std::shared_ptr<SituationReport> &report) const;

		int Crew() const;

		const std::string &CrewDisplayName(bool startOfSentence = false);

		Action::Activity DetermineIntent(const std::shared_ptr<SituationReport> &report) const;
		Action::Activity DetermineCaptureIntent(const std::shared_ptr<SituationReport> &report) const;
		Action::Activity DetermineDefaultIntent(const std::shared_ptr<SituationReport> &report) const;
		Action::Activity DeterminePlunderIntent(const std::shared_ptr<SituationReport> &report) const;

		State MaybeChangeState(
			State state,
			Action::Activity actual,
			const Action::Activity enemyIntent
		) const;

		Negotiation MaybeChangeNegotiation(
			Negotiation negotiation,
			Action::Activity actual
		) const;

		double ExpectedInvasionCasualties(int enemyDefenders) const;
		double ExpectedDefensiveCasualties(int enemyInvaders) const;
		double DefensiveVictoryProbability(int enemyInvaders) const;
		double InvasionVictoryProbability(int enemyDefenders) const;

		int64_t ExpectedCaptureProfit(double expectedInvasionCasualties, double victoryOdds);
		int64_t ExpectedInvasionProfit(
			const std::shared_ptr<Combatant> &enemy,
			int64_t expectedCaptureProfit,
			int64_t expectedPlunderProfit,
			int64_t expectedProtectedPlunderProfit
		);
		int64_t ExpectedPlunderProfit();
		int64_t ExpectedProtectedPlunderProfit(double expectedInvasionCasualties, double victoryOdds);

		double AttackPower() const;
		double DefensePower() const;

		double ExpectedSelfDestructCasualtiesInflicted(const std::shared_ptr<Combatant> &enemy) const;
		double SelfDestructProbability(const std::shared_ptr<Combatant> &enemy) const;

		int Defenders() const;
    int Invaders() const;

    BoardingProbability GetOdds() const;

		std::shared_ptr<Ship> &GetShip();
		const std::shared_ptr<Ship> &GetShip() const;

		bool IsBoarder() const;
		bool IsPlayerControlled() const;
		bool IsPlunderFinished() const;

		std::shared_ptr<Plunder::Session> &PlunderSession();

		const std::vector<std::shared_ptr<Plunder>> &PlunderOptions() const;

		double PostCaptureSurvivalOdds() const;

		double ActionPower(Action::Objective objective) const;
		double CasualtyPower(Action::Objective objective) const;

	private:
		std::shared_ptr<Ship> ship;
		std::shared_ptr<Plunder::Session> plunderSession;

		Ship::BoardingGoal goal;
		AttackStrategy attackStrategy;
		DefenseStrategy defenseStrategy;

		int automatedDefenders;
		int automatedInvaders;

		std::shared_ptr<BoardingProbability> probability;

		std::shared_ptr<Crew::CasualtyAnalysis> casualtyAnalysis = nullptr;
		std::shared_ptr<Crew::ShipAnalysis> crewAnalysisAfter = nullptr;
		std::shared_ptr<Crew::ShipAnalysis> crewAnalysisBefore;

		std::string crewDisplayNameMidSentence;
		std::string crewDisplayNameStartOfSentence;

		int64_t captureValue;
		int64_t chassisValue;
		int64_t protectedPlunderValue;

		int64_t expectedCostPerBoardingCasualty;
		int64_t expectedCostPerCasualtyPostCapture;
		int64_t expectedPostCaptureCasualtyCosts;

		double postCaptureSurvivalOdds;

		bool hasCaptureGoal;
		bool hasPlunderGoal;

		bool isBoarder;
		bool isPlayerControlled;

		// If a ship loses crew members, we need to trigger consequences for them.
		// We use this flag to check whether or not we have done that yet.
		bool hasResolvedBoardingCasualties = false;
	};



	class SituationReport {
	public:
		SituationReport(
			const std::shared_ptr<Combatant> &combatant,
			const std::shared_ptr<Combatant> &enemy,
			const Turn &turn,
			const std::shared_ptr<SituationReport> &previousReport
		);

		const std::shared_ptr<Combatant> &combatant;
		const std::shared_ptr<Combatant> &enemy;
		const Turn &turn;
		const std::shared_ptr<SituationReport> &previousReport;

		const std::shared_ptr<Ship> &ship;
		const std::shared_ptr<Ship> &enemyShip;

		bool isBoarder;
		bool actedFirst;
		bool isConquered;
		bool isEnemyConquered;
		bool isEnemyInvading;

		const Action &latestAction;
		const Action &enemyLatestAction;

		int invaders;
		int defenders;
		int crew;

		int enemyInvaders;
		int enemyDefenders;
		int enemyCrew;

		int cargoSpace;
		int enemyCargoSpace;

		BoardingProbability::Report probabilityReport;

		double attackPower;
		double defensePower;

		double enemyAttackPower;
		double enemyDefensePower;

		double attackingTotalPower;
		double defendingTotalPower;

		double minimumTurnsToVictory;
		double minimumTurnsToDefeat;

		double selfDestructProbability;

		double enemySelfDestructProbability;
		double enemyCumulativeSelfDestructProbability;
		double enemySelfDestructCasualtyPower;

		double expectedSelfDestructCasualties;
		double expectedInvasionCasualties;
		double expectedDefensiveCasualties;

		double invasionVictoryProbability;
		double defensiveVictoryProbability;
		double postCaptureSurvivalProbability;

		int64_t expectedCaptureProfit;
		int64_t expectedPlunderProfit;
		int64_t expectedProtectedPlunderProfit;
		int64_t expectedInvasionProfit;

		bool isPlunderFinished;
		const std::vector<std::shared_ptr<Plunder>> &plunderOptions;

		const std::shared_ptr<Action::ObjectiveCondition> validObjectives;
		const std::shared_ptr<Action::ObjectiveCondition> enemyValidObjectives;
	};



	// --- Start of core BoardingCombat class implementation ---

	BoardingCombat(
		PlayerInfo &player,
		std::shared_ptr<Ship> &boarderShip,
		std::shared_ptr<Ship> &targetShip
	);

	Action::Result ApplyAction(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);
	Action::Result ApplyNull(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);

	Action::Result ApplyAttack(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);
	Action::Result ApplyDefend(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);
	Action::Result ApplyNegotiate(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);
	Action::Result ApplyReject(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);
	Action::Result ApplyResolve(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);
	Action::Result ApplyPlunder(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);
	Action::Result ApplySelfDestruct(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);

	Action::Result ApplyCapture(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);
	Action::Result ApplyDestroy(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);
	Action::Result ApplyLeave(
		State state,
		Negotiation negotiation,
		bool isBoarder,
		const Action &action,
		const Action &enemyAction
	);

	bool ApplyCasualtyConsequences();

	CasualtyReport RollForCasualties(
		State state,
		bool isBoarder,
		const Action &boarderAction,
		const Action &targetAction
	);

	const std::shared_ptr<History> &GetHistory() const;

	int CountInactiveFrames() const;

	const std::shared_ptr<Combatant> GetPlayerCombatant() const;
	const std::shared_ptr<Combatant> GetPlayerEnemy() const;

	bool IsLanguageShared() const;

	bool IsPlayerEnemyConquered() const;
	bool IsPlayerConquered() const;

	// Automatically resolve the entire combat. Used when the player
	// is not involved. Returns a message summarising the result.
	const std::string ResolveAutomatically();

	// Resolve the next turn of combat automatically. Used when the player is not involved.
	const std::shared_ptr<Turn> Step();

	// Resolve the next turn of combat, based on the action chosen by the player.
	const std::shared_ptr<Turn> Step(Action::Activity playerIntent);

private:
	static const std::map<std::string, double> postCaptureSurvivalOddsByCategory;

	static std::string BuildCrewDisplayName(std::shared_ptr<Ship> &ship, bool startOfSentence = false);

	PlayerInfo &player;
	Ship::BoardingGoal boardingObjective;

	bool usingBoardingPanel;

	bool pendingCasualtyConsequences = false;
	bool hasNegotiationFailed = false;
	bool isLanguageShared;

	std::shared_ptr<BoardingProbability> probability;

	std::shared_ptr<Combatant> boarder;
	std::shared_ptr<Combatant> target;

	std::shared_ptr<History> history;
};


