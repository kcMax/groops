/***********************************************/
/**
* @file gnssParametrizationSignalBiases.cpp
*
* @brief Signal biases.
* @see GnssParametrization
*
* @author Torsten Mayer-Guerr
* @date 2021-01-23
*
*/
/***********************************************/

#include "base/import.h"
#include "config/config.h"
#include "gnss/gnss.h"
#include "gnss/gnssTransceiverSelector/gnssTransceiverSelector.h"
#include "gnss/gnssParametrization/gnssParametrizationSignalBiases.h"

/***********************************************/

GnssParametrizationSignalBiases::GnssParametrizationSignalBiases(Config &config)
{
  try
  {
    readConfig(config, "name",                            name,                   Config::OPTIONAL, "parameter.signalBiases", "used for parameter selection");
    readConfig(config, "selectTransmitters",              selectTransmitters,     Config::DEFAULT,  R"(["all"])", "");
    readConfig(config, "selectReceivers",                 selectReceivers,        Config::DEFAULT,  R"(["all"])", "");
    readConfig(config, "outputfileSignalBiasTransmitter", fileNameOutTransmitter, Config::OPTIONAL, "", "variable {prn} available");
    readConfig(config, "outputfileSignalBiasReceiver",    fileNameOutReceiver,    Config::OPTIONAL, "", "variable {station} available");
    readConfig(config, "inputfileSignalBiasTransmitter",  fileNameInTransmitter,  Config::OPTIONAL, "", "variable {prn} available");
    readConfig(config, "inputfileSignalBiasReceiver",     fileNameInReceiver,     Config::OPTIONAL, "", "variable {station} available");
    if(isCreateSchema(config)) return;
  }
  catch(std::exception &e)
  {
    GROOPS_RETHROW(e)
  }
}

/***********************************************/

void GnssParametrizationSignalBiases::init(Gnss *gnss, Parallel::CommunicatorPtr /*comm*/)
{
  try
  {
    this->gnss = gnss;

    if(!fileNameInTransmitter.empty())
    {
      VariableList fileNameVariableList;
      addVariable("prn", fileNameVariableList);
      auto selectedTransmitters = selectTransmitters->select(gnss->transmitters);
      for(UInt idTrans=0; idTrans<gnss->transmitters.size(); idTrans++)
        if(selectedTransmitters.at(idTrans) && gnss->transmitters.at(idTrans)->useable())
        {
          try
          {
            fileNameVariableList["prn"]->setValue(gnss->transmitters.at(idTrans)->name());
            readFileGnssSignalBias(fileNameInTransmitter(fileNameVariableList), gnss->transmitters.at(idTrans)->signalBias);
          }
          catch(std::exception &/*e*/)
          {
            logWarningOnce<<"Unable to read signal bias file <"<<fileNameInTransmitter(fileNameVariableList)<<">, disabling transmitter."<<Log::endl;
            gnss->transmitters.at(idTrans)->disable();
          }
        }
    }

    if(!fileNameInReceiver.empty())
    {
      VariableList fileNameVariableList;
      addVariable("station", fileNameVariableList);
      auto selectedReceivers = selectReceivers->select(gnss->receivers);
      for(UInt idRecv=0; idRecv<gnss->receivers.size(); idRecv++)
        if(selectedReceivers.at(idRecv) && gnss->receivers.at(idRecv)->useable())
        {
          try
          {
            fileNameVariableList["station"]->setValue(gnss->receivers.at(idRecv)->name());
            readFileGnssSignalBias(fileNameInTransmitter(fileNameVariableList), gnss->receivers.at(idRecv)->signalBias);
          }
          catch(std::exception &/*e*/)
          {
            logWarningOnce<<"Unable to read signal bias file <"<<fileNameInReceiver(fileNameVariableList)<<">, disabling receiver."<<Log::endl;
            gnss->receivers.at(idRecv)->disable();
          }
        }
    }
  }
  catch(std::exception &e)
  {
    GROOPS_RETHROW(e)
  }
}

/***********************************************/

void GnssParametrizationSignalBiases::writeResults(const GnssNormalEquationInfo &normalEquationInfo, const std::string &suffix) const
{
  try
  {
    if(!isEnabled(normalEquationInfo, name))
      return;

    if(!fileNameOutTransmitter.empty() && !normalEquationInfo.isEachReceiverSeparately && Parallel::isMaster(normalEquationInfo.comm))
    {
      VariableList fileNameVariableList;
      addVariable("prn", "***", fileNameVariableList);
      logStatus<<"write transmitter signal biases to files <"<<fileNameOutTransmitter(fileNameVariableList).appendBaseName(suffix)<<">"<<Log::endl;
      auto selectedTransmitters = selectTransmitters->select(gnss->transmitters);
      for(auto trans : gnss->transmitters)
        if(trans->useable() && selectedTransmitters.at(trans->idTrans()))
        {
          GnssSignalBias signalBias = trans->signalBias;
          for(UInt idType=0; idType<signalBias.types.size(); idType++)
            if(signalBias.types.at(idType) == GnssType::PHASE)
              signalBias.biases.at(idType) = std::remainder(signalBias.biases.at(idType), LIGHT_VELOCITY/signalBias.types.at(idType).frequency());
          fileNameVariableList["prn"]->setValue(trans->name());
          writeFileGnssSignalBias(fileNameOutTransmitter(fileNameVariableList).appendBaseName(suffix), signalBias);
        }
    }

    if(!fileNameOutReceiver.empty())
    {
      VariableList fileNameVariableList;
      addVariable("station", "****", fileNameVariableList);
      logStatus<<"write receiver signal biases to files <"<<fileNameOutReceiver(fileNameVariableList).appendBaseName(suffix)<<">"<<Log::endl;
      auto selectedReceivers = selectReceivers->select(gnss->receivers);
      for(auto recv : gnss->receivers)
        if(recv->isMyRank() && selectedReceivers.at(recv->idRecv()) && normalEquationInfo.estimateReceiver.at(recv->idRecv()))
        {
          GnssSignalBias signalBias = recv->signalBias;
          for(UInt idType=0; idType<signalBias.types.size(); idType++)
            if(signalBias.types.at(idType) == GnssType::PHASE)
              signalBias.biases.at(idType) = std::remainder(signalBias.biases.at(idType), LIGHT_VELOCITY/signalBias.types.at(idType).frequency());
          fileNameVariableList["station"]->setValue(recv->name());
          writeFileGnssSignalBias(fileNameOutReceiver(fileNameVariableList).appendBaseName(suffix), signalBias);
        }
    }
  }
  catch(std::exception &e)
  {
    GROOPS_RETHROW(e)
  }
}

/***********************************************/
