/********************************************************************* 
* Usage of this software requires acceptance of the SMASH-CMU License,
* which can be found at the following URL:
*
* https://code.google.com/p/smash-cmu/wiki/License
*********************************************************************/

/*********************************************************************
* area_coverage_module.cpp - Defines the manager for area coverage.
*********************************************************************/

#include <vector>
#include <map>
#include <math.h>
#include "area_coverage_module.h"
#include "utilities/CommonMadaraVariables.h"
#include "AreaCoverage.h"
#include "SnakeAreaCoverage.h"
#include "RandomAreaCoverage.h"
#include "InsideOutAreaCoverage.h"

using namespace SMASH::AreaCoverage;
using namespace SMASH::Utilities;

#define REACHED_ACCURACY	                0.0000050		// Margin (in degrees) to use when checking if we have reached a location.
#define ALTITUDE_DIFFERENCE					0.5				// The amount of vertical space (in meters) to leave between drones.

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Madara Variable Definitions
////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions.
#define MF_MAIN_LOGIC               "area_coverage_doAreaCoverage"			// Function that checks if there is area coverage to be done, and does it.
#define MF_INIT_SEARCH_CELL         "area_coverage_initSearchCell"          // Initializes the cell that we will be searching.
#define MF_CALC_AND_MOVE_TO_ALT     "area_coverage_calcAndMoveToAlt"        // Initializes the cell that we will be searching.
#define MF_NEXT_TARGET_REACHED      "area_coverage_checkNextTargetReached"  // Checks if the current target has been reached.
#define MF_FINAL_TARGET_REACHED     "area_coverage_checkFinalTargetReached" // Checks if the final target has been reached.
#define MF_TARGET_REACHED			"area_coverage_checkTargetReached"      // Checks if the current target has been reached.
#define MF_SET_NEW_TARGET           "area_coverage_setNewTarget"            // Sets the next target.
#define MF_UPDATE_AVAILABLE_DRONES	"area_coverage_updateAvailableDrones"   // Function that checks the amount and positions of drones ready for covering.
#define MF_SET_NEW_COVERAGE         "area_coverage_setNewCoverage"      // Switches the drones coverage algorithm

// Internal variables.
#define MV_ACCURACY	                "0.0000020"                                 // Delta (in degrees) to use when checking if we have reached a location.
#define MV_CELL_INITIALIZED	        ".area_coverage.cell.initialized"           // Flag to check if we have initialized our cell in the search area.
#define MV_NEXT_TARGET_LAT          ".area_coverage.target.location.latitude"   // The latitude of the next target location in our search pattern.
#define MV_NEXT_TARGET_LON          ".area_coverage.target.location.longitude"  // The longitude of the next target location in our search pattern.
#define MV_AVAILABLE_DRONES_AMOUNT	".area_coverage.devices.available.total"    // The amount of available drones.
#define MV_AVAILABLE_DRONES_MY_IDX	".area_coverage.devices.available.my_idx"   // The index of the device in the list of available ones.
#define MV_MY_CELL_TOP_LEFT_LAT     ".area_coverage.cell.top_left.location.latitude"        // The x of the top left corner of the cell I am searching.
#define MV_MY_CELL_TOP_LEFT_LON     ".area_coverage.cell.top_left.location.longitude"       // The y of the top left corner of the cell I am searching.
#define MV_MY_CELL_BOT_RIGHT_LAT    ".area_coverage.cell.bottom_right.location.latitude"    // The x of the bottom right corner of the cell I am searching.
#define MV_MY_CELL_BOT_RIGHT_LON    ".area_coverage.cell.bottom_right.location.longitude"   // The y of the bottom right corner of the cell I am searching.

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Private variables.
////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Define the ids for the expressions that will exist to accomplish different aspects of area coverage.
enum AreaCoverageMadaraExpressionId 
{
	// Expression to call function to update the positions of the drones available for coverage.
	ACE_FIND_AVAILABLE_DRONES_POSITIONS,
};

// Map of Madara expressions used in bridge building.
static std::map<AreaCoverageMadaraExpressionId, Madara::Knowledge_Engine::Compiled_Expression> m_expressions;

