/* Demo_04_Model.cpp */

#include <stdio.h>
#include <vector>
#include <boost/mpi.hpp>
#include "repast_hpc/AgentId.h"
#include "repast_hpc/RepastProcess.h"
#include "repast_hpc/Utilities.h"
#include "repast_hpc/Properties.h"
#include "repast_hpc/initialize_random.h"
#include "repast_hpc/SVDataSetBuilder.h"
#include "repast_hpc/Point.h"

#include "Demo_04_Model.h"


RepastHPCDemoAgentPackageProvider::RepastHPCDemoAgentPackageProvider(repast::SharedContext<RepastHPCDemoAgent>* agentPtr): agents(agentPtr){ }

void RepastHPCDemoAgentPackageProvider::providePackage(RepastHPCDemoAgent * agent, std::vector<RepastHPCDemoAgentPackage>& out){
    repast::AgentId id = agent->getId();
    RepastHPCDemoAgentPackage package(id.id(), id.startingRank(), id.agentType(), id.currentRank(), agent->getC(), agent->getTotal());
    out.push_back(package);
}

void RepastHPCDemoAgentPackageProvider::provideContent(repast::AgentRequest req, std::vector<RepastHPCDemoAgentPackage>& out){
    std::vector<repast::AgentId> ids = req.requestedAgents();
    for(size_t i = 0; i < ids.size(); i++){
        providePackage(agents->getAgent(ids[i]), out);
    }
}


RepastHPCDemoAgentPackageReceiver::RepastHPCDemoAgentPackageReceiver(repast::SharedContext<RepastHPCDemoAgent>* agentPtr): agents(agentPtr){}

RepastHPCDemoAgent * RepastHPCDemoAgentPackageReceiver::createAgent(RepastHPCDemoAgentPackage package){
    repast::AgentId id(package.id, package.rank, package.type, package.currentRank);
    return new RepastHPCDemoAgent(id, package.c, package.total);
}

void RepastHPCDemoAgentPackageReceiver::updateAgent(RepastHPCDemoAgentPackage package){
    repast::AgentId id(package.id, package.rank, package.type);
    RepastHPCDemoAgent * agent = agents->getAgent(id);
    agent->set(package.currentRank, package.c, package.total);
}



DataSource_AgentTotals::DataSource_AgentTotals(repast::SharedContext<RepastHPCDemoAgent>* c) : context(c){ }

int DataSource_AgentTotals::getData(){
	int sum = 0;
	repast::SharedContext<RepastHPCDemoAgent>::const_local_iterator iter    = context->localBegin();
	repast::SharedContext<RepastHPCDemoAgent>::const_local_iterator iterEnd = context->localEnd();
	while( iter != iterEnd) {
		sum+= (*iter)->getTotal();
		iter++;
	}
	return sum;
}

DataSource_AgentCTotals::DataSource_AgentCTotals(repast::SharedContext<RepastHPCDemoAgent>* c) : context(c){ }

int DataSource_AgentCTotals::getData(){
	int sum = 0;
	repast::SharedContext<RepastHPCDemoAgent>::const_local_iterator iter    = context->localBegin();
	repast::SharedContext<RepastHPCDemoAgent>::const_local_iterator iterEnd = context->localEnd();
	while( iter != iterEnd) {
		sum+= (*iter)->getC();
		iter++;
	}
	return sum;
}


DemoDiffusor::DemoDiffusor(){ }

DemoDiffusor::~DemoDiffusor(){ }

double DemoDiffusor::getNewValue(double* values){
    repast::RelativeLocation relLoc(3); // Radius is 1, in 3-D; initialized to -1, -1, -1
    int countOfNeighbors = 0;
    double increase = 0;
    do{
        int index = relLoc.getIndex();
        if(!isnan(values[index])){
            countOfNeighbors++;
            increase += values[index] * .002;
        }
    }while(relLoc.increment(true)); // Loop until it's gone through all neighbors, skipping the center cell
           

    int indexOfCenter = relLoc.getIndexOfCenter();
    double originalValue = isnan(values[indexOfCenter]) ? 0 : values[indexOfCenter]; // Shouldn't happen
    double decrease = ((double)countOfNeighbors) * (originalValue * .002); // Lose 2 percent to each neighbor
    if(increase > 0 || decrease > 0 || originalValue > 0) std::cout << " INCREASE: " << increase << " DECREASE " << decrease << " OV " << originalValue << (originalValue > 1 ? " HIGH " : "") << "  RESULT: " << (originalValue - decrease + increase) << std::endl;
    return (originalValue - decrease + increase);

}



