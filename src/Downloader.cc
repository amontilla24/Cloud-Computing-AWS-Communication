#include <sysexits.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <map>
#include <iostream>
#include <sstream>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <assert.h>

#include <stdlib.h>     // Random generator
#include <time.h>       // Random seed
#include <chrono>       // Timing library

#include "rpc/server.h"
#include "rpc/rpc_error.h"
#include "picosha2/picosha2.h"

#include "logger.hpp"
#include "Downloader.hpp"

using namespace std;


//--------------------------------------------------
//------------------- Constructor ------------------
//--------------------------------------------------

Downloader::Downloader(INIReader& t_config)
	: config(t_config)
{
	auto log = logger();

	// Read in the downloader's base directory
	base_dir = config.Get("downloader", "base_dir", "");
	if (base_dir == "") {
		log->error("Invalid base directory: {}", base_dir);
		exit(EX_CONFIG);
	}
	log->info("Using base_dir {}", base_dir);

	// Read in the block size
	blocksize = (int) config.GetInteger("downloader", "blocksize", -1);
	if (blocksize <= 0) {
		log->error("Invalid block size: {}", blocksize);
		exit(EX_CONFIG);
	}
	log->info("Using a block size of {}", blocksize);

	num_servers = (int) config.GetInteger("ssd", "num_servers", -1);
	if (num_servers <= 0) {
		log->error("num_servers {} is invalid", num_servers);
		exit(EX_CONFIG);
	}
	log->info("Number of servers: {}", num_servers);

	for (int i = 0; i < num_servers; ++i) {
		string servconf = config.Get("ssd", "server"+std::to_string(i), "");
		if (servconf == "") {
			log->error("Server {} not found in config file", i);
			exit(EX_CONFIG);
		}
		size_t idx = servconf.find(":");
		if (idx == string::npos) {
			log->error("Config line {} is invalid", servconf);
			exit(EX_CONFIG);
		}
		string host = servconf.substr(0, idx);
		int port = (int) strtol(servconf.substr(idx+1).c_str(), nullptr, 0);
		if (port <= 0 || port > 65535) {
			log->error("Invalid port number: {}", servconf);
			exit(EX_CONFIG);
		}

		log->info("  Server {}= {}:{}", i, host, port);
		ssdhosts.push_back(host);
		ssdports.push_back(port);
	}

	log->info("Downloader initalized");
}

//-----------------------------------------                                     
//----- Get local server based on RTT -----                                     
//-----------------------------------------                                     
                                                                                
int Downloader::getLocalServer(vector<double> RTT){                               
                                                                                
    int locServer = 0;                                                          
                                                                                
    for (int n = 1; n < num_servers; ++n){                                      
        if(RTT[locServer] > RTT[n]){                                            
            locServer = n;                                                      
        }                                                                       
    }                                                                           
    return locServer;                                                           
}

//-------------------------------------------------
//-- Get list of servers in ascending RTT order --
//------------------------------------------------
list <int> Downloader::getServerOrder(vector<double> RTT){

	auto log = logger();

	list<int> servers;

	for (int i = 0; i < num_servers; i++) {
		
		int order = 0;
		for (int n = 0; n < num_servers; n++){
			
			if(RTT[i] > RTT[n]){
				order++;
			} 
		}
		servers.push_back(order);
	}
	return servers;
}

