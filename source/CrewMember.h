/* CrewMember.h
Copyright (c) 2024 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
*/

#ifndef CREW_MEMBER_H_
#define CREW_MEMBER_H_

#include "DataNode.h"
#include "Ship.h"

class CrewMember
	{
	public:
		// Load a definition for a crew member.
		void Load(const DataNode &node);

		bool AvoidsEscorts() const;
		bool AvoidsFlagship() const;
		bool AvoidsParked() const;
		bool OnlyParked() const;
		const bool CanOccurOnShip(const std::shared_ptr<Ship> ship, const bool isFlagship) const;
		const bool WouldOutrankOnShip(const std::shared_ptr<Ship> ship, const std::string &rankingCrewId = "") const;
		const std::map<std::string, bool> ShipCategories() const;
		const std::string Id() const;
		const std::string Name() const;
		const std::vector<int64_t> OccursAt() const;
		const std::vector<std::string> AvoidsShipCategories() const;
		const std::vector<std::string> OnlyShipCategories() const;
		int64_t DeathBenefit() const;
		int64_t DeathShares() const;
    int64_t NumberOnShip(const std::shared_ptr<Ship> &ship, const bool isFlagship, const bool includeExtras = true) const;
		int64_t ParkedSalary() const;
		int64_t ParkedShares() const;
		int64_t Salary() const;
		int64_t Shares() const;
		int64_t SharesPerCombatLevel() const;
		int64_t SharesPerLicense() const;
		int64_t ShipPopulationPerMember() const;
		int64_t TotalShares(int combatLevel, int licenseCount) const;

		// We only want there to be a single shared_ptr for each CrewMember object.
		// Instead of calling make_shared<CrewMember>(...) directly, use this function.
		// That way, we can compare shared_ptr<CrewMember> objects when analysing the crew.
		static std::map<const CrewMember*, std::shared_ptr<CrewMember>> SharedPtrCache;
		const std::shared_ptr<CrewMember> GetSharedPtr() const;

	private:

		// If true, the crew member will not appear on escorts
		bool avoidsEscorts = false;
		// If true, the crew member will not appear on the flagship
		bool avoidsFlagship = false;
		// If true, the crew member will not appear on parked ships
		bool avoidsParked = false;
		// If true, the crew member will only appear on parked ships
		bool onlyParked = false;
		// The number of credits paid to their estate upon death
		// (minimum 0; calculated by formula if negative)
		int64_t deathBenefit = -1;
		// The extra shares of profit paid to their estate on the day of their death
		// (minimum 0; calculated by formula if negative)
		int64_t deathShares = -1;
		// The number of credits paid daily while parked (minimum 0)
		int64_t parkedSalary = 0;
		// The crew member's profit shares while parked (minimum 0)
		int64_t parkedShares = 0;
		// The number of credits paid daily (minimum 0)
		int64_t salary = 0;
		// The crew member's shares in the fleet's profits (minimum 0)
		int64_t shares = 0;
		// The number of shares that the crew member gains for each level of combat rating
		// This attribute is only used by the Player crew member to calculate TotalShares.
    // For other crew members, we use Shares directly as a performance optimisation.
		int64_t sharesPerCombatLevel = 0;
		// The number of shares that the crew member gains with each new license
		// This attribute is only used by the Player crew member to calculate TotalShares.
    // For other crew members, we use Shares directly as a performance optimisation.
		int64_t sharesPerLicense = 0;
		// Every nth crew member on the ship will be this crew member
		int64_t shipPopulationPerMember = 0;
		// The id that the crew member is stored against in GameData::Crews()
		std::string id;
		// The display name for this kind of crew members (singular, Title Case)
		std::string name;
		// The crew member occurs at these crew member numbers if possible.
		// Use this to add crew members that are required when a ship reaches
		// specific crew sizes.
		// Example usage: "occurs at" 1 3 5 7 13
		std::vector<int64_t> occursAt;
		// If defined, the crew member will never appear on ships of these categories.
		// Takes precedence over "only ship categories".
		// Example usage: "avoids ship categories" "Fighter" "Drone"
		std::vector<std::string> avoidsShipCategories;
		// If defined, the crew member will only appear on ships of these categories.
		// Example usage: "only ship categories" "Light Freighter" "Heavy Freighter" "Utility"
		std::vector<std::string> onlyShipCategories;
		// Lookup table for whether or not the crew member can appear on ships of
		// each category. Initialized during the Load process.
		std::map<std::string, bool> shipCategories;
		};

	#endif // CREW_MEMBER_H_