RepastHPCDemoModel::RepastHPCDemoModel(std::string propsFile, int argc, char** argv, boost::mpi::communicator* comm): context(comm){
	props = new repast::Properties(propsFile, argc, argv, comm);
	stopAt = repast::strToInt(props->getProperty("stop.at"));
	countOfAgents = repast::strToInt(props->getProperty("count.of.agents"));
	initializeRandom(*props, comm);
	if(repast::RepastProcess::instance()->rank() == 0) props->writeToSVFile("./output/record.csv");
	provider = new RepastHPCDemoAgentPackageProvider(&context);
	receiver = new RepastHPCDemoAgentPackageReceiver(&context);
	
    repast::Point<double> origin(-100,-100,-100);
    repast::Point<double> extent(200, 200, 200);
    
    repast::GridDimensions gd(origin, extent);
    
    std::vector<int> processDims;
    processDims.push_back(2);
    processDims.push_back(2);
    processDims.push_back(2);
    
    valueLayer = new repast::DiffusionLayerND<double>(processDims, gd, 2, true, 0, 1);
    
    discreteSpace = new repast::SharedDiscreteSpace<RepastHPCDemoAgent, repast::WrapAroundBorders, repast::SimpleAdder<RepastHPCDemoAgent> >("AgentDiscreteSpace", gd, processDims, 2, comm);
	
    std::cout << "RANK " << repast::RepastProcess::instance()->rank() << " BOUNDS: " << discreteSpace->bounds().origin() << " " << discreteSpace->bounds().extents() << std::endl;

    std::cout << "RANK " << repast::RepastProcess::instance()->rank() << " DIMENSIONS: " << discreteSpace->dimensions() << std::endl;

    const repast::GridDimensions& valueLayerDims = valueLayer->getLocalBoundaries();
    std::cout << "RANK " << repast::RepastProcess::instance()->rank() << " VALUE LAYER BOUNDS: " << valueLayerDims << std::endl;

    repast::Point<int> placeAt(discreteSpace->dimensions().origin().getX() + 32, discreteSpace->dimensions().origin().getY() + 32, discreteSpace->dimensions().origin().getZ() + 32);
    bool errFlag = false;
    valueLayer->setValueAt(100, placeAt, errFlag);
    valueLayer->write("./","TEST_AFTER_INIT",true);

    
   	context.addProjection(discreteSpace);
    
	// Data collection
	// Create the data set builder
	std::string fileOutputName("./output/agent_total_data.csv");
	repast::SVDataSetBuilder builder(fileOutputName.c_str(), ",", repast::RepastProcess::instance()->getScheduleRunner().schedule());
	
	// Create the individual data sets to be added to the builder
	DataSource_AgentTotals* agentTotals_DataSource = new DataSource_AgentTotals(&context);
	builder.addDataSource(createSVDataSource("Total", agentTotals_DataSource, std::plus<int>()));

	DataSource_AgentCTotals* agentCTotals_DataSource = new DataSource_AgentCTotals(&context);
	builder.addDataSource(createSVDataSource("C", agentCTotals_DataSource, std::plus<int>()));

	// Use the builder to create the data set
	agentValues = builder.createDataSet();
	
}

RepastHPCDemoModel::~RepastHPCDemoModel(){
	delete props;
	delete provider;
	delete receiver;
	
	delete agentValues;
	delete valueLayer;

}

void RepastHPCDemoModel::init(){
	int rank = repast::RepastProcess::instance()->rank();
	for(int i = 0; i < countOfAgents; i++){
        repast::Point<int> initialLocation((int)discreteSpace->dimensions().origin().getX() + i,(int)discreteSpace->dimensions().origin().getY() + i, (int)discreteSpace->dimensions().origin().getZ() + i);
		repast::AgentId id(i, rank, 0);
		id.currentRank(rank);
		RepastHPCDemoAgent* agent = new RepastHPCDemoAgent(id);
		context.addAgent(agent);
        discreteSpace->moveTo(id, initialLocation);
	}
}

