/***********************************************/
/**
* @file gnssReceiverGeneratorStationNetwork.cpp
*
* @brief GNSS ground station network.
*
* @author Torsten Mayer-Guerr
* @author Sebastian Strasser
* @date 2021-02-25
*
*/
/***********************************************/

#include "base/import.h"
#include "base/string.h"
#include "base/planets.h"
#include "config/config.h"
#include "inputOutput/logging.h"
#include "inputOutput/system.h"
#include "files/fileGnssSignalBias.h"
#include "files/fileGnssStationInfo.h"
#include "files/fileInstrument.h"
#include "files/fileMatrix.h"
#include "files/fileStringTable.h"
#include "classes/gravityfield/gravityfield.h"
#include "classes/tides/tides.h"
#include "gnss/gnss.h"
#include "gnss/gnssReceiver.h"
#include "gnss/gnssTransmitter.h"
#include "gnss/gnssReceiverGenerator/gnssReceiverGenerator.h"
#include "gnss/gnssReceiverGenerator/gnssReceiverGeneratorStationNetwork.h"

/***********************************************/

GnssReceiverGeneratorStationNetwork::GnssReceiverGeneratorStationNetwork(Config &config)
{
  try
  {
    std::string choice;
    maxStationCount = MAX_UINT;

    readConfig(config, "inputfileStationList",               fileNameStationList,     Config::MUSTSET,  "", "ascii file with station names");
    readConfig(config, "maxStationCount",                    maxStationCount,         Config::OPTIONAL, "", "maximum number of stations to be used");
    readConfig(config, "inputfileStationInfo",               fileNameStationInfo,     Config::MUSTSET,  "{groopsDataDir}/gnss/receiverStation/stationInfo/igs/stationInfo.{station}.xml", "variable {station} available. station metadata (antennas, receivers, ...)");
    readConfig(config, "inputfileAntennaDefinition",         fileNameAntennaDef,      Config::MUSTSET,  "{groopsDataDir}/gnss/receiverStation/antennaDefinition/igs/igs14/antennaDefinition_igs14.dat", "antenna center offsets and variations");
    if(readConfigChoice(config, "noAntennaPatternFound", choice, Config::MUSTSET, "ignoreObservation", "what should happen if no antenna pattern is found for an observation"))
    {
      if(readConfigChoiceElement(config, "ignoreObservation",   choice, "ignore observation if no matching pattern is found"))
        noPatternFoundAction = GnssAntennaDefinition::NoPatternFoundAction::IGNORE_OBSERVATION;
      if(readConfigChoiceElement(config, "useNearestFrequency", choice, "use pattern of nearest frequency if no matching pattern is found"))
        noPatternFoundAction = GnssAntennaDefinition::NoPatternFoundAction::USE_NEAREST_FREQUENCY;
      if(readConfigChoiceElement(config, "throwException",      choice, "throw exception if no matching pattern is found"))
        noPatternFoundAction = GnssAntennaDefinition::NoPatternFoundAction::THROW_EXCEPTION;
      endChoice(config);
    }
    readConfig(config, "inputfileReceiverDefinition",        fileNameReceiverDef,     Config::OPTIONAL, "", "observed signal types");
    readConfig(config, "inputfileAccuracyDefinition",        fileNameAccuracyDef,     Config::MUSTSET,  "{groopsDataDir}/gnss/receiverStation/accuracyDefinition/accuracyDefinition.xml", "elevation and azimuth dependent accuracy");
    readConfig(config, "inputfileStationPosition",           fileNameStationPosition, Config::OPTIONAL, "{groopsDataDir}/gnss/receiverStation/position/igs/igb14/stationPosition.{station}.dat", "variable {station} available.");
    readConfig(config, "inputfileObservations",              fileNameObs,             Config::OPTIONAL, "gnssReceiver_{loopTime:%D}.{station}.dat", "variable {station} available");
    readConfig(config, "loadingDisplacement",                gravityfield,            Config::DEFAULT,  "",     "loading deformation");
    readConfig(config, "tidalDisplacement",                  tides,                   Config::DEFAULT,  "",     "tidal deformation");
    readConfig(config, "ephemerides",                        ephemerides,             Config::OPTIONAL, "jpl",  "for tidal deformation");
    readConfig(config, "inputfileDeformationLoadLoveNumber", deformationName,         Config::MUSTSET,  "{groopsDataDir}/loading/deformationLoveNumbers_CM_Gegout97.txt", "");
    readConfig(config, "inputfilePotentialLoadLoveNumber",   potentialName,           Config::OPTIONAL, "{groopsDataDir}/loading/loadLoveNumbers_Gegout97.txt", "if full potential is given and not only loading potential");
    readConfig(config, "useType",                            useType,                 Config::OPTIONAL, "",     "only use observations that match any of these patterns");
    readConfig(config, "ignoreType",                         ignoreType,              Config::OPTIONAL, "",     "ignore observations that match any of these patterns");
    readConfig(config, "elevationCutOff",                    elevationCutOff,         Config::DEFAULT,  "5",    "[degree] ignore observations below cutoff");
    readConfig(config, "elevationTrackMinimum",              elevationTrackMinimum,   Config::DEFAULT,  "15",   "[degree] ignore tracks that never exceed minimum elevation");
    readConfig(config, "minObsCountPerTrack",                minObsCountPerTrack,     Config::DEFAULT,  "60",   "tracks with less number of epochs with observations are dropped");
    readConfig(config, "minEstimableEpochsRatio",            minEstimableEpochsRatio, Config::DEFAULT,  "0.75", "[0,1] drop stations with lower ratio of estimable epochs to total epochs");
    if(readConfigSequence(config, "preprocessing", Config::MUSTSET, "", "settings for preprocessing of observations/stations"))
    {
      readConfig(config, "huber",                 huber,               Config::DEFAULT,  "2.5",  "residuals > huber*sigma0 are downweighted");
      readConfig(config, "huberPower",            huberPower,          Config::DEFAULT,  "1.5",  "residuals > huber: sigma=(e/huber)^huberPower*sigma0");
      readConfig(config, "codeMaxPositionDiff",   codeMaxPosDiff,      Config::DEFAULT,  "100",  "[m] max. allowed position error by PPP code only clock error estimation");
      readConfig(config, "denoisingLambda",       denoisingLambda,     Config::DEFAULT,  "5",    "regularization parameter for total variation denoising used in cylce slip detection");
      readConfig(config, "tecWindowSize",         tecWindowSize,       Config::DEFAULT,  "15",   "(0 = disabled) window size for TEC smoothness evaluation used in cycle slip detection");
      readConfig(config, "tecSigmaFactor",        tecSigmaFactor,      Config::DEFAULT,  "3.5",  "factor applied to moving standard deviation used as threshold in TEC smoothness evaluation during cycle slip detection");
      readConfig(config, "outputfileTrackBefore", fileNameTrackBefore, Config::OPTIONAL, "",     "variables {station}, {prn}, {timeStart}, {timeEnd}, {types}, TEC and MW-like combinations in cycles for each track before cycle slip detection");
      readConfig(config, "outputfileTrackAfter",  fileNameTrackAfter,  Config::OPTIONAL, "",     "variables {station}, {prn}, {timeStart}, {timeEnd}, {types}, TEC and MW-like combinations in cycles for each track after cycle slip detection");
      endSequence(config);
    } // readConfigSequence(preprocessing)
    if(isCreateSchema(config)) return;
  }
  catch(std::exception &e)
  {
    GROOPS_RETHROW(e)
  }
}

