/* Crew.h
Copyright (c) 2019 by Luke Arndt

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#ifndef CREW_H_
#define CREW_H_

#include "Ship.h"

class Crew
{
public:
	// Calculate one day's salaries for the Player's fleet
	static int64_t CalculateSalaries(
		const std::vector<std::shared_ptr<Ship>> ships,
		const bool includeExtras = true
	);

	// Calculate the total cost of the flagship's extra crew
	static int64_t CostOfExtraCrew(
		const std::vector<std::shared_ptr<Ship>> ships
	);

	// Figure out how many of a given crew member are on a ship
	static int64_t NumberOnShip(
		const Crew crew,
		const std::shared_ptr<Ship> ship,
		const bool isFlagship,
		const bool includeExtras = true
	);

	// Calculate one day's salaries for a ship
	static int64_t SalariesForShip(
		const std::shared_ptr<Ship> ship,
		const bool isFlagship,
		const bool includeExtras = true
	);

	// Load a definition for a crew economics setting.
	void Load(const DataNode &node);
	
	bool AvoidsEscorts() const;
	bool AvoidsFlagship() const;
	bool IsPaidSalaryWhileParked() const;
	int64_t DailySalary() const;
	int64_t MinimumPerShip() const;
	int64_t PopulationPerOccurrence() const;
	std::string Name() const;

private:
	bool avoidsEscorts;
	bool avoidsFlagship;
	bool isPaidSalaryWhileParked;
	int64_t dailySalary;
	int64_t minimumPerShip;
	int64_t populationPerOccurrence;
	std::string name;
};

#endif