//------------------------------------------------
//----------- Main download function -------------
//------------------------------------------------
void Downloader::download()
{
	auto log = logger();

	vector<rpc::client*> clients;

	// Connect to all of the servers
	for (int i = 0; i < num_servers; ++i)
	{
		log->info("Connecting to server {}", i);
		try {
			clients.push_back(new rpc::client(ssdhosts[i], ssdports[i]));
			clients[i]->set_timeout(RPC_TIMEOUT);
		} catch (rpc::timeout &t) {
			log->error("Unable to connect to server {}: {}", i, t.what());
			exit(-1);
		}
	}

	// Issue a ping to each server
	for (int i = 0; i < num_servers; ++i)
	{
		log->info("Pinging server {}", i);
		try {
			clients[i]->call("ping");
			log->info("  success");
		} catch (rpc::timeout &t) {
			log->error("Error pinging server {}: {}", i, t.what());
			exit(-1);
		}
	}



	//-----------------------------------                                       
	//-- Classify Servers based on RTT --                                       
	//-----------------------------------                                       

	vector<double> avgRTT;                                                      

	log->info("Calculating average RTTs");                                    

	for (int n = 0; n < num_servers; ++n){                                      

		log->info("Ping timing for server {}", n);                              

		int ping_num = 8;                                                       
		std::chrono::duration<double> elapsed;                                  
		double avg = 0.0;                                                       

		for (int i = 0; i < ping_num; ++i){                                     

			try {                                                               

				// Record start time                                            
				auto start = std::chrono::high_resolution_clock::now();         

				clients[n]->call("ping");                                       

				// Record end time                                              
				auto finish = std::chrono::high_resolution_clock::now();        

				elapsed = finish - start;                                       

				avg = avg + (elapsed.count()*1000)/ping_num;                    
				log->info("Ping {}: {}", i+1, (elapsed.count()*1000));        

			} catch (rpc::timeout &t) {                                         
				log->error("Error pinging server {}: {}", 0, t.what());         
				exit(-1);                                                       
			}                                                                   
		}                                                                       
		avgRTT.push_back(avg);                                                  
		log->info("Server {} avg RTT: {}ms", n, avg);                           
	}                                                                           

	// Print Results                                                            
	for (int n = 0; n < num_servers; ++n){                                      
		log->info("Average RTT for server {}: {}", n, avgRTT[n]);               
	}

	// Get local server
	int localServer = getLocalServer(avgRTT);

	// Get file info map from localhost
	FileInfoMap remoteMap = clients[localServer]->call("get_fileinfo_map").as<FileInfoMap>();;

	// Get list of server block maps
	map<int, list<string>> blockMapList;
	
	for (int n = 0; n < num_servers; ++n){ 
		
		log->info("Getting blocks from client {}", n);
		
		list<string> blocklist = clients[n]->call("get_stored_blocks").as<list<string>>();
		
		blockMapList[n] = blocklist;
	}

	list<int> orderServers = getServerOrder(avgRTT);
	
	//----------------------------------
	//-- Go through files to download --
	//----------------------------------

	log->info("Download from Server");

	// Timing purposes
        auto start = std::chrono::high_resolution_clock::now();

	auto rem_it = remoteMap.begin();

	while( rem_it != remoteMap.end()){

		// Get filename and hash list
		string remote_filename = rem_it->first;
		list<string> hash_list = get<1>(remoteMap[remote_filename]);

		map<int, string> downBlocks;

		// File to be created
		std::ofstream out(base_dir +"/"+ remote_filename);

		
		// Iterate through all hashes
		for (auto it = hash_list.begin(); it != hash_list.end(); it++){
	
			string hash = it->c_str();

			int downloaded = 0;
			for (std::list<int>::iterator s_it = orderServers.begin(); s_it != orderServers.end(); ++s_it){
				
				if(downloaded == 0){
					int s = *s_it;
				
					list<string> blockmap = blockMapList[s];
	
					if( std::find(blockmap.begin(), blockmap.end(), hash) != blockmap.end() ){
			
						string block = clients[s]->call("get_block", hash).as<string>();
						out << block;
					
						downloaded = 1;
								
					}
				}
			}
		}
			
		out.close();
		rem_it++;
	}

	auto finish = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> elapsedtime = finish - start;
        double finaltime = elapsedtime.count();

        log->info("Download time: {}", finaltime);

	// Delete the clients
	for (int i = 0; i < num_servers; ++i)
	{
		log->info("Tearing down client {}", i);
		delete clients[i];
	}
}