/***********************************************/

void GnssReceiverGeneratorStationNetwork::init(const std::vector<Time> &times, const Time &timeMargin, const std::vector<GnssTransmitterPtr> &transmitters,
                                               EarthRotationPtr earthRotation, Parallel::CommunicatorPtr comm, std::vector<GnssReceiverPtr> &receiversAll)
{
  try
  {
    logStatus<<"init station network"<<Log::endl;

    std::vector<GnssAntennaDefinitionPtr> antennaDefList;
    if(!fileNameAntennaDef.empty())
      readFileGnssAntennaDefinition(fileNameAntennaDef, antennaDefList);

    std::vector<GnssReceiverDefinitionPtr> receiverDefList;
    if(!fileNameReceiverDef.empty())
      readFileGnssReceiverDefinition(fileNameReceiverDef, receiverDefList);

    std::vector<GnssAntennaDefinitionPtr> accuracyDefList;
    if(!fileNameAccuracyDef.empty())
      readFileGnssAntennaDefinition(fileNameAccuracyDef, accuracyDefList);

    std::vector<std::vector<std::string>> stationName;
    readFileStringTable(fileNameStationList, stationName);
    VariableList fileNameVariableList;
    addVariable("station", fileNameVariableList);
    std::vector<std::vector<GnssReceiverPtr>> receiversWithAlternatives(stationName.size());
    for(UInt i=0; i<stationName.size(); i++)
      for(UInt k=0; k<stationName.at(i).size(); k++) // alternatives
      {
        try
        {
          fileNameVariableList["station"]->setValue(stationName.at(i).at(k));
          if(!fileNameObs.empty() && !System::exists(fileNameObs(fileNameVariableList)))
            continue;

          GnssStationInfo info;
          readFileGnssStationInfo(fileNameStationInfo(fileNameVariableList), info);
          info.fillAntennaPattern(antennaDefList);
          info.fillReceiverDefinition(receiverDefList);
          info.fillAntennaAccuracy(accuracyDefList);

          // approximate station position
          if(!fileNameStationPosition.empty())
          {
            try
            {
              Vector3dArc arc = InstrumentFile::read(fileNameStationPosition(fileNameVariableList));
              auto iter = (arc.size() == 1) ? arc.begin() : std::find_if(arc.begin(), arc.end(), [&](const Epoch &e){return e.time.isInInterval(times.front(), times.back());});
              if(iter != arc.end())
                info.approxPosition = iter->vector3d;
            }
            catch(std::exception &/*e*/)
            {
            }
          }

          // test completeness of antennas
          for(const auto &antenna : info.antenna)
            if(antenna.timeEnd > times.front() && antenna.timeStart <= times.back() && (!antenna.antennaDef || !antenna.accuracyDef))
              logWarningOnce<<info.markerName<<"."<<info.markerNumber<<": No "<<(!antenna.antennaDef ? "antenna" : "accuracy")<<" definition found for "<<antenna.str()<<Log::endl;

          GnssReceiverPtr recv = std::make_shared<GnssReceiver>(FALSE, TRUE, stationName.at(i).at(k), info, noPatternFoundAction, Vector(times.size(), TRUE), TRUE, 1.);
          receiversWithAlternatives.at(i).push_back(recv);
        }
        catch(std::exception &e)
        {
          logWarningOnce<<stationName.at(i).at(k)<<" disabled: "<<e.what()<<Log::endl;
        }
      }

    // remove empty stations
    receiversWithAlternatives.erase(std::remove_if(receiversWithAlternatives.begin(), receiversWithAlternatives.end(),
                                                   [](auto x) {return !x.size();}), receiversWithAlternatives.end());

    // read observations at single nodes
    // ---------------------------------
    logStatus<<"read observations"<<Log::endl;
    Vector receiverAlternative(receiversWithAlternatives.size());
    logTimerStart;
    for(UInt i=0; i<receiversWithAlternatives.size(); i++)
      if(i%Parallel::size(comm) == Parallel::myRank(comm)) // distribute to nodes
      {
        logTimerLoop(i, receiversWithAlternatives.size());
        for(UInt k=0; k<receiversWithAlternatives.at(i).size(); k++) // test alternatives
        {
          try
          {
            fileNameVariableList["station"]->setValue(receiversWithAlternatives.at(i).at(k)->name());
            GnssReceiverPtr recv = receiversWithAlternatives.at(i).at(k);
            recv->isMyRank_ = TRUE;

            recv->times = times;
            recv->clk.resize(times.size(), 0);
            recv->pos.resize(times.size(), recv->info.approxPosition);
            recv->vel.resize(times.size());
            recv->offset.resize(times.size());
            recv->global2local.resize(times.size(), inverse(localNorthEastUp(recv->info.approxPosition, Ellipsoid())));
            recv->local2antenna.resize(times.size());
            for(UInt idEpoch=0; idEpoch<times.size(); idEpoch++)
            {
              const UInt idAnt = recv->info.findAntenna(times.at(idEpoch));
              if((idAnt != NULLINDEX) && recv->info.antenna.at(idAnt).antennaDef && recv->info.antenna.at(idAnt).accuracyDef)
              {
                recv->offset.at(idEpoch)        = recv->info.antenna.at(idAnt).position - recv->info.referencePoint(times.at(idEpoch));
                recv->local2antenna.at(idEpoch) = recv->info.antenna.at(idAnt).local2antennaFrame;
              }
              else
                recv->disable(idEpoch);
            }

            // simulation case
            if(fileNameObs.empty())
            {
              receiverAlternative(i) = 1;
              break;
            }

            auto rotationCrf2Trf = std::bind(&EarthRotation::rotaryMatrix, earthRotation, std::placeholders::_1);
            recv->readObservations(fileNameObs(fileNameVariableList), transmitters, rotationCrf2Trf, timeMargin, elevationCutOff,
                                  useType, ignoreType, GnssObservation::RANGE | GnssObservation::PHASE);

            // count epochs with observations
            UInt countEpochs = 0;
            for(UInt idEpoch=0; idEpoch<times.size(); idEpoch++)
              if(recv->useable(idEpoch))
                countEpochs++;
            if(countEpochs*recv->observationSampling < minEstimableEpochsRatio*times.size()*medianSampling(times).seconds())
              continue;

            // found valid station
            receiverAlternative(i) = k+1;
            break;
          }
          catch(std::exception &e)
          {
            logWarning<<receiversWithAlternatives.at(i).at(k)->name()<<" disabled: "<<e.what()<<Log::endl;
          }
        }
      }
    Parallel::barrier(comm);
    logTimerLoopEnd(receiversWithAlternatives.size());
    Parallel::reduceSum(receiverAlternative, 0, comm);
    Parallel::broadCast(receiverAlternative, 0, comm);

    // store valid receivers
    // ---------------------
    for(UInt i=0; i<receiversWithAlternatives.size(); i++)
      if(receiverAlternative(i) > 0)
      {
        receivers.push_back(receiversWithAlternatives.at(i).at(receiverAlternative(i)-1));
        if(receivers.size() >= maxStationCount)
          break;
      }
    receiversAll.insert(receiversAll.end(), receivers.begin(), receivers.end());
    logInfo<<"  "<<receivers.size()<<" of "<<stationName.size()<<" stations used"<<Log::endl;

    // tides & loading
    // ---------------
    logStatus<<"compute tides & loading"<<Log::endl;
    Vector hn, ln;
    if(!deformationName.empty())
    {
      Matrix love;
      readFileMatrix(deformationName, love);
      hn = love.column(0);
      ln = love.column(1);

      // models contain the total mass (loading mass & deformation mass effect)
      if(!potentialName.empty())
      {
        Vector kn;
        readFileMatrix(potentialName, kn);
        for(UInt n=2; n<std::min(kn.rows(), hn.rows()); n++)
          hn(n) /= (1.+kn(n));
        for(UInt n=2; n<std::min(kn.rows(), ln.rows()); n++)
          ln(n) /= (1.+kn(n));
      }
    }

    std::vector<Vector3d> positions;
    for(auto &recv : receivers)
      if(recv->isMyRank())
        positions.push_back(recv->position(0));

    Vector gravity(positions.size()); // normal gravity
    for(UInt i=0; i<gravity.size(); i++)
      gravity(i) = Planets::normalGravity(positions.at(i));

    std::vector<Rotary3d> rotEarth(times.size());
    for(UInt idEpoch=0; idEpoch<times.size(); idEpoch++)
      rotEarth.at(idEpoch) = earthRotation->rotaryMatrix(times.at(idEpoch));

    std::vector<std::vector<Vector3d>> disp(positions.size(), std::vector<Vector3d>(times.size()));
    tides->deformation(times, positions, rotEarth, earthRotation, ephemerides, gravity, hn, ln, disp);
    gravityfield->deformation(times, positions, gravity, hn, ln, disp);
    tides        = nullptr;
    gravityfield = nullptr;

    // add displacements
    UInt idx = 0;
    for(auto &recv : receivers)
      if(recv->isMyRank())
      {
        for(UInt idEpoch=0; idEpoch<times.size(); idEpoch++)
          recv->offset.at(idEpoch) += recv->global2localFrame(idEpoch).transform(disp.at(idx).at(idEpoch));
        idx++;
      }
  }
  catch(std::exception &e)
  {
    GROOPS_RETHROW(e)
  }
}