void RepastHPCDemoModel::requestAgents(){
	int rank = repast::RepastProcess::instance()->rank();
	int worldSize= repast::RepastProcess::instance()->worldSize();
	repast::AgentRequest req(rank);
	for(int i = 0; i < worldSize; i++){                     // For each process
		if(i != rank){                                      // ... except this one
			std::vector<RepastHPCDemoAgent*> agents;        
			context.selectAgents(5, agents);                // Choose 5 local agents randomly
			for(size_t j = 0; j < agents.size(); j++){
				repast::AgentId local = agents[j]->getId();          // Transform each local agent's id into a matching non-local one
				repast::AgentId other(local.id(), i, 0);
				other.currentRank(i);
				req.addRequest(other);                      // Add it to the agent request
			}
		}
	}
    repast::RepastProcess::instance()->requestAgents<RepastHPCDemoAgent, RepastHPCDemoAgentPackage, RepastHPCDemoAgentPackageProvider, RepastHPCDemoAgentPackageReceiver>(context, req, *provider, *receiver, *receiver);
}

void RepastHPCDemoModel::cancelAgentRequests(){
	int rank = repast::RepastProcess::instance()->rank();
	if(rank == 0) std::cout << "CANCELING AGENT REQUESTS" << std::endl;
	repast::AgentRequest req(rank);
	
	repast::SharedContext<RepastHPCDemoAgent>::const_state_aware_iterator non_local_agents_iter  = context.begin(repast::SharedContext<RepastHPCDemoAgent>::NON_LOCAL);
	repast::SharedContext<RepastHPCDemoAgent>::const_state_aware_iterator non_local_agents_end   = context.end(repast::SharedContext<RepastHPCDemoAgent>::NON_LOCAL);
	while(non_local_agents_iter != non_local_agents_end){
		req.addCancellation((*non_local_agents_iter)->getId());
		non_local_agents_iter++;
	}
    repast::RepastProcess::instance()->requestAgents<RepastHPCDemoAgent, RepastHPCDemoAgentPackage, RepastHPCDemoAgentPackageProvider, RepastHPCDemoAgentPackageReceiver>(context, req, *provider, *receiver, *receiver);
	
	std::vector<repast::AgentId> cancellations = req.cancellations();
	std::vector<repast::AgentId>::iterator idToRemove = cancellations.begin();
	while(idToRemove != cancellations.end()){
		context.importedAgentRemoved(*idToRemove);
		idToRemove++;
	}
}


void RepastHPCDemoModel::removeLocalAgents(){
	int rank = repast::RepastProcess::instance()->rank();
	if(rank == 0) std::cout << "REMOVING LOCAL AGENTS" << std::endl;
	for(int i = 0; i < 5; i++){
		repast::AgentId id(i, rank, 0);
		repast::RepastProcess::instance()->agentRemoved(id);
		context.removeAgent(id);
	}
    repast::RepastProcess::instance()->synchronizeAgentStatus<RepastHPCDemoAgent, RepastHPCDemoAgentPackage, RepastHPCDemoAgentPackageProvider, RepastHPCDemoAgentPackageReceiver>(context, *provider, *receiver, *receiver);
}


