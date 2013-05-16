/*
 * Copyright (c) 2011-2013
 *     Gabriel Hjort Blindell <ghb@kth.se>
 *     George Ungureanu <ugeorge@kth.se>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OF THIS SOFTWARE NOR THE
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "../../frontend/dumper.h"
#include "modelmodifiersysc.h"
#include "../SY/fanoutsy.h"
#include "../SY/delaysy.h"
#include "../../language/cfunction.h"
#include "../../language/cdatatype.h"
#include "../../tools/tools.h"
#include "../../exceptions/castexception.h"
#include "../../exceptions/indexoutofboundsexception.h"
#include <set>
#include <map>
#include <string>
#include <new>
#include <stdexcept>

using namespace f2cc;
using namespace f2cc::Forsyde;
using namespace f2cc::Forsyde::SY;
using std::string;
using std::list;
using std::map;
using std::set;
using std::vector;
using std::bad_alloc;
using std::pair;

ModelModifierSysC::ModelModifierSysC(ProcessNetwork* processnetwork,
		Logger& logger, Config& config)
        throw(InvalidArgumentException) : processnetwork_(processnetwork), logger_(logger),
        configuration_(config){
    if (!processnetwork) {
        THROW_EXCEPTION(InvalidArgumentException, "\"processnetwork\" must not be NULL");
    }
}

ModelModifierSysC::~ModelModifierSysC() throw() {}


void ModelModifierSysC::flattenAndParallelize() throw(
    		RuntimeException, InvalidModelException, InvalidProcessException, OutOfMemoryException){

	logger_.logMessage(Logger::INFO, "Flattening the process network and "
			"extracting data parallel processes ...");

	Composite* root = processnetwork_->getComposite(Id("f2cc0"));
	if(!root){
		THROW_EXCEPTION(InvalidModelException, string("Process network ")
						+ "does not have a root process");
	}

    list<Composite*> list_of_composites  = root->getComposites();
    for (list<Composite*>::iterator it = list_of_composites.begin(); it != list_of_composites.end(); ++it){
		flattenCompositeProcess(*it, root);
    }
    XmlDumper dump1(logger_);
    dump1.dump(processnetwork_, "flattened.xml");

    list<list<Leaf*> > equivalent_combs = extractEquivalentCombs(root);

	list<list<Leaf*> >::iterator equ_it;
	for (equ_it = equivalent_combs.begin(); equ_it != equivalent_combs.end(); ++equ_it){
		createParallelComposite(root,*equ_it);
	}
    XmlDumper dump2(logger_);
    dump2.dump(processnetwork_, "flattened1.xml");

	list<list<Leaf*> > equivalent_procs = extractEquivalentLeafs(root);
	while(equivalent_procs.size() != 0){
		for (equ_it = equivalent_procs.begin(); equ_it != equivalent_procs.end(); ++equ_it){
			createParallelComposite(root,*equ_it);
		}
		equivalent_procs = extractEquivalentLeafs(root);
	}
	removeRedundantZipsUnzips(root);
}

void ModelModifierSysC::firstCostAnalysis() throw(
		RuntimeException, InvalidModelException, InvalidProcessException, OutOfMemoryException){

	logger_.logMessage(Logger::INFO, "Flattening the process network and "
			"extracting data parallel processes ...");


}

void ModelModifierSysC::flattenCompositeProcess(Composite* composite, Composite* parent) throw(
		 RuntimeException, InvalidProcessException, InvalidArgumentException, OutOfMemoryException){
    if (!composite) {
        THROW_EXCEPTION(InvalidArgumentException, "\"composite\" must not be NULL");
    }
    if (!parent) {
        THROW_EXCEPTION(InvalidArgumentException, "\"parent\" must not be NULL");
    }

	logger_.logMessage(Logger::DEBUG, string() + "Moving leaf processes inside \""
				+ composite->getId()->getString()
				+ "\" to \""
				+ parent->getId()->getString()
				+ "\"...");

    list<Composite*> list_of_composites  = composite->getComposites();
    for (list<Composite*>::iterator it = list_of_composites.begin(); it != list_of_composites.end(); ++it){
		flattenCompositeProcess(*it, composite);
    }

	list<Leaf*> contained_leafs  = composite->getProcesses();
	for (list<Leaf*>::iterator nit = contained_leafs.begin(); nit != contained_leafs.end(); ++nit){
		moveToNewParent(*nit, composite, parent);
	}
	list<Composite::IOPort*> in_ports = composite->getInIOPorts();
	for (list<Composite::IOPort*>::iterator nit = in_ports.begin(); nit != in_ports.end(); ++nit){
		logger_.logMessage(Logger::DEBUG, string() + "Redirecting \""
					+ (*nit)->getConnectedPortOutside()->toString()
					+ "\" to \""
					+ (*nit)->getConnectedPortInside()->toString()+ "\"");
		(*nit)->getConnectedPortOutside()->connect((*nit)->getConnectedPortInside());

	}
	list<Composite::IOPort*> out_ports = composite->getOutIOPorts();
	for (list<Composite::IOPort*>::iterator nit = out_ports.begin(); nit != out_ports.end(); ++nit){
		logger_.logMessage(Logger::DEBUG, string() + "Redirecting \""
					+ (*nit)->getConnectedPortInside()->toString()
					+ "\" to \""
					+ (*nit)->getConnectedPortOutside()->toString()+ "\"");
		(*nit)->getConnectedPortInside()->connect((*nit)->getConnectedPortOutside());
	}

	logger_.logMessage(Logger::DEBUG, string() + "Removing \""
				+ composite->getId()->getString()
				+ "\"...");
	processnetwork_->removeComposite(*composite->getId());
	parent->deleteComposite(*composite->getId());
}



list<list<Leaf*> > ModelModifierSysC::extractEquivalentCombs(Composite* parent)
        throw(InvalidArgumentException, OutOfMemoryException){
    if (!parent) {
        THROW_EXCEPTION(InvalidArgumentException, "\"parent\" must not be NULL");
    }

    logger_.logMessage(Logger::DEBUG, string() + "Extracting equivalent Comb processes from "
    			+ parent->getId()->getString() + "...");

    list<list<Leaf*> > grouped_equivalent_processes;
    map< string, list<map<Id, SY::Comb*> > > table_of_equivalences;


	list<Leaf*> list_of_leafs = parent->getProcesses();
	list<Leaf*>::iterator it_leafs;

	for (it_leafs = list_of_leafs.begin(); it_leafs != list_of_leafs.end(); ++it_leafs){
		SY::Comb* curr_comb = dynamic_cast<SY::Comb*>(*it_leafs);
		if(curr_comb){
			string function_name = curr_comb->getFunction()->getName();
			map< string, list<map<Id, SY::Comb*> > >::iterator found_func_at =
					table_of_equivalences.find(function_name);
			if (found_func_at != table_of_equivalences.end()){
				logger_.logMessage(Logger::DEBUG, string() + "Verifying dependencies for \""
							+ curr_comb->getId()->getString()
							+ "\"...");

				bool fct_added = false;
				for (list<map<Id, SY::Comb*> >::iterator it = table_of_equivalences[function_name].begin();
						it != table_of_equivalences[function_name].end(); ++it){
					if (!foundDependencyDownstream(curr_comb, *it) &&
							!foundDependencyUpstream(curr_comb, *it)){
						(*it).insert(pair<Id, SY::Comb*>(*curr_comb->getId(), curr_comb));
						fct_added = true;

						break;
					}
				}
				visited_processes_.clear();
				if (!fct_added) {
					map<Id, SY::Comb*> new_map;
					new_map.insert(pair<Id, SY::Comb*>(*curr_comb->getId(), curr_comb));
					table_of_equivalences[function_name].push_back(new_map);
				}
			}
			else {
				list<map<Id, SY::Comb*> > new_list;
				map<Id, SY::Comb*> new_map;
				new_map.insert(pair<Id, SY::Comb*>(*curr_comb->getId(), curr_comb));
				new_list.push_back(new_map);
			    try {
			    	table_of_equivalences.insert(
			    			pair<string, list<map<Id, SY::Comb*> > >(function_name, new_list));
			    }
			    catch(bad_alloc&) {
			        THROW_EXCEPTION(OutOfMemoryException);
			    }
			}
		}
	}


	map< string, list<map<Id, SY::Comb*> > >::iterator itt;
	for (itt = table_of_equivalences.begin(); itt != table_of_equivalences.end(); ++itt){
		list<map<Id, SY::Comb*> >::iterator itl;
		for (itl = itt->second.begin(); itl != itt->second.end(); ++itl){
			if ((*itl).size() > 1){
				list<Leaf*> equivalent_combs;
				logger_.logMessage(Logger::DEBUG, string() + "Found "
							+ tools::toString((*itl).size())
							+ " equivalent Combs having function \""
							+ itt->first + "\". Adding them to the list...");
				map<Id, SY::Comb*>::iterator itm;
				for (itm = (*itl).begin(); itm != (*itl).end(); ++itm){
					try {
						equivalent_combs.push_back(itm->second);
					}
					catch(bad_alloc&) {
						THROW_EXCEPTION(OutOfMemoryException);
					}
				}
				try {
					grouped_equivalent_processes.push_back(equivalent_combs);
				}
				catch(bad_alloc&) {
					THROW_EXCEPTION(OutOfMemoryException);
				}
			}
		}
	}
    return grouped_equivalent_processes;
}


list<list<Leaf*> > ModelModifierSysC::extractEquivalentLeafs(Composite* parent)
        throw(InvalidArgumentException, OutOfMemoryException){
    if (!parent) {
        THROW_EXCEPTION(InvalidArgumentException, "\"parent\" must not be NULL");
    }

    logger_.logMessage(Logger::INFO, string() + "Extracting equivalent processes from "
    			+ parent->getId()->getString() + "...");

    list<list<Leaf*> > grouped_equivalent_processes;
    map< Id, list<Leaf*> > table_of_equivalences;

	list<Leaf*> list_of_leafs = parent->getProcesses();
	list<Leaf*>::iterator it_leafs;

	for (it_leafs = list_of_leafs.begin(); it_leafs != list_of_leafs.end(); ++it_leafs){
		if (!dynamic_cast<SY::Zipx*>(*it_leafs) && !dynamic_cast<SY::Unzipx*>(*it_leafs)){
			Id tag = Id("");
			bool foundTag = false;
			list<Leaf::Port*> out_ports = (*it_leafs)->getOutPorts();
			for (list<Leaf::Port*>::iterator itp = out_ports.begin(); itp != out_ports.end(); ++itp){
				Process* connected_proc = (*itp)->getConnectedPort()->getProcess();
				if(dynamic_cast<SY::Zipx*>(connected_proc)){
					tag = *connected_proc->getId();
					foundTag = true;
					break;
				}
			}
			if (!foundTag) {
				list<Leaf::Port*> in_ports = (*it_leafs)->getInPorts();
				for (list<Leaf::Port*>::iterator itp = in_ports.begin(); itp != in_ports.end(); ++itp){
					Process* connected_proc = (*itp)->getConnectedPort()->getProcess();
					if(dynamic_cast<SY::Unzipx*>(connected_proc)){
						tag = *connected_proc->getId();
						foundTag = true;
						break;
					}
				}
			}
			if (foundTag){
				map< Id, list<Leaf*> >::iterator found_func_at = table_of_equivalences.find(tag);
				if (found_func_at != table_of_equivalences.end()){
					logger_.logMessage(Logger::DEBUG, string() + "Found potential parallel process: "
								+ (*it_leafs)->getId()->getString() );
					found_func_at->second.push_back(*it_leafs);
				}
				else{
					list<Leaf*> new_list;
					new_list.push_back(*it_leafs);
					table_of_equivalences.insert(pair<Id, list<Leaf*> >(tag, new_list));
				}
			}
		}
	}

	map< Id, list<Leaf*> >::iterator itt;
	for (itt = table_of_equivalences.begin(); itt != table_of_equivalences.end(); ++itt){
		if (itt->second.size() > 1){
			logger_.logMessage(Logger::DEBUG, string() + "Found "
						+ tools::toString(itt->second.size())
						+ " equivalent leafs connected to \""
						+ itt->first.getString() + "\". Adding them to the list...");
			try {
				grouped_equivalent_processes.push_back(itt->second);
			}
			catch(bad_alloc&) {
				THROW_EXCEPTION(OutOfMemoryException);
			}
		}
	}
    return grouped_equivalent_processes;
}

void ModelModifierSysC::removeRedundantZipsUnzips(Forsyde::Composite* parent)
     throw(InvalidArgumentException, OutOfMemoryException){
    if (!parent) {
        THROW_EXCEPTION(InvalidArgumentException, "\"parent\" must not be NULL");
    }

    logger_.logMessage(Logger::INFO, string() + "Removing redundant Unzipx-Zipx sequences from "
    			+ parent->getId()->getString() + "...");

	list<Leaf*> list_of_leafs = parent->getProcesses();
	list<Leaf*>::iterator it_leafs;
	for (it_leafs = list_of_leafs.begin(); it_leafs != list_of_leafs.end(); ++it_leafs){
		SY::Unzipx* unzip = dynamic_cast<SY::Unzipx*>(*it_leafs);
		if (unzip){
			list<Leaf::Port*> oports = unzip->getOutPorts();
			Process* conencted_rpocess = oports.front()->getConnectedPort()->getProcess();
			if (!conencted_rpocess) break;
			SY::Zipx* pair_zip = dynamic_cast<SY::Zipx*>(conencted_rpocess);
			if (pair_zip){
				bool paired = true;
				list<Leaf::Port*>::iterator itp;
				for (itp = oports.begin(); itp != oports.end(); ++itp){
					if ((*itp)->getConnectedPort()->getProcess() != pair_zip){
						paired = false;
						break;
					}
				}
				if (paired){
					logger_.logMessage(Logger::DEBUG, string() + "Connecting \""
					    			+ unzip->getInPorts().front()->getConnectedPort()->toString()
					    			+ "\" with \""
					    			+ pair_zip->getOutPorts().front()->getConnectedPort()->toString()
					    			+"\"...");

					unzip->getInPorts().front()->getConnectedPort()->connect(
							pair_zip->getOutPorts().front()->getConnectedPort());


					processnetwork_->removeProcess(*pair_zip->getId());
					parent->removeProcess(*pair_zip->getId());
					processnetwork_->removeProcess(*unzip->getId());
					parent->removeProcess(*unzip->getId());
					//TODO: fix deallocation bug!
				}
			}
		}
	}
}


bool ModelModifierSysC::foundDependencyUpstream(Leaf* current_process,
		std::map<Id, SY::Comb*> to_compare_with)
 	 throw(RuntimeException, InvalidProcessException, InvalidArgumentException, OutOfMemoryException){

	visited_processes_.insert(pair<Id, bool>(*current_process->getId(), true));

    list<Leaf::Port*> in_ports = current_process->getInPorts();
    for (list<Leaf::Port*>::iterator it = in_ports.begin(); it != in_ports.end(); ++it){
    	if (dynamic_cast<Leaf::Port*>((*it)->getConnectedPort())){
    		Leaf* next_process = dynamic_cast<Leaf*>((*it)->getConnectedPort()->getProcess());
    		if (visited_processes_.find(*next_process->getId()) != visited_processes_.end())
    			return false;
    		if (to_compare_with.find(*next_process->getId()) != to_compare_with.end()) return true;
    		else {
    			if (foundDependencyUpstream(next_process, to_compare_with)) return true;
    		}
    	}
    }
    return false;
}

bool ModelModifierSysC::foundDependencyDownstream(Leaf* current_process,
		std::map<Id, SY::Comb*> to_compare_with)
 	 throw(RuntimeException, InvalidProcessException, InvalidArgumentException, OutOfMemoryException){

	visited_processes_.insert(pair<Id, bool>(*current_process->getId(), true));

    list<Leaf::Port*> out_ports = current_process->getOutPorts();
    for (list<Leaf::Port*>::iterator it = out_ports.begin(); it != out_ports.end(); ++it){
    	if (dynamic_cast<Leaf::Port*>((*it)->getConnectedPort())){
    		Leaf* next_process = dynamic_cast<Leaf*>((*it)->getConnectedPort()->getProcess());
    		if (visited_processes_.find(*next_process->getId()) != visited_processes_.end())
				return false;
    		if (to_compare_with.find(*next_process->getId()) != to_compare_with.end()) return true;
    		else {
    			if (foundDependencyDownstream(next_process, to_compare_with)) return true;
    		}
    	}
    }
    return false;
}


void ModelModifierSysC::createParallelComposite(Composite* parent, list<Leaf*> equivalent_processes)
              throw(RuntimeException, InvalidProcessException,InvalidArgumentException, OutOfMemoryException){
    if (!parent) {
        THROW_EXCEPTION(InvalidArgumentException, "\"parent\" must not be NULL");
    }

    //count the processes
    unsigned number_of_processes = 0;
	list<Leaf*>::iterator it_list;
	for (it_list = equivalent_processes.begin(); it_list != equivalent_processes.end(); ++it_list){
		ParallelComposite* par_comp = dynamic_cast<ParallelComposite*>(*it_list);
		if (par_comp) number_of_processes += par_comp->getNumProcesses();
		else number_of_processes++;
	}

    //create the process
    Hierarchy parent_hierarchy =  parent->getHierarchy();
    ParallelComposite* new_pcomp = new ParallelComposite(processnetwork_->getUniqueCompositeId("pcomp_"),
    		parent_hierarchy,Id(""), number_of_processes);

    processnetwork_->addComposite(new_pcomp);
    parent->addComposite(new_pcomp);

	//Extract the base process
    Process* reference_process = equivalent_processes.front();
    equivalent_processes.pop_front();

	logger_.logMessage(Logger::INFO, string() + "Creating a parallel composite process from \""
				+ reference_process->getId()->getString()
				+ "\" with " + tools::toString(number_of_processes) + " processes...");

    Leaf* leaf_ref = dynamic_cast<Leaf*>(reference_process);
    if (leaf_ref) prepareLeafForParallel(leaf_ref, parent, new_pcomp, number_of_processes);
    else {
		THROW_EXCEPTION(CastException, string("Process \"")
						+ reference_process->getId()->getString()
						+ "\" is not a leaf");
    }
    //redirect the data path through the parallel composite.
    while (equivalent_processes.size() != 0){
        Process* old_process = equivalent_processes.front();
        equivalent_processes.pop_front();
    	redirectFlowThroughParallelComposite(old_process, parent, new_pcomp);
    }
}



void ModelModifierSysC::prepareLeafForParallel(Leaf* reference_leaf, Composite* parent,
		 ParallelComposite* new_pcomp, unsigned number_of_processes)
	 throw(RuntimeException, InvalidProcessException, InvalidArgumentException, OutOfMemoryException){
    if (!reference_leaf) {
        THROW_EXCEPTION(InvalidArgumentException, "\"reference_leaf\" must not be NULL");
    }
    if (!parent) {
        THROW_EXCEPTION(InvalidArgumentException, "\"parent\" must not be NULL");
    }
    if (!new_pcomp) {
        THROW_EXCEPTION(InvalidArgumentException, "\"new_pcomp\" must not be NULL");
    }

    //copy the leaf to the ParallelComposite
    moveToParallelComposite(reference_leaf, parent, new_pcomp);

    Hierarchy parent_hierarchy = parent->getHierarchy();
    list<Leaf::Port*> in_ports = reference_leaf->getInPorts();
    for (list<Leaf::Port*>::iterator it = in_ports.begin(); it != in_ports.end(); ++it){
    	Leaf::Port* leaf_port = *it;

    	//add new IOPort to pcomp with array outside and scalar inside
    	new_pcomp->addInIOPort(*leaf_port->getId(), leaf_port->getDataType());
    	Composite::IOPort* new_pcomp_port = new_pcomp->getInIOPort(*(*it)->getId());
    	CDataType type_outside = CDataType(leaf_port->getDataType().getType(),
				true, true, (leaf_port->getDataType().getArraySize() * number_of_processes),
				false, true);
    	new_pcomp_port->setDataType(true,type_outside);

    	//creating a new Zipx and connect it to the new port
    	SY::Zipx* new_zip;
        try {
        	new_zip = new SY::Zipx(Id(processnetwork_->getUniqueProcessId("zipx_")),
        			parent_hierarchy, 0);
        }
        catch (bad_alloc&) {
            THROW_EXCEPTION(OutOfMemoryException);
        }
        parent->addProcess(new_zip);
        processnetwork_->addProcess(new_zip);

		//connect the out Zip port to the new ParallelComposite
        new_zip->addOutPort(Id("oport1"), type_outside);
        Leaf::Port* new_zip_oport = new_zip->getOutPort(Id("oport1"));
        new_pcomp_port->connect(new_zip_oport);
        logger_.logMessage(Logger::DEBUG, string() + "Created  \""
        				+ new_zip->getId()->getString()
        				+ "\" and connected it to \""
        				+ new_pcomp_port->toString() + "\"");

        //redirect the flow into this new Zip
        Process::Interface* connected_port = leaf_port->getConnectedPort();
        Composite::IOPort* connected_ioport = dynamic_cast<Composite::IOPort*>(connected_port);
        ParallelComposite* pcomp_parent = dynamic_cast<ParallelComposite*>(connected_port->getProcess());
        if(connected_ioport){
        	if(pcomp_parent && (connected_ioport->getConnectedPortInside() == leaf_port)){
        		connected_port = connected_ioport->getConnectedPortOutside();
        	}
        }
        Id new_port_id = Id("port_to_" + leaf_port->getProcess()->getId()->getString());
        new_zip->addInPort(new_port_id, leaf_port->getDataType());
		Leaf::Port* new_zip_iport = new_zip->getInPort(new_port_id);
		new_zip_iport->connect(connected_port);
		logger_.logMessage(Logger::DEBUG, string() + "Redirected \""
							+ connected_port->toString()
							+ "\" to \""
							+ new_zip_iport->toString() + "\"");

		//finally connect the inner pcomp ioport to the now-free leaf port
		new_pcomp_port->connect(leaf_port);
		logger_.logMessage(Logger::DEBUG, string() + "Finally, connected \""
							+ leaf_port->toString()
							+ "\" to \""
							+ new_pcomp_port->toString() + "\"");
    }

    list<Leaf::Port*> out_ports = reference_leaf->getOutPorts();
    for (list<Leaf::Port*>::iterator it = out_ports.begin(); it != out_ports.end(); ++it){
    	Leaf::Port* leaf_port = *it;

    	//add new IOPort to pcomp with array outside and scalar inside
    	new_pcomp->addOutIOPort(*leaf_port->getId(), leaf_port->getDataType());
    	Composite::IOPort* new_pcomp_port = new_pcomp->getOutIOPort(*(*it)->getId());
    	CDataType type_outside = CDataType(leaf_port->getDataType().getType(),
				true, true, (leaf_port->getDataType().getArraySize() * number_of_processes),
				false, true);
    	new_pcomp_port->setDataType(true,type_outside);

    	//creating a new Unzipx and connect it to the new port
    	SY::Unzipx* new_unzip;
        try {
        	new_unzip = new SY::Unzipx(Id(processnetwork_->getUniqueProcessId("unzipx_")),
        			parent_hierarchy, 0);
        }
        catch (bad_alloc&) {
            THROW_EXCEPTION(OutOfMemoryException);
        }
        parent->addProcess(new_unzip);
		processnetwork_->addProcess(new_unzip);
		//connect the out Zip port to the new ParallelComposite
        new_unzip->addInPort(Id("iport1"), type_outside);
        Leaf::Port* new_unzip_iport = new_unzip->getInPort(Id("iport1"));
        new_pcomp_port->connect(new_unzip_iport);
        logger_.logMessage(Logger::DEBUG, string() + "Created  \""
        				+ new_unzip->getId()->getString()
        				+ "\" and connected it to \""
        				+ new_pcomp_port->toString() + "\"");

        //redirect the flow into this new Zip
        Process::Interface* connected_port = leaf_port->getConnectedPort();
        Composite::IOPort* connected_ioport = dynamic_cast<Composite::IOPort*>(connected_port);
        ParallelComposite* pcomp_parent = dynamic_cast<ParallelComposite*>(connected_port->getProcess());
        if(connected_ioport){
        	if(pcomp_parent && (connected_ioport->getConnectedPortInside() == leaf_port)){
        		connected_port = connected_ioport->getConnectedPortOutside();
        	}
        }

        Id new_port_id = Id("port_from_" + leaf_port->getProcess()->getId()->getString());
        new_unzip->addOutPort(new_port_id, leaf_port->getDataType());
		Leaf::Port* new_unzip_oport = new_unzip->getOutPort(new_port_id);
		new_unzip_oport->connect(connected_port);
		logger_.logMessage(Logger::DEBUG, string() + "Redirected \""
							+ connected_port->toString()
							+ "\" to \""
							+ new_unzip_oport->toString() + "\"");

		//finally connect the inner pcomp ioport to the now-free leaf port
		new_pcomp_port->connect(leaf_port);
		logger_.logMessage(Logger::DEBUG, string() + "Finally, connected \""
							+ leaf_port->toString()
							+ "\" to \""
							+ new_pcomp_port->toString() + "\"");
    }
}

void ModelModifierSysC::moveToParallelComposite(Process* reference_process, Composite* old_parent,
		 ParallelComposite* new_parent) throw (InvalidProcessException, OutOfMemoryException){
    if (!reference_process) {
        THROW_EXCEPTION(InvalidArgumentException, "\"reference_process\" must not be NULL");
    }
    if (!old_parent) {
        THROW_EXCEPTION(InvalidArgumentException, "\"old_parent\" must not be NULL");
    }
    if (!new_parent) {
        THROW_EXCEPTION(InvalidArgumentException, "\"new_parent\" must not be NULL");
    }


    Hierarchy new_hierarchy = new_parent->getHierarchy();
    reference_process->setHierarchy(new_hierarchy);
    Leaf* new_leaf = dynamic_cast<Leaf*>(reference_process);
    if (new_leaf) {
    	old_parent->removeProcess(*new_leaf->getId());
    	new_parent->addProcess(new_leaf);

    	SY::Comb* new_comb = dynamic_cast<SY::Comb*>(reference_process);
    	if (new_comb) new_parent->changeName(Id(new_comb->getFunction()->getName()));
    	else new_parent->changeName(Id(string() + "pcomp_" + new_leaf->getId()->getString()));

    }
    else {
    	Composite* new_comp = dynamic_cast<Composite*>(reference_process);
    	if (new_comp){
    		old_parent->removeComposite(*new_comp->getId());
    		new_parent->addComposite(new_comp);
    		new_parent->changeName(new_comp->getName());
    	}
    }

    new_parent->setContainedProcessId(reference_process->getId());

    logger_.logMessage(Logger::DEBUG, string() + "Moved process  \""
				+ reference_process->getId()->getString()
				+ "\" of type " + reference_process->type()
				+ "  to its new parent \""
				+ new_parent->getId()->getString() + "\"");

}


void ModelModifierSysC::moveToNewParent(Process* reference_process, Composite* old_parent,
		Composite* new_parent) throw (InvalidProcessException, OutOfMemoryException){
    if (!reference_process) {
        THROW_EXCEPTION(InvalidArgumentException, "\"reference_process\" must not be NULL");
    }
    if (!old_parent) {
        THROW_EXCEPTION(InvalidArgumentException, "\"old_parent\" must not be NULL");
    }
    if (!new_parent) {
        THROW_EXCEPTION(InvalidArgumentException, "\"new_parent\" must not be NULL");
    }


    Hierarchy new_hierarchy = new_parent->getHierarchy();
    reference_process->setHierarchy(new_hierarchy);
    Leaf* new_leaf = dynamic_cast<Leaf*>(reference_process);
    if (new_leaf) {
    	old_parent->removeProcess(*new_leaf->getId());
    	new_parent->addProcess(new_leaf);
    }
    else {
    	Composite* new_comp = dynamic_cast<Composite*>(reference_process);
    	if (new_comp){
    		old_parent->removeComposite(*new_comp->getId());
    		new_parent->addComposite(new_comp);
    	}
    }

    logger_.logMessage(Logger::DEBUG, string() + "Moved process  \""
				+ reference_process->getId()->getString()
				+ "\" of type " + reference_process->type()
				+ "  to its new parent \""
				+ new_parent->getId()->getString() + "\"");

}


void ModelModifierSysC::redirectFlowThroughParallelComposite(Process* old_process, Composite* parent,
		ParallelComposite* new_pcomp) throw (
    		 InvalidArgumentException, RuntimeException, InvalidProcessException){
    if (!old_process) {
        THROW_EXCEPTION(InvalidArgumentException, "\"old_process\" must not be NULL");
    }
    if (!new_pcomp) {
        THROW_EXCEPTION(InvalidArgumentException, "\"new_pcomp\" must not be NULL");
    }

    logger_.logMessage(Logger::DEBUG, string() + "Redirecting the data flow from process \""
				+ old_process->getId()->getString()
				+ "\" to process \""
				+ new_pcomp->getId()->getString() + "\"...");

    Composite* comp = dynamic_cast<Composite*>(old_process);
    Leaf* leaf = dynamic_cast<Leaf*>(old_process);

    if (comp) {
    	list<Composite::IOPort*> in_ports = comp->getInIOPorts();
		for (list<Composite::IOPort*>::iterator it = in_ports.begin(); it != in_ports.end(); ++it){
			Process* connected_proc = new_pcomp->getInIOPort(*(*it)->getId())->getConnectedPortOutside(
					)->getProcess();
			SY::Zipx* in_zip = dynamic_cast<SY::Zipx*>(connected_proc);
			if (!in_zip){
				THROW_EXCEPTION(InvalidProcessException,
								string("Process \"")
								+ old_process->getId()->getString()
								+ "\" is not Zipx");
			}
			Process::Interface* connected_interface = (*it)->getConnectedPortOutside();
			Leaf::Port* connected_port = dynamic_cast<Leaf::Port*>(connected_interface);
			Composite::IOPort* connected_ioport = dynamic_cast<Composite::IOPort*>(connected_interface);

			//add a new port to the connected zip and redirect the flow through it
			Id new_port_id = Id("port_to_" + (*it)->getProcess()->getId()->getString());
			in_zip->addInPort(new_port_id, (*it)->getDataType().first);
			Leaf::Port* new_port = in_zip->getInPort(new_port_id);
			if(connected_port) connected_port->connect(new_port);
			else if(connected_ioport) connected_ioport->connect(new_port);

		}
    	list<Composite::IOPort*> out_ports = comp->getOutIOPorts();
		for (list<Composite::IOPort*>::iterator it = out_ports.begin(); it != out_ports.end(); ++it){
			Process* connected_proc = new_pcomp->getOutIOPort(*(*it)->getId())->getConnectedPortOutside(
					)->getProcess();
			SY::Unzipx* out_unzip = dynamic_cast<SY::Unzipx*>(connected_proc);
			if (!out_unzip){
				THROW_EXCEPTION(InvalidProcessException,
								string("Process \"")
								+ old_process->getId()->getString()
								+ "\" is not Unzipx");
			}
			Process::Interface* connected_interface = (*it)->getConnectedPortOutside();
			Leaf::Port* connected_port = dynamic_cast<Leaf::Port*>(connected_interface);
			Composite::IOPort* connected_ioport = dynamic_cast<Composite::IOPort*>(connected_interface);

			//add a new port to the connected zip and redirect the flow through it
			Id new_port_id = Id("port_from_" + (*it)->getProcess()->getId()->getString());
			out_unzip->addOutPort(new_port_id, (*it)->getDataType().first);
			Leaf::Port* new_port = out_unzip->getOutPort(new_port_id);
			if(connected_port) connected_port->connect(new_port);
			else if(connected_ioport) connected_ioport->connect(new_port);


		}

		processnetwork_->removeComposite(*comp->getId());
		parent->deleteComposite(*comp->getId());
    }
    else if (leaf){
    	list<Leaf::Port*> in_ports = leaf->getInPorts();
		for (list<Leaf::Port*>::iterator it = in_ports.begin(); it != in_ports.end(); ++it){
			Process* connected_proc = new_pcomp->getInIOPort(*(*it)->getId())->getConnectedPortOutside(
					)->getProcess();
			SY::Zipx* in_zip = dynamic_cast<SY::Zipx*>(connected_proc);
			if (!in_zip){
				THROW_EXCEPTION(InvalidProcessException,
								string("Process \"")
								+ old_process->getId()->getString()
								+ "\" is not Zipx");
			}
			Process::Interface* connected_interface = (*it)->getConnectedPort();
			Leaf::Port* connected_port = dynamic_cast<Leaf::Port*>(connected_interface);
			Composite::IOPort* connected_ioport = dynamic_cast<Composite::IOPort*>(connected_interface);

			//add a new port to the connected zip and redirect the flow through it
			Id new_port_id = Id("port_to_" + (*it)->getProcess()->getId()->getString());
			in_zip->addInPort(new_port_id, (*it)->getDataType());
			Leaf::Port* new_port = in_zip->getInPort(new_port_id);
			if(connected_port) connected_port->connect(new_port);
			else if(connected_ioport) connected_ioport->connect(new_port);


		}
    	list<Leaf::Port*> out_ports = leaf->getOutPorts();
		for (list<Leaf::Port*>::iterator it = out_ports.begin(); it != out_ports.end(); ++it){
			Process* connected_proc = new_pcomp->getOutIOPort(*(*it)->getId())->getConnectedPortOutside(
					)->getProcess();
			SY::Unzipx* out_unzip = dynamic_cast<SY::Unzipx*>(connected_proc);
			if (!out_unzip){
				THROW_EXCEPTION(InvalidProcessException,
								string("Process \"")
								+ old_process->getId()->getString()
								+ "\" is not Unzipx");
			}
			Process::Interface* connected_interface = (*it)->getConnectedPort();
			Leaf::Port* connected_port = dynamic_cast<Leaf::Port*>(connected_interface);
			Composite::IOPort* connected_ioport = dynamic_cast<Composite::IOPort*>(connected_interface);

			//add a new port to the connected zip and redirect the flow through it
			Id new_port_id = Id("port_from_" + (*it)->getProcess()->getId()->getString());
			out_unzip->addOutPort(new_port_id, (*it)->getDataType());
			Leaf::Port* new_port = out_unzip->getOutPort(new_port_id);
			if(connected_port) connected_port->connect(new_port);
			else if(connected_ioport) connected_ioport->connect(new_port);


		}
		processnetwork_->removeProcess(*leaf->getId());
		parent->deleteProcess(*leaf->getId());
    }
    else {
		THROW_EXCEPTION(InvalidProcessException,
						string("Process \"")
						+ old_process->getId()->getString()
						+ "\" is of unknown type");
    }

}

///////////////////////////////////////////////////////////////
/*
ModelModifierSysC::ContainedSection::ContainedSection(Process* start, Process* end)
        throw(InvalidArgumentException) : start(start), end(end) {
    if (!start) {
        THROW_EXCEPTION(InvalidArgumentException, "\"start\" must not be NULL");
    }
    if (!end) {
        THROW_EXCEPTION(InvalidArgumentException, "\"end\" must not be NULL");
    }
}

string ModelModifierSysC::ContainedSection::toString() const throw() {
    return string("\"") + start->getId()->getString() + "--" +
        end->getId()->getString() + "\"";
}
*/
