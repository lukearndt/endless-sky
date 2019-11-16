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

using namespace std;

class Crew
{
public:
	// Calculate one day's salaries for the Player's fleet
	static int64_t CalculateSalaries(const Ship *flagship, const vector<shared_ptr<Ship>> ships);

	// Load a definition for a crew economics setting.
	void Load(const DataNode &node);
	
	const bool &isOnEscorts() const;
	const bool &isOnFlagship() const;
	const bool &isPaidWhileParked() const;
	const int64_t &DailySalary() const;
	const int64_t &MinimumPerShip() const;
	const int64_t &PopulationPerOccurrence() const;
	const std::string &Name() const;
	const vector<string> &ShipCategories() const; 

private:
	bool isOnEscorts;
	bool isOnFlagship;
	bool isPaidWhileParked;
	int64_t dailySalary;
	int64_t minimumPerShip;
	int64_t populationPerOccurrence;
	std::string name;
	vector<string> shipCategories;

#endif
