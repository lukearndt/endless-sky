/* HiringPanel.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "HiringPanel.h"

#include "Command.h"
#include "Crew.h"
#include "text/Format.h"
#include "GameData.h"
#include "Information.h"
#include "Interface.h"
#include "PlayerInfo.h"
#include "Ship.h"

#include <algorithm>

using namespace std;



HiringPanel::HiringPanel(PlayerInfo &player)
	: player(player), maxHire(0), maxFire(0)
{
	SetTrapAllEvents(false);
}



void HiringPanel::Step()
{
	DoHelp("hiring");
}



void HiringPanel::Draw()
{
	const Interface *hiring = GameData::Interfaces().Get("hiring");
	info.ClearConditions();

	// Analyse the player's fleet and generate a report.
	const shared_ptr<Crew::FleetAnalysis> analysis = player.FleetCrewAnalysis();
	PlayerInfo::FleetBalance fleetBalance = player.MaintenanceAndReturns();

	info.SetString("flagship bunks", Format::Number(analysis->flagshipBunkAnalysis->total));
	info.SetString("flagship required", Format::Number(analysis->flagshipBunkAnalysis->requiredCrew));
	info.SetString("flagship extra", Format::Number(analysis->flagshipBunkAnalysis->extraCrew));
	info.SetString("flagship unused", Format::Number(analysis->flagshipBunkAnalysis->empty));

	info.SetString("fleet bunks", Format::Number(analysis->fleetBunkAnalysis->total));
	info.SetString("fleet required", Format::Number(analysis->fleetBunkAnalysis->requiredCrew));
	info.SetString("fleet unused", Format::Number(analysis->fleetBunkAnalysis->empty));
	info.SetString("passengers", Format::Number(analysis->fleetBunkAnalysis->passengers));

	info.SetString("salary required", to_string(analysis->salaryReport->at(Crew::ReportDimension::Required)));
	info.SetString("shares required", to_string(analysis->sharesReport->at(Crew::ReportDimension::Required)));
	info.SetString("salary extra", to_string(analysis->salaryReport->at(Crew::ReportDimension::Extra)));
	info.SetString("shares extra", to_string(analysis->sharesReport->at(Crew::ReportDimension::Extra)));

	info.SetString("your share of profits", to_string(analysis->profitPlayerPercentage) + "%");
	info.SetString("player profit percentage", to_string(analysis->profitPlayerPercentage) + "% of fleet profits");
	info.SetString("player daily income", to_string(
		player.GetTributeTotal() + player.Accounts().SalariesIncomeTotal()
		+ fleetBalance.assetsReturns - fleetBalance.maintenanceCosts
	));
	info.SetString("player shares", to_string(analysis->playerShares));

	int modifier = Modifier();
	if(modifier > 1)
		info.SetString("modifier", "x " + to_string(modifier));
	else
		info.SetString("modifier", "");

	maxFire = max(analysis->flagshipBunkAnalysis->extraCrew, (int64_t)0);
	maxHire = max(min(
		analysis->flagshipBunkAnalysis->empty,
		analysis->fleetBunkAnalysis->empty - analysis->fleetBunkAnalysis->passengers
	), (int64_t)0);

	if(maxHire)
		info.SetCondition("can hire");
	if(maxFire)
		info.SetCondition("can fire");

	hiring->Draw(info, this);
}



bool HiringPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if(command.Has(Command::HELP))
	{
		DoHelp("hiring", true);
		return true;
	}

	if(!player.Flagship())
		return false;

	if(key == 'h' || key == SDLK_EQUALS || key == SDLK_KP_PLUS || key == SDLK_PLUS
		|| key == SDLK_RETURN || key == SDLK_SPACE)
	{
		player.Flagship()->AddCrew(min(maxHire, Modifier()));
		player.UpdateCargoCapacities();
	}
	else if(key == 'f' || key == SDLK_MINUS || key == SDLK_KP_MINUS || key == SDLK_BACKSPACE || key == SDLK_DELETE)
	{
		player.Flagship()->AddCrew(-min(maxFire, Modifier()));
		player.UpdateCargoCapacities();
	}
	else
		return false;

	return true;
}