void RepastHPCDemoModel::doSomething(){
	int whichRank = 0;
	if(repast::RepastProcess::instance()->rank() == whichRank) std::cout << " TICK " << repast::RepastProcess::instance()->getScheduleRunner().currentTick() << std::endl;

	if(repast::RepastProcess::instance()->rank() == whichRank){
		std::cout << "LOCAL AGENTS:" << std::endl;
		for(int r = 0; r < 4; r++){
			for(int i = 0; i < 10; i++){
				repast::AgentId toDisplay(i, r, 0);
				RepastHPCDemoAgent* agent = context.getAgent(toDisplay);
				if((agent != 0) && (agent->getId().currentRank() == whichRank)){
                    std::vector<int> agentLoc;
                    discreteSpace->getLocation(agent->getId(), agentLoc);
                    repast::Point<int> agentLocation(agentLoc);
                    std::cout << agent->getId() << " " << agent->getC() << " " << agent->getTotal() << " AT " << agentLocation << std::endl;
                }
			}
		}
		
		std::cout << "NON LOCAL AGENTS:" << std::endl;
		for(int r = 0; r < 4; r++){
			for(int i = 0; i < 10; i++){
				repast::AgentId toDisplay(i, r, 0);
				RepastHPCDemoAgent* agent = context.getAgent(toDisplay);
				if((agent != 0) && (agent->getId().currentRank() != whichRank)){
                    std::vector<int> agentLoc;
                    discreteSpace->getLocation(agent->getId(), agentLoc);
                    repast::Point<int> agentLocation(agentLoc);
                    std::cout << agent->getId() << " " << agent->getC() << " " << agent->getTotal() << " AT " << agentLocation << std::endl;
                }
			}
		}
	}

	
	
	std::vector<RepastHPCDemoAgent*> agents;
	context.selectAgents(repast::SharedContext<RepastHPCDemoAgent>::LOCAL, countOfAgents, agents);
	std::vector<RepastHPCDemoAgent*>::iterator it = agents.begin();
	while(it != agents.end()){
        (*it)->play(&context);
		it++;
    }

    bool doWrite =(repast::RepastProcess::instance()->getScheduleRunner().currentTick() == 3.0);
    if(doWrite)  valueLayer->write("./","TEST_BEFORE_DIFFUSE",true);
    
    DemoDiffusor diffusor; // Create a diffusor object
    std::cout << "DIFFUSING..." << std::endl;
    valueLayer->diffuse(&diffusor);  // Note: overwrites secondary layer, switches, and synchronizes
    std::cout << "DONE DIFFUSING..." << std::endl;
    if(doWrite)  valueLayer->write("./","TEST_AFTER_DIFFUSE",true);
    
    valueLayer->copyCurrentToSecondary();  // Destroy the old values in the old layer and replace with the new values
    
    
    it = agents.begin();
    while(it != agents.end()){
		(*it)->processValues(valueLayer, discreteSpace);
		it++;
    }

    valueLayer->switchValueLayer();        // Begin using the new values
    
    std::cout << " VALUE LAYER SYNCHRONIZING ON RANK " << std::endl;
    valueLayer->synchronize();
    std::cout << " VALUE LAYER DONE SYNCHRONIZING ON RANK " << std::endl;
    
    it = agents.begin();
    while(it != agents.end()){
		(*it)->move(discreteSpace);
		it++;
    }

	discreteSpace->balance();
    repast::RepastProcess::instance()->synchronizeAgentStatus<RepastHPCDemoAgent, RepastHPCDemoAgentPackage, RepastHPCDemoAgentPackageProvider, RepastHPCDemoAgentPackageReceiver>(context, *provider, *receiver, *receiver);
    
    repast::RepastProcess::instance()->synchronizeProjectionInfo<RepastHPCDemoAgent, RepastHPCDemoAgentPackage, RepastHPCDemoAgentPackageProvider, RepastHPCDemoAgentPackageReceiver>(context, *provider, *receiver, *receiver);

	repast::RepastProcess::instance()->synchronizeAgentStates<RepastHPCDemoAgentPackage, RepastHPCDemoAgentPackageProvider, RepastHPCDemoAgentPackageReceiver>(*provider, *receiver);
    
}

void RepastHPCDemoModel::initSchedule(repast::ScheduleRunner& runner){
	runner.scheduleEvent(1, repast::Schedule::FunctorPtr(new repast::MethodFunctor<RepastHPCDemoModel> (this, &RepastHPCDemoModel::requestAgents)));
	runner.scheduleEvent(2, 1, repast::Schedule::FunctorPtr(new repast::MethodFunctor<RepastHPCDemoModel> (this, &RepastHPCDemoModel::doSomething)));
	//runner.scheduleEvent(3, repast::Schedule::FunctorPtr(new repast::MethodFunctor<RepastHPCDemoModel> (this, &RepastHPCDemoModel::moveAgents)));
	runner.scheduleEndEvent(repast::Schedule::FunctorPtr(new repast::MethodFunctor<RepastHPCDemoModel> (this, &RepastHPCDemoModel::recordResults)));
	runner.scheduleStop(stopAt);
	
	// Data collection
	runner.scheduleEvent(1.5, 5, repast::Schedule::FunctorPtr(new repast::MethodFunctor<repast::DataSet>(agentValues, &repast::DataSet::record)));
	runner.scheduleEvent(10.6, 10, repast::Schedule::FunctorPtr(new repast::MethodFunctor<repast::DataSet>(agentValues, &repast::DataSet::write)));
	runner.scheduleEndEvent(repast::Schedule::FunctorPtr(new repast::MethodFunctor<repast::DataSet>(agentValues, &repast::DataSet::write)));
}

void RepastHPCDemoModel::recordResults(){
	if(repast::RepastProcess::instance()->rank() == 0){
		props->putProperty("Result","Passed");
		std::vector<std::string> keyOrder;
		keyOrder.push_back("RunNumber");
		keyOrder.push_back("stop.at");
		keyOrder.push_back("Result");
		props->writeToSVFile("./output/results.csv", keyOrder);
    }
}


	
