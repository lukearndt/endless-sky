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
#include "CaptureOdds.h"
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
	 * If a combatant tries to take an Action that is not available to them,
	 * the Turn will not be created. Instead, the constructor will raise
	 * an error, which we catch immediately and log before handling.
	 * Ideally this would never happen, since the rest of the game code
	 * should prevent the AI or player from taking invalid Actions, but
	 * bugs are always possible and we want to make sure that the game
	 * does not allow boarding combats to proceed along invalid paths.
	 *
	 * @param combat A reference to the BoardingCombat that is taking place.
	 * @param boarderAction The Action that the boarder will take this turn.
	 * @param targetAction The Action that the target will take this turn.
	 * @param boarderActionDetails Extra details about the boarder's action.
	 * @param targetActionDetails Extra details about the target's action.
	 */
	class Turn {
	public:
		Turn(
			BoardingCombat &combat,
			Action boarderAction,
			Action targetAction,
			ActionDetails boarderActionDetails = 0,
			ActionDetails targetActionDetails = 0
		);

		Turn(BoardingCombat &combat, Offer &agreement);

		Turn(
			const std::shared_ptr<Combatant> &boarder,
			const std::shared_ptr<Combatant> &target
		);

		Action boarderAction;
		Action targetAction;

		Action successfulBoarderAction;
		Action successfulTargetAction;

		State state;
		NegotiationState negotiationState;

		int boarderCasualties;
		int targetCasualties;

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
			bool isBoarder,
			bool isPlayerControlled, // True if the player is using the BoardingPanel.
			const std::vector<std::shared_ptr<Ship>> &playerFleet = {} // Unfortunate dependency, used for displaying a cargo space message.
		);

		int ApplyCasualty(bool isDefender);

		Action AttemptBinaryAction(const BoardingCombat & combat, Action action);

		const std::shared_ptr<std::map<Action, bool>> AvailableActions(State state) const;

		std::shared_ptr<Crew::CasualtyAnalysis> CasualtyAnalysis();

		int CasualtyRollsPerAction(Action action) const;

		bool ConsiderAttacking(const BoardingCombat &combat);

		int Crew() const;

		const std::string &CrewDisplayName(bool startOfSentence = false);

		Action DetermineAction(const BoardingCombat &combat);

		int64_t ExpectedCaptureProfit(double expectedInvasionCasualties, double victoryOdds);
		int64_t ExpectedInvasionProfit(
			const std::shared_ptr<Combatant> &enemy,
			int64_t expectedCaptureProfit,
			int64_t expectedProtectedPlunderProfit
		);
		int64_t ExpectedProtectedPlunderProfit(double expectedInvasionCasualties, double victoryOdds);

		double AttackPower() const;
		double DefensePower() const;

		double ExpectedSelfDestructCasualties(const std::shared_ptr<Combatant> &enemy) const;
		double SelfDestructProbability(const std::shared_ptr<Combatant> &enemy) const;

		int Defenders() const;
    int Invaders() const;

    CaptureOdds GetOdds() const;

		std::shared_ptr<Ship> &GetShip();
		const std::shared_ptr<Ship> &GetShip() const;

		bool IsBoarder() const;
		bool IsPlayerControlled() const;

		const std::vector<std::shared_ptr<Plunder>> &PlunderOptions() const;

		double PostCaptureSurvivalOdds() const;

		double Power(Action action, Action successfulBinaryAction = Action::Null) const;


	private:
		std::shared_ptr<Ship> ship;
		std::shared_ptr<Plunder::Session> plunderSession;

		AttackStrategy attackStrategy;
		DefenseStrategy defenseStrategy;

		int automatedDefenders;
		int automatedInvaders;

		CaptureOdds odds;

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

		bool hasCaptureObjective;
		bool hasPlunderObjective;

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
			const Turn &turn
		);

		const std::shared_ptr<Combatant> &combatant;
		const std::shared_ptr<Combatant> &enemy;
		const Turn &turn;

		const std::shared_ptr<Ship> &ship;
		const std::shared_ptr<Ship> &enemyShip;

		bool isConquered;
		bool isEnemyConquered;

		int invaders;
		int defenders;
		int crew;

		int enemyInvaders;
		int enemyDefenders;
		int enemyCrew;

		int cargoSpace;

		double attackPower;
		double defensePower;

		double enemyAttackPower;
		double enemyDefensePower;

		double attackingTotalPower;
		double defendingTotalPower;

		double minimumTurnsToVictory;
		double minimumTurnsToDefeat;

		double enemySelfDestructProbability;
		double cumulativeSelfDestructProbability;
		double selfDestructCasualtyPower;

		double expectedSelfDestructCasualties;
		double expectedInvasionCasualties;
		double expectedDefensiveCasualties;

		double invasionVictoryProbability;
		double defensiveVictoryProbability;
		double postCaptureSurvivalProbability;

		int64_t expectedCaptureProfit;
		int64_t expectedProtectedPlunderProfit;
		int64_t expectedInvasionProfit;

		const std::vector<std::shared_ptr<Plunder>> &plunderOptions;
		const std::map<Action, bool> &availableActions;
	};



	// --- Start of core BoardingCombat class implementation ---

	BoardingCombat(
		PlayerInfo &player,
		std::shared_ptr<Ship> &boarderShip,
		std::shared_ptr<Ship> &targetShip
	);

	const std::shared_ptr<History> &GetHistory() const;

	int CountInactiveFrames() const;

	const std::shared_ptr<Combatant> GetPlayerCombatant() const;
	const std::shared_ptr<Combatant> GetPlayerEnemy() const;

	State GuessNextState(State state, Action boarderAction, Action targetAction) const;

	bool IsLanguageShared() const;

	bool IsPlayerEnemyConquered() const;
	bool IsPlayerConquered() const;

	// Automatically resolve the entire combat. Used when the player
	// is not involved. Returns a message summarising the result.
	const std::string ResolveAutomatically();

	// Resolve the next turn of combat automatically. Used when the player is not involved.
	const std::shared_ptr<Turn> Step();

	// Resolve the next turn of combat, based on the action chosen by the player.
	const std::shared_ptr<Turn> Step(Action playerAction, ActionDetails playerActionDetails = 0);

private:
	static const std::map<Action, bool> aggressionByAction;
	static const std::map<std::string, double> postCaptureSurvivalOddsByCategory;

	static std::string BuildCrewDisplayName(std::shared_ptr<Ship> &ship, bool startOfSentence = false);
	static bool IsValidActionDetails(Action action, ActionDetails details);
	static bool IsValidActionDetails(Action action, int details);
	static bool IsValidActionDetails(Action action, Offer details);
	static bool IsValidActionDetails(Action action, bool details);

	PlayerInfo &player;
	Ship::BoardingObjective boardingObjective;
	bool usingBoardingPanel;

	bool hasNegotiationFailed = false;
	bool isLanguageShared;

	std::shared_ptr<Combatant> boarder;
	std::shared_ptr<Combatant> target;

	std::shared_ptr<History> history;
};


