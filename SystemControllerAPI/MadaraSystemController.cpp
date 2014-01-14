/*********************************************************************
 * Usage of this software requires acceptance of the SMASH-CMU License,
 * which can be found at the following URL:
 *
 * https://code.google.com/p/smash-cmu/wiki/License
 *********************************************************************/

#include "MadaraSystemController.h"
#include "madara/knowledge_engine/Knowledge_Base.h"

#include "CommonMadaraVariables.h"
#include "kb_setup.h"
#include "transport_vrep.h"
#include "transport_ardrone2.h"
#include "string_utils.h"

#include <map>
#include <string>

using namespace SMASH::Utilities;

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor, sets up a Madara knowledge base and basic values.
////////////////////////////////////////////////////////////////////////////////////////////////////////
MadaraController::MadaraController(int id, std::string platform)
{
    // Start the counter at 0.
    m_regionId = 0;

    // Set our state.
    m_id = id;

    // Setup logging level.
    MADARA_debug_level = 1;
    bool enableLogging = true;

    // Create the knowledge base.
    m_knowledge = new Madara::Knowledge_Engine::Knowledge_Base();

    // Get the transport(s).
    std::vector<Madara::Transport::Base*> transports;
    if(platform == "vrep")
    {
      Madara::Transport::Multicast_Transport* vrepTransport = 
        get_vrep_multicast_transport(id, m_knowledge);
      transports.push_back(vrepTransport);
    }
    else if(platform == "ardrone2")
    {
      Madara::Transport::Broadcast_Transport* ardroneTransport = 
        get_ardrone2_broadcast_transport(id, m_knowledge);
      transports.push_back(ardroneTransport);
    }

    // Setup the knowledge base.
    setup_knowledge_base(m_knowledge, transports, id, enableLogging);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor, simply cleans up.
////////////////////////////////////////////////////////////////////////////////////////////////////////
MadaraController::~MadaraController()
{
    m_knowledge->print_knowledge();
    if(m_knowledge != NULL)
        delete m_knowledge;
    m_knowledge = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Method that updates general parameters of thw swarm.
// numberOfDrones: The number of drones in the system.
// commRange: The communications range for the network.
// minAltitude: The min altitude for the flying devices.
// lineWidth: The width of a search line.
// heightDiff: The vertical distance to leave between drones.
////////////////////////////////////////////////////////////////////////////////////////////////////////
void MadaraController::updateGeneralParameters(const int& numberOfDrones, const double& commRange, const double& minAltitude, 
                                               const double& heightDiff, const int& coverageTrackingEnabled, const int& coverageTrackingFileEnabled,
                                               const double& thermalSensorAngle, const double& defaultPriority, const std::string& prioritizedAreas)
{
    // Set up the general parameters from the class into Madara variables.
    m_knowledge->set(MV_COMM_RANGE, commRange, Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_MIN_ALTITUDE, minAltitude, Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_AREA_COVERAGE_HEIGHT_DIFF, heightDiff, Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_TOTAL_DEVICES_GLOBAL, (Madara::Knowledge_Record::Integer) numberOfDrones, Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_COVERAGE_TRACKING_ENABLED, (Madara::Knowledge_Record::Integer) coverageTrackingEnabled, Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_COVERAGE_TRACKING_FILE_ENABLED, (Madara::Knowledge_Record::Integer) coverageTrackingFileEnabled, Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_THERMAL_SENSOR_ANGLE, thermalSensorAngle, Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_DEFAULT_PRIORITY, defaultPriority, Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_PRIORITIZED_AREAS, prioritizedAreas, Madara::Knowledge_Engine::Eval_Settings(true));


    // This call will flush all past changes.
    m_knowledge->send_modifieds();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sends a takeoff command.
////////////////////////////////////////////////////////////////////////////////////////////////////////
void MadaraController::sendTakeoffCommand()
{
    m_knowledge->set(MV_SWARM_MOVE_REQUESTED, MO_TAKEOFF_CMD);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sends a land command.
////////////////////////////////////////////////////////////////////////////////////////////////////////
void MadaraController::sendLandCommand()
{
    m_knowledge->set(MV_SWARM_MOVE_REQUESTED, MO_LAND_CMD);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sets up all the variables required for a bridge request.
////////////////////////////////////////////////////////////////////////////////////////////////////////
void MadaraController::setupBridgeRequest(int bridgeId, Region startRegion, Region endRegion)
{
    int rectangleType = 0;
    std::string bridgeIdString = NUM_TO_STR(bridgeId);

    // We set the total bridges to the bridge id + 1, since it starts at 0.
    int totalBridges = bridgeId + 1;
    m_knowledge->set(MV_TOTAL_BRIDGES, (Madara::Knowledge_Record::Integer) totalBridges,
      Madara::Knowledge_Engine::Eval_Settings(true));

    // Store the id of the source region for this bridge.
    int sourceRegionId = m_regionId++;
    std::string sourceRegionIdString = NUM_TO_STR(sourceRegionId);
    m_knowledge->set(MV_BRIDGE_SOURCE_REGION_ID(bridgeIdString),
      (Madara::Knowledge_Record::Integer)sourceRegionId,
      Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_REGION_TYPE(sourceRegionIdString),
      (Madara::Knowledge_Record::Integer) rectangleType,
      Madara::Knowledge_Engine::Eval_Settings(true));

    // Set the bounding box of the regions. For now, the rectangle will actually just be a point.
    // NOTE: we use substring below to store the information not in the local but a global variable, which is only needed in a simulation.
    std::string sourceNorthWestLocation = startRegion.northWest.toString();
    std::string sourceSouthEastLocation = startRegion.southEast.toString();
    m_knowledge->set(MV_REGION_TOPLEFT_LOC(sourceRegionIdString), sourceNorthWestLocation,
      Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_REGION_BOTRIGHT_LOC(sourceRegionIdString), sourceSouthEastLocation,
      Madara::Knowledge_Engine::Eval_Settings(true));

    // Store the id of the sink region for this bridge.
    int sinkRegionId = m_regionId++;
    std::string sinkRegionIdString = NUM_TO_STR(sinkRegionId);
    m_knowledge->set(MV_BRIDGE_SINK_REGION_ID(bridgeIdString), (Madara::Knowledge_Record::Integer) sinkRegionId,
      Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_REGION_TYPE(sinkRegionIdString), (Madara::Knowledge_Record::Integer) rectangleType,
      Madara::Knowledge_Engine::Eval_Settings(true));

    // Set the bounding box of the regions. For now, the rectangle will actually just be a point.
    std::string sinkNorthWestLocation = endRegion.northWest.toString();
    std::string sinkSouthEastLocation = endRegion.southEast.toString();
    m_knowledge->set(MV_REGION_TOPLEFT_LOC(sinkRegionIdString), sinkNorthWestLocation,
      Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_REGION_BOTRIGHT_LOC(sinkRegionIdString), sinkSouthEastLocation,
      Madara::Knowledge_Engine::Eval_Settings(true));

    // Indicates that we are requesting a bridge.
    // This call has no delay to flush all past changes.
    m_knowledge->set(MV_BRIDGE_REQUESTED, (Madara::Knowledge_Record::Integer) 1.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Requests a drone to be part of area coverage.
////////////////////////////////////////////////////////////////////////////////////////////////////////
void MadaraController::requestAreaCoverage(std::vector<int> droneIds, int searchAreaId, string searchAlgorithm, int wait, 
                                           double lineWidth, string humanDetectionAlgorithm)
{
    // Set the given search area as the area for this drone to search; and tell it to start searching.

  // Set the values for each drone.
  for(unsigned int i=0; i<droneIds.size(); i++)
  {
    std::string droneIdString = NUM_TO_STR(droneIds[i]);
    m_knowledge->set(MV_ASSIGNED_SEARCH_AREA(droneIdString), (Madara::Knowledge_Record::Integer) searchAreaId,
      Madara::Knowledge_Engine::Eval_Settings(true)); 
    m_knowledge->set(MV_AREA_COVERAGE_REQUESTED(droneIdString), searchAlgorithm,
      Madara::Knowledge_Engine::Eval_Settings(true));
        m_knowledge->set(MV_SEARCH_WAIT, (Madara::Knowledge_Record::Integer) wait,
      Madara::Knowledge_Engine::Eval_Settings(true));

        // This is currently a global value, but it could be different for each search area.
        m_knowledge->set(MV_AREA_COVERAGE_LINE_WIDTH, lineWidth, Madara::Knowledge_Engine::Eval_Settings(true));

        // Setup the human detection algorithm we want.
        m_knowledge->set(MV_HUMAN_DETECTION_REQUESTED(droneIdString), humanDetectionAlgorithm,
      Madara::Knowledge_Engine::Eval_Settings(true));
  }

    // Wait till this point to disseminate the variables set in the previous loop.
    m_knowledge->send_modifieds();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Requests a drone to be part of area coverage.
////////////////////////////////////////////////////////////////////////////////////////////////////////
void MadaraController::setNewSearchArea(int searchAreaId, SMASH::Utilities::Region& areaBoundaries)
{
    // Add a new search area.
    int searchAreaRegionId = m_regionId++;
    std::string searchAreaIdString = NUM_TO_STR(searchAreaId);
    m_knowledge->set(MV_SEARCH_AREA_REGION(searchAreaIdString), (Madara::Knowledge_Record::Integer) searchAreaRegionId,
      Madara::Knowledge_Engine::Eval_Settings(true));
    int totalSearchAreas = searchAreaId + 1;
    m_knowledge->set(MV_TOTAL_SEARCH_AREAS, (Madara::Knowledge_Record::Integer) totalSearchAreas,
      Madara::Knowledge_Engine::Eval_Settings(true));

    // Set the type and bounding box of the region associated with this search area.
    int rectangleType = 0;
    std::string sourceRegionIdString = NUM_TO_STR(searchAreaRegionId);
    std::string topLeftLocation = areaBoundaries.northWest.toString();
    std::string botRightLocation = areaBoundaries.southEast.toString();
    m_knowledge->set(MV_REGION_TYPE(sourceRegionIdString), (Madara::Knowledge_Record::Integer) rectangleType,
      Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_REGION_TOPLEFT_LOC(sourceRegionIdString), topLeftLocation,
      Madara::Knowledge_Engine::Eval_Settings(true));
    m_knowledge->set(MV_REGION_BOTRIGHT_LOC(sourceRegionIdString), botRightLocation,
      Madara::Knowledge_Engine::Eval_Settings(true));

    // Apply all changes.
    m_knowledge->send_modifieds();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convenience method that updates general parameters of thw swarm.
////////////////////////////////////////////////////////////////////////////////////////////////////////
std::vector<SMASH::Utilities::Position> MadaraController::getCurrentLocations()
{
    std::vector<SMASH::Utilities::Position> locations = std::vector<Position>();

    // Loop over all drones to get the current location for all of them.
    int numDrones = m_knowledge->get(MV_TOTAL_DEVICES).to_integer();
    for(int i=0; i < numDrones; i++)
    {
        Position currDroneLocation;
        std::string currDroneIdString = NUM_TO_STR(i);
        currDroneLocation.latitude = m_knowledge->get(MV_DEVICE_LAT(currDroneIdString)).to_double();
        currDroneLocation.longitude = m_knowledge->get(MV_DEVICE_LON(currDroneIdString)).to_double();
        locations.push_back(currDroneLocation);
    }

    return locations;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convenience method that updates general parameters of thw swarm.
////////////////////////////////////////////////////////////////////////////////////////////////////////
std::vector<SMASH::Utilities::Position> MadaraController::getCurrentThermals()
{
    std::vector<SMASH::Utilities::Position> locations = std::vector<Position>();

    // Loop over all drones to get the current location for all of them.
    std::map<std::string, Madara::Knowledge_Record> thermalLocations;
    m_knowledge->to_map("location_*", thermalLocations);
    
    // Transfer the locations to the vector.
    std::map<std::string, Madara::Knowledge_Record>::iterator iter;
    for (iter = thermalLocations.begin(); iter != thermalLocations.end(); ++iter)
    {
        // Split the madara variable name to get the coordinates.
        std::string locationString = iter->first;
        std::vector<std::string> variableParts = stringSplit(locationString, '_');

        // The coordinates will be in the position 1 and 2 of the split string (lat and long).
        if(variableParts.size() >= 3)
        {
            Position currThermal;
            currThermal.latitude = atof(variableParts[1].c_str());
            currThermal.latitude = atof(variableParts[2].c_str());
            locations.push_back(currThermal);
        }
    }
     
    return locations;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////////////////////////////////////////
void MadaraController::printKnowledge()
{
    m_knowledge->print_knowledge();
}