// Stores information about the area coverage state.
static AreaCoverage* m_coverageAlgorithm;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Private function declarations.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void defineFunctions(Madara::Knowledge_Engine::Knowledge_Base &knowledge);
static void compileExpressions(Madara::Knowledge_Engine::Knowledge_Base &knowledge);

static Madara::Knowledge_Record madaraInitSearchCell (Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables);
static Madara::Knowledge_Record madaraSetNewTarget (Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables);
static Madara::Knowledge_Record madaraTargetReached (Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables);
static Madara::Knowledge_Record madaraReachedFinalTarget(Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables);
static Madara::Knowledge_Record madaraSetNewCoverage(Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables);
static Madara::Knowledge_Record madaraCalculateAndMoveToAltitude (Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initializer, gets the refence to the knowledge base and compiles expressions.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SMASH::AreaCoverage::initialize(Madara::Knowledge_Engine::Knowledge_Base &knowledge)
{
	// Defines internal and external functions.
	defineFunctions(knowledge);

	// Registers all default expressions, to have them compiled for faster access.
	compileExpressions(knowledge);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cleanup, cleans up the dynamically allocated search algorithm
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SMASH::AreaCoverage::cleanup(Madara::Knowledge_Engine::Knowledge_Base &knowledge)
{
	// should only need to delete the coverage algorithm
	delete m_coverageAlgorithm;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Gets the main logic to be run. This returns a function call that can be included in another block of logic.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::string SMASH::AreaCoverage::get_core_function()
{
	return MF_MAIN_LOGIC "()";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Registers functions with Madara.
// ASSUMPTION: Drone IDs are continuous, starting from 0.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
void defineFunctions(Madara::Knowledge_Engine::Knowledge_Base &knowledge)
{
	// Function that can be included in main loop of another method to introduce area coverage.
	knowledge.define_function(MF_MAIN_LOGIC, 
		"(" MV_AREA_COVERAGE_REQUESTED("{.id}") " => "
			"("
				"(" 
					"("MV_CELL_INITIALIZED ")"
					" => (" 
						// Check if we are just initializing, or if we reached the next target, but not the end of the area, to set the next waypoint.
						"((" MV_NEXT_TARGET_LAT " == 0) && (" MV_NEXT_TARGET_LON " == 0)) || "
						"((" MF_NEXT_TARGET_REACHED "()" " && !" MF_FINAL_TARGET_REACHED "() ))" // && " MF_SET_NEW_COVERAGE "() ))"
							" => " MF_SET_NEW_TARGET  "()" 
						")"
				");"
				"("
					// If we haven't found our cell yet, do it and calculate and move to our altitude.
					"(!" MV_CELL_INITIALIZED ")"
						" => ( (" MF_INIT_SEARCH_CELL "() && " MF_CALC_AND_MOVE_TO_ALT "() ) => (" MV_CELL_INITIALIZED " = 1))"
				");"
			")"
		")"
		);

	// Function to update the amound and positions of drones available for covering a specific area.
	knowledge.define_function(MF_UPDATE_AVAILABLE_DRONES, 
		// Set available drones to 0 and disregard its return (choose right).
		MV_AVAILABLE_DRONES_AMOUNT " = 0 ;>"

		// Loop over all the drones to find the available ones.
		".i[0->" MV_TOTAL_DEVICES ")"
		"("
			// A drone is available if it is mobile and it is not busy, and it is assigned to the same area as I am.
			"(" MV_MOBILE("{.i}") " && (!" MV_BUSY("{.i}") ") && (" MV_ASSIGNED_SEARCH_AREA("{.i}") " == " MV_ASSIGNED_SEARCH_AREA("{.id}") "))"
			" => "
			"("
				// If so, increase the amount of available drones, and and store my idx in the list if I find it.
				"((.i == .id) => (" MV_AVAILABLE_DRONES_MY_IDX " = .i));"
					"++" MV_AVAILABLE_DRONES_AMOUNT ";"
				");"
			");"
		);

	// Returns 1 if we are closer than MV_ACCURACY to the current target of our search.
	knowledge.define_function(MF_NEXT_TARGET_REACHED, 
		"("
			"(" MF_TARGET_REACHED "(" MV_DEVICE_LAT("{.id}") "," MV_NEXT_TARGET_LAT "," MV_DEVICE_LON("{.id}") "," MV_NEXT_TARGET_LON ")" ")"
		//"("
		//    "((" MV_DEVICE_LAT("{.id}") " - " MV_NEXT_TARGET_LAT ") < " MV_ACCURACY ") && "
		//    "((" MV_NEXT_TARGET_LAT " - " MV_DEVICE_LAT("{.id}") ") < " MV_ACCURACY ") "
		//")"
		//" && "
		//"("
		//    "((" MV_DEVICE_LON("{.id}") " - " MV_NEXT_TARGET_LON ") < " MV_ACCURACY ") && "
		//    "((" MV_NEXT_TARGET_LON " - " MV_DEVICE_LON("{.id}") ") < " MV_ACCURACY ") "
		//")"
		");"
		);

	knowledge.define_function(MF_FINAL_TARGET_REACHED, madaraReachedFinalTarget);

	// Returns 1 if we are closer than MV_ACCURACY to certain target location. 
	knowledge.define_function(MF_TARGET_REACHED, madaraTargetReached);

	// Function that can be included in main loop of another method to introduce area coverage.
	knowledge.define_function(MF_INIT_SEARCH_CELL, madaraInitSearchCell);

	// Function that can be included in main loop of another method to introduce area coverage.
	knowledge.define_function(MF_CALC_AND_MOVE_TO_ALT, madaraCalculateAndMoveToAltitude);

	// Function that can be included in main loop of another method to introduce area coverage.
	knowledge.define_function(MF_SET_NEW_TARGET, madaraSetNewTarget);

	// Change over to random area coverage
	knowledge.define_function(MF_SET_NEW_COVERAGE, madaraSetNewCoverage);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compiles all expressions to be used by this class.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
void compileExpressions(Madara::Knowledge_Engine::Knowledge_Base &knowledge)
{
	// Expression to update the list of available drones, simply calls the predefined function.
	m_expressions[ACE_FIND_AVAILABLE_DRONES_POSITIONS] = knowledge.compile(
		MF_UPDATE_AVAILABLE_DRONES "();"
		);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
* Checks if we are within a certain accuracy of a target location.
* @return  Returns true (1) if we are, or false (0) if not.
**/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
Madara::Knowledge_Record madaraTargetReached (Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables)
{
	// All the params come from Madara.
	double currLat = args[0].to_double();
	double targetLat = args[1].to_double();
	double currLon = args[2].to_double();
	double targetLon = args[3].to_double();

	//printf("Curr and target lats are %.10f,%.10f, and diff is %.10f\n", currLat, targetLat, (currLat-targetLat));
	//printf("Curr and target lats are %.10f,%.10f, and diff is %.10f\n", currLon, targetLon, (currLon-targetLon));

	if(fabs(currLat - targetLat) < REACHED_ACCURACY &&
		fabs(currLon - targetLon) < REACHED_ACCURACY)
	{
		printf("HAS reached target\n");
		return Madara::Knowledge_Record(1.0);
	}
	else
	{
		printf("HAS NOT reached target\n");
		return Madara::Knowledge_Record(0.0);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
* Method that invocates the functionality of defining our cell to search, which will be called from Madara when required.
* Will be called from an external Madara function.
* @return  Returns true (1) if it can calculate the cell, false (0) if it couldn't for some reason.
**/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
Madara::Knowledge_Record madaraInitSearchCell (Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables)
{
	// Find all the available drones, called here to ensure atomicity and we have the most up to date data.
	variables.evaluate(m_expressions[ACE_FIND_AVAILABLE_DRONES_POSITIONS],
		Madara::Knowledge_Engine::Knowledge_Update_Settings(true, false));

	// Obtain drone information
	int availableDrones = (int) variables.get(MV_AVAILABLE_DRONES_AMOUNT).to_integer();
	int myIndexInList = (int) variables.get(MV_AVAILABLE_DRONES_MY_IDX).to_integer();

	// Obtain the region details where we will be searching.
	std::string myAssignedSearchArea = variables.get(MV_ASSIGNED_SEARCH_AREA("{.id}")).to_string();
	std::string myAssignedSearchRegion = variables.get(MV_SEARCH_AREA_REGION(myAssignedSearchArea)).to_string();
	double topLeftX = variables.get(MV_REGION_TOPLEFT_LAT(myAssignedSearchRegion)).to_double();
	double topLeftY = variables.get(MV_REGION_TOPLEFT_LON(myAssignedSearchRegion) ).to_double();
	double bottomRightX = variables.get(MV_REGION_BOTRIGHT_LAT(myAssignedSearchRegion)).to_double();
	double bottomRightY = variables.get(MV_REGION_BOTRIGHT_LON(myAssignedSearchRegion) ).to_double();
	Region searchArea = Region(Position(topLeftX, topLeftY), Position(bottomRightX, bottomRightY));

	// Calculate the actual cell I will be covering.
	// NOTE: we are storing the cell location as a string instead of doubles to ensure we have enough precision, since Madara,
	// as of version 0.9.44, has only 6 digits of precision for doubles (usually 4 decimals for latitudes and longitudes).
	//m_coverageAlgorithm = new RandomAreaCoverage(false);
	m_coverageAlgorithm = new SnakeAreaCoverage();
	Region* myCell = m_coverageAlgorithm->initialize(myIndexInList, searchArea, availableDrones);

	if(myCell != NULL)
	{
		// Store this cell in Madara.
		variables.set(MV_MY_CELL_TOP_LEFT_LAT, NUM_TO_STR(myCell->topLeftCorner.x));
		variables.set(MV_MY_CELL_TOP_LEFT_LON, NUM_TO_STR(myCell->topLeftCorner.y));
		variables.set(MV_MY_CELL_BOT_RIGHT_LAT, NUM_TO_STR(myCell->bottomRightCorner.x));
		variables.set(MV_MY_CELL_BOT_RIGHT_LON, NUM_TO_STR(myCell->bottomRightCorner.y));

		return Madara::Knowledge_Record(1.0);
	}
	else
	{
		// If we couldn't generate our cell for some reason, the function was not successful.
		return Madara::Knowledge_Record(0.0);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
* Method that sets up the altitude of a drone, based on is index in the search area (which will be 0 if it has not
* been set).
* @return  Returns true (1) always.
**/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
Madara::Knowledge_Record madaraCalculateAndMoveToAltitude (Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables)
{
	// Calculate and store my assigned or default altitude based on my index on the list.
	// (If the search area has not been initialized, all drones will end up at the same, default height).
	double minAltitude = (double) variables.get(MV_MIN_ALTITUDE).to_double();
	int myIndexInList = (int) variables.get(MV_AVAILABLE_DRONES_MY_IDX).to_integer();
	double myDefaultAltitude = minAltitude + ALTITUDE_DIFFERENCE * (double) myIndexInList;
	variables.set(MV_ASSIGNED_ALTITUDE("{.id}"), myDefaultAltitude);

	// Send the command to go to this altitude.
	// NOTE: we are storing the cell location as a string instead of doubles to ensure we have enough precision, since Madara,
	// as of version 0.9.44, has only 6 digits of precision for doubles (usually 4 decimals for latitudes and longitudes).
	variables.set(MV_MOVEMENT_TARGET_ALT, myDefaultAltitude);
	variables.set(MV_MOVEMENT_REQUESTED, std::string(MO_MOVE_TO_ALTITUDE_CMD));
	printf("Moving to altitude %f!\n", myDefaultAltitude);

	return Madara::Knowledge_Record(1.0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
* Method that sets the next waypoint target in our search pattern, and tells the movement module to move there.
* Will be called from an external Madara function.
* @return  Returns true (1) always.
**/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
Madara::Knowledge_Record madaraSetNewTarget (Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables)
{
	// Get the next target.
	Position nextTarget = m_coverageAlgorithm->getNextTargetLocation();

	// Update the drone status for the next target.
	// NOTE: we are storing the cell location as a string instead of doubles to ensure we have enough precision, since Madara,
	// as of version 0.9.44, has only 6 digits of precision for doubles (usually 4 decimals for latitudes and longitudes).
	variables.set(MV_NEXT_TARGET_LAT, NUM_TO_STR(nextTarget.x),
		Madara::Knowledge_Engine::Knowledge_Update_Settings(false, false));
	variables.set(MV_NEXT_TARGET_LON, NUM_TO_STR(nextTarget.y),
		Madara::Knowledge_Engine::Knowledge_Update_Settings(false, false));

	// Set the movement command for the movement module.
	// NOTE: we are storing the cell location as a string instead of doubles to ensure we have enough precision, since Madara,
	// as of version 0.9.44, has only 6 digits of precision for doubles (usually 4 decimals for latitudes and longitudes).
	variables.set(MV_MOVEMENT_TARGET_LAT, NUM_TO_STR(nextTarget.x),
		Madara::Knowledge_Engine::Knowledge_Update_Settings(false, false));
	variables.set(MV_MOVEMENT_TARGET_LON, NUM_TO_STR(nextTarget.y),
		Madara::Knowledge_Engine::Knowledge_Update_Settings(false, false));
	variables.set(MV_MOVEMENT_REQUESTED, std::string(MO_MOVE_TO_GPS_CMD));

	return Madara::Knowledge_Record(1.0);
}

/**
* Switches the coverage algorithm used
* TODO: allow for other algorithms to be used
* @return  Returns true (1) always.
**/
Madara::Knowledge_Record madaraSetNewCoverage(Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables)
{
	// Find all the available drones, called here to ensure atomicity and we have the most up to date data.
	variables.evaluate(m_expressions[ACE_FIND_AVAILABLE_DRONES_POSITIONS],
		Madara::Knowledge_Engine::Knowledge_Update_Settings(true, false));

	// Obtain drone information
	int availableDrones = (int) variables.get(MV_AVAILABLE_DRONES_AMOUNT).to_integer();
	int myIndexInList = (int) variables.get(MV_AVAILABLE_DRONES_MY_IDX).to_integer();

	// Obtain the region details where we will be searching.
	std::string myAssignedSearchArea = variables.get(MV_ASSIGNED_SEARCH_AREA("{.id}")).to_string();
	std::string myAssignedSearchRegion = variables.get(MV_SEARCH_AREA_REGION(myAssignedSearchArea)).to_string();
	double topLeftX = variables.get(MV_REGION_TOPLEFT_LAT(myAssignedSearchRegion)).to_double();
	double topLeftY = variables.get(MV_REGION_TOPLEFT_LON(myAssignedSearchRegion) ).to_double();
	double bottomRightX = variables.get(MV_REGION_BOTRIGHT_LAT(myAssignedSearchRegion)).to_double();
	double bottomRightY = variables.get(MV_REGION_BOTRIGHT_LON(myAssignedSearchRegion) ).to_double();
	Region searchArea = Region(Position(topLeftX, topLeftY), Position(bottomRightX, bottomRightY));

	// set cell information
	// NOTE: we are storing the cell location as a string instead of doubles to ensure we have enough precision, since Madara,
	// as of version 0.9.44, has only 6 digits of precision for doubles (usually 4 decimals for latitudes and longitudes).
	delete m_coverageAlgorithm;
	m_coverageAlgorithm = new RandomAreaCoverage();
	Region* myCell = m_coverageAlgorithm->initialize(myIndexInList, searchArea, availableDrones);
	variables.set(MV_MY_CELL_TOP_LEFT_LAT, NUM_TO_STR(topLeftX));
	variables.set(MV_MY_CELL_TOP_LEFT_LON, NUM_TO_STR(topLeftY));
	variables.set(MV_MY_CELL_BOT_RIGHT_LAT, NUM_TO_STR(topLeftX));
	variables.set(MV_MY_CELL_BOT_RIGHT_LON, NUM_TO_STR(topLeftY));

	return Madara::Knowledge_Record(1.0);
}

/**
* Determines if this algorithm has ended.
* @return  Returns true (1) if ended, else returns false (0)
**/
Madara::Knowledge_Record madaraReachedFinalTarget(Madara::Knowledge_Engine::Function_Arguments &args,
	Madara::Knowledge_Engine::Variables &variables)
{
	// change to new coverage algorithm
	if(m_coverageAlgorithm->isTargetingFinalWaypoint())
		return Madara::Knowledge_Record(1.0);
	return Madara::Knowledge_Record(0.0);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test method used to setup drones in certain locations and issue a search request.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SMASH::AreaCoverage::setupSearchTest(Madara::Knowledge_Engine::Knowledge_Base &knowledge)
{
	// Setup the algorithm inputs.
	std::string id = "0";
	std::map<int, Position> availableDrones;
	availableDrones[0] = Position(3.48578,6.9078);
	availableDrones[2] = Position(2.99198,3.6877);
	availableDrones[5] = Position(8.99198,3.6877);
	availableDrones[8] = Position(3.98820,3.6670);

	// Generate information about my position and the position of others, also indicating we are mobile.
	knowledge.set(MV_DEVICE_LAT("0"), availableDrones[0].x);
	knowledge.set(MV_DEVICE_LON("0"), availableDrones[0].y);
	knowledge.set(MV_MOBILE("0"), 1.0);

	knowledge.set(MV_DEVICE_LAT("2"), availableDrones[5].x);	
	knowledge.set(MV_DEVICE_LON("2"), availableDrones[5].y);
	knowledge.set(MV_MOBILE("2"), 1.0);

	knowledge.set(MV_DEVICE_LAT("5"), availableDrones[5].x);	
	knowledge.set(MV_DEVICE_LON("5"), availableDrones[5].y);
	knowledge.set(MV_MOBILE("5"), 1.0);

	knowledge.set(MV_DEVICE_LAT("8"), availableDrones[8].x);
	knowledge.set(MV_DEVICE_LON("8"), availableDrones[8].y);
	knowledge.set(MV_MOBILE("8"), 1.0);

	// Generate information that should be set by sink when sending command for bridge.
	knowledge.set(MV_TOTAL_DEVICES, 9.0);

	// Simulate the sink actually sending the command to search.
	knowledge.set(MV_ASSIGNED_SEARCH_AREA(id), (Madara::Knowledge_Record::Integer) 0,
		Madara::Knowledge_Engine::Eval_Settings(true));
	knowledge.set(MV_ASSIGNED_SEARCH_AREA("2"), (Madara::Knowledge_Record::Integer) 0,
		Madara::Knowledge_Engine::Eval_Settings(true));
	knowledge.set(MV_ASSIGNED_SEARCH_AREA("5"), (Madara::Knowledge_Record::Integer) 0,
		Madara::Knowledge_Engine::Eval_Settings(true));
	knowledge.set(MV_ASSIGNED_SEARCH_AREA("8"), (Madara::Knowledge_Record::Integer) 0,
		Madara::Knowledge_Engine::Eval_Settings(true));

	knowledge.set(MV_SEARCH_AREA_REGION("0"), (Madara::Knowledge_Record::Integer) 0,
		Madara::Knowledge_Engine::Eval_Settings(true));

	// Set the bounding box of the regions. For now, the rectangle will actually just be a point.
	// NOTE: we use substring below to store the information not in the local but a global variable, which is only needed in a simulation.
	std::string sourceRegionIdString = "0";
	knowledge.set(MV_REGION_TYPE("0"), (Madara::Knowledge_Record::Integer) 0,
		Madara::Knowledge_Engine::Eval_Settings(true));
	knowledge.set((MV_REGION_TOPLEFT_LAT(sourceRegionIdString)), 0.0,
		Madara::Knowledge_Engine::Eval_Settings(true));
	knowledge.set((MV_REGION_TOPLEFT_LON(sourceRegionIdString)), 10.0,
		Madara::Knowledge_Engine::Eval_Settings(true));
	knowledge.set((MV_REGION_BOTRIGHT_LAT(sourceRegionIdString)), 10.0,
		Madara::Knowledge_Engine::Eval_Settings(true));
	knowledge.set((MV_REGION_BOTRIGHT_LON(sourceRegionIdString)), 0.0,
		Madara::Knowledge_Engine::Eval_Settings(true));

	knowledge.set(MV_AREA_COVERAGE_REQUESTED(id), 1.0);
}
