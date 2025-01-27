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

#include "CaptureOdds.h"
#include "Crew.h"
#include "Outfit.h"
#include "Ship.h"

#include <memory>
#include <string>
#include <vector>
#include <variant>

class Outfit;
class Ship;


// This class represents a hostile boarding action between two combatants.
class BoardingCombat {
public:
	template <typename T>
	using History = std::vector<std::shared_ptr<T>>;

	template <typename T>
	using VariantMap = std::shared_ptr<std::map<T, std::variant<bool, double, int, int64_t>>>;

	// The actions that a combatant can take during a single turn of combat.
	enum class Action {
		Null, // The combatant does not take an action. Used during Turn 0 or when defeated.
		// The first section of actions do not end the combat, except for SelfDestruct if successful.
		Attack, // Try to capture the enemy or use attack power to repel invaders.
		Defend,  // Focus on preventing the enemy from invading.
		Plunder, // Try to steal an outfit or cargo from the enemy.
		SelfDestruct, // Attempt to destroy yourself, denying technology and possibly killing some invaders.
		// The second section of actions involve non-violent resolutions to the combat. They require a shared language.
		Negotiate, // Ask the enemy to negotiate. If the player is involved, opens a mission dialogue.
		Resolve, // End the combat without further violence, applying the agreed-upon Terms.
		// The third section of actions are final as they end the combat.
		Capture, // Final. Requires victory. Repair the ship and take control of it, transferring crew.
		Raid, // Final. Requires isolation or victory. Plunders as much value as possible. Destroys enemy if no plunder remains.
		Leave, // Final. Requires isolation or victory. Withdraws from the ship, leaving it disabled. Others can board afterward.
		Destroy // Final. Requires isolation or victory. Destroys the enemy ship, preventing any further boarding from others.
	};

	// The current state of the boarding combat.
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
	// If the negotiation action succeeds, they can discuss terms and potentially end the combat.
	// If one combatant refuses to negotiate, the other combatant can no longer take the Negotiate action.
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

	// This defines the conditions under which a ship will attack.
	enum class AttackStrategy {
		Cautious, // Only attack if victory is assured and no friendly casualties are expected.
		Aggressive, // Attack if victory is assured and expected casualties don't exceed extra crew.
		Reckless, // Attack if victory is likely, regardless of the casualties.
		Fanatical // Attack as long as victory is possible.
	};

	enum class DefenseStrategy {
		Repel, // Focus on preventing the ship's defeat. If overwhelmed, might try to self-destruct.
		Counter, // As Repel, but also prevent the ship from attacking unless the enemy just defended or plundered.
		Deny // Attempt to self-destruct if the ship might be captured or plundered.
	};



	// During a negotiation, the combatants can make an Agreement to resolve
	// the combat without any further violence. This class is used to model
	// a set of Terms of that can be enacted if both combatants agree.
	class Agreement	{
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

		Agreement(VariantMap<Term> &terms);

		Agreement &AddOrAmendTerm(Term term, std::variant<bool, double, int> value);

		Agreement &RemoveTerm(Term term);

		const VariantMap<Term> &GetTerms() const;

		const bool HasTerm(Term term) const;

	private:
		VariantMap<Term> terms;
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
	 */
	class Turn {
	public:
		Turn(
			BoardingCombat &combat,
			Action boarderAction,
			Action targetAction
		);

		Turn(BoardingCombat &combat, Agreement &agreement);

		Turn(bool isFirstTurn);

		Action boarderAction;
		Action targetAction;

		Action successfulBoarderAction;
		Action successfulTargetAction;

		int boarderCasualties;
		int targetCasualties;

		State state;
		NegotiationState negotiationState;

		std::string summary;
	};



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
			AttackStrategy attackStrategy,
			DefenseStrategy defenseStrategy,
			bool isBoarder,
			bool isPlayerControlled,
			bool isPlayerFlagship
		);

		int ApplyCasualty(bool isDefender);

		Action AttemptBinaryAction(const BoardingCombat & combat, Action action);

		std::shared_ptr<Crew::CasualtyAnalysis> CasualtyAnalysis();

		int CasualtyRollsPerAction() const;

		bool ConsiderAttacking(const BoardingCombat &combat);

		int Crew() const;

		const std::string &CrewDisplayName(bool startOfSentence = false);

		Action DetermineAction(const BoardingCombat &combat);

		int64_t ExpectedCaptureProfit(double expectedInvasionCasualties, double victoryOdds);

		int64_t ExpectedProtectedPlunderProfit(double expectedInvasionCasualties, double victoryOdds);

		int GetDefenders() const;

    int GetInvaders() const;

    CaptureOdds GetOdds() const;

		std::shared_ptr<Ship> &GetShip();

		bool IsPlayerControlled() const;

		double PostCaptureSurvivalOdds() const;

		double Power(Action action, Action successfulBinaryAction = Action::Null) const;

		std::shared_ptr<std::map<Action, bool>> &ValidActions(State state) const;

	private:
		std::shared_ptr<Ship> ship;

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
		bool isPlayerFlagship;

		// If a ship loses crew members, we need to trigger consequences for them.
		// We use this flag to check whether or not we have done that yet.
		bool hasResolvedBoardingCasualties = false;
	};



	BoardingCombat(
		PlayerInfo &player,
		std::shared_ptr<Ship> &boarderShip,
		std::shared_ptr<Ship> &targetShip,
		AttackStrategy boarderAttackStrategy,
		AttackStrategy targetAttackStrategy,
		DefenseStrategy boarderDefenseStrategy,
		DefenseStrategy targetDefenseStrategy,
		bool boarderIsPlayerControlled,
		bool boarderIsPlayerFlagship,
		bool targetIsPlayerControlled,
		bool targetIsPlayerFlagship
	);

	const std::shared_ptr<History<Turn>> &GetHistory() const;

	const std::shared_ptr<Combatant> &GetPlayerCombatant() const;
	const std::shared_ptr<Combatant> &GetPlayerEnemy() const;

	State GuessNextState(State state, Action boarderAction, Action targetAction) const;

	bool IsLanguageShared() const;

	bool HasNegotiationFailed() const;

	// Automatically resolve the entire boarding combat. Used when the player
	// is not involved. Returns a message summarising the result.
	const std::string ResolveAutomatically();

	// Resolve the next turn of combat automatically. Used when the player is not involved.
	const std::shared_ptr<Turn> Step();

	// Resolve the next turn of combat, based on the action chosen by the player.
	const std::shared_ptr<Turn> Step(Action playerAction);

private:
	static const std::map<Action, bool> aggressionByAction;
	static const std::map<std::string, double> postCaptureSurvivalOddsByCategory;

	static std::string BuildCrewDisplayName(std::shared_ptr<Ship> &ship, bool startOfSentence = false);

	PlayerInfo &player;

	bool boarderIsPlayerControlled;
	bool targetIsPlayerControlled;

	bool hasNegotiationFailed = false;
	bool isLanguageShared;

	std::shared_ptr<Combatant> boarder;
	std::shared_ptr<Combatant> target;

	std::shared_ptr<History<Turn>> history;
};