/***********************************************/

void GnssReceiverGeneratorStationNetwork::preprocessing(Gnss *gnss, Parallel::CommunicatorPtr comm)
{
  try
  {
    logStatus<<"init observations"<<Log::endl;
    UInt disabledStations = 0;
    VariableList fileNameVariableList;
    addVariable("station", fileNameVariableList);
    Single::forEach(receivers.size(), [&](UInt idRecv)
    {
      Parallel::peek(comm);
      if(receivers.at(idRecv)->isMyRank())
      {
        try
        {
          auto recv = receivers.at(idRecv);
          fileNameVariableList["station"]->setValue(recv->name());
          recv->createTracks(gnss->transmitters, minObsCountPerTrack, {GnssType::L5_G});
          recv->estimateInitialClockErrorFromCodeObservations(gnss->transmitters, gnss->funcRotationCrf2Trf, gnss->funcReduceModels, huber, huberPower, codeMaxPosDiff, FALSE/*estimateKinematicPosition*/);
          GnssReceiver::ObservationEquationList eqn(*recv, gnss->transmitters, gnss->funcRotationCrf2Trf, gnss->funcReduceModels, GnssObservation::RANGE | GnssObservation::PHASE);
          recv->disableEpochsWithGrossCodeObservationOutliers(eqn, codeMaxPosDiff, 0.5);
          recv->writeTracks(fileNameTrackBefore, eqn, {GnssType::L5_G});
          recv->cycleSlipsDetection(eqn, minObsCountPerTrack, denoisingLambda, tecWindowSize, tecSigmaFactor, {GnssType::L5_G});
          recv->removeLowElevationTracks(eqn, elevationTrackMinimum);
          recv->trackOutlierDetection(eqn, {GnssType::L5_G}, huber, huberPower);
          recv->cycleSlipsRepairAtSameFrequency(eqn);
          recv->writeTracks(fileNameTrackAfter, eqn, {GnssType::L5_G});

          // count epochs with observations
          UInt countEpochs = 0;
          for(UInt idEpoch=0; idEpoch<gnss->times.size(); idEpoch++)
            if(recv->useable(idEpoch))
              countEpochs++;
          if(countEpochs*recv->observationSampling < minEstimableEpochsRatio*gnss->times.size()*medianSampling(gnss->times).seconds())
          {
            recv->disable();
            disabledStations++;
          }
        }
        catch(std::exception &e)
        {
          logWarning<<receivers.at(idRecv)->name()<<" disabled: "<<e.what()<<Log::endl;
          receivers.at(idRecv)->disable();
          disabledStations++;
        }
      }
    });

    Parallel::reduceSum(disabledStations, 0, comm);
    logInfo<<"  "<<disabledStations<<" disabled stations"<<Log::endl;
  }
  catch(std::exception &e)
  {
    GROOPS_RETHROW(e)
  }
}

/***********************************************/

void GnssReceiverGeneratorStationNetwork::simulation(const std::vector<GnssType> &types,
                                                     NoiseGeneratorPtr noiseClock, NoiseGeneratorPtr noiseObs,
                                                     Gnss *gnss, Parallel::CommunicatorPtr comm)
{
  try
  {
    logStatus<<"simulate observations"<<Log::endl;
    Single::forEach(receivers.size(), [&](UInt idRecv)
    {
      Parallel::peek(comm);
      if(receivers.at(idRecv)->isMyRank())
      {
        try
        {
          receivers.at(idRecv)->simulateObservations(types, noiseClock, noiseObs, gnss->transmitters,
                                                     gnss->funcRotationCrf2Trf, gnss->funcReduceModels,
                                                     minObsCountPerTrack, elevationCutOff, elevationTrackMinimum, useType, ignoreType,
                                                     GnssObservation::RANGE | GnssObservation::PHASE);
        }
        catch(std::exception &e)
        {
          logWarning<<receivers.at(idRecv)->name()<<" disabled: "<<e.what()<<Log::endl;
          receivers.at(idRecv)->disable();
        }
      }
    });
  }
  catch(std::exception &e)
  {
    GROOPS_RETHROW(e)
  }
}

/***********************************************/
