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
#include "Uploader.hpp"

using namespace std;


//---------------------------------------------
//--------------- Constructor -----------------
//---------------------------------------------

Uploader::Uploader(INIReader& t_config)
    : config(t_config)
{
    auto log = logger();

    // Read in the uploader's base directory
    base_dir = config.Get("uploader", "base_dir", "");
    if (base_dir == "") {
        log->error("Invalid base directory: {}", base_dir);
        exit(EX_CONFIG);
    }
    log->info("Using base_dir {}", base_dir);

    // Read in the block size
    blocksize = (int) config.GetInteger("uploader", "blocksize", -1);
    if (blocksize <= 0) {
        log->error("Invalid block size: {}", blocksize);
        exit(EX_CONFIG);
    }
    log->info("Using a block size of {}", blocksize);

    // Read in the uploader's block placement policy
    policy = config.Get("uploader", "policy", "");
    if (policy == "") {
        log->error("Invalid placement policy: {}", policy);
        exit(EX_CONFIG);
    }
    log->info("Using a block placement policy of {}", policy);

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

    log->info("Uploader initalized");
}

//-----------------------------------------
//----- Get local server based on RTT -----
//-----------------------------------------

int Uploader::getLocalServer(vector<double> RTT){

    int locServer = 0;

    for (int n = 1; n < num_servers; ++n){
        if(RTT[locServer] > RTT[n]){
            locServer = n;
        }
    }
    return locServer;
}

//-------------------------------------------
//----- Get closest server based on RTT -----
//-------------------------------------------

int Uploader::getClosestServer(vector<double> RTT, int locServer){

    auto log = logger();   
    
    int closeServer = getRandomServer(locServer);

    for (int n = 0; n < num_servers; ++n){
        if( (RTT[closeServer] > RTT[n]) && (n != locServer) ){
            closeServer = n;
        }
    }
    return closeServer;
}

//--------------------------------------------
//----- Get farthest server based on RTT -----
//--------------------------------------------

int Uploader::getFarthestServer(vector<double> RTT, int locServer){

    int farServer = 0;
    for (int n = 1; n < num_servers; ++n){
        if( (RTT[farServer] < RTT[n]) && (n != locServer) ){
            farServer = n;
        }
    }
    return farServer;
}

//------------------------------------------
//----- Get random server based on RTT -----
//------------------------------------------

int Uploader::getRandomServer(int takenServer){

    if( takenServer == -1 ){

        return rand() % 4; // % num_servers
    }
    else{
        int randServer2 = takenServer;
        while( randServer2 == takenServer ){
            randServer2 = rand() % 4; // % num_servers
        }
        return randServer2;
    }
}

//-----------------------------------------------------------------             
//---------------- Get all bytes from given file ------------------             
//-----------------------------------------------------------------             

string Uploader::getAllBytes(string fileName) {                          


    std::ifstream is (base_dir + '/' + fileName, std::ifstream::binary);    

    if (is) {                                                               
        string ret;                                                     

        // get length of file:                                          
        is.seekg (0, is.end);                                           
        int length = is.tellg();                                        
        is.seekg (0, is.beg);                                           

        char * buffer = new char [length];                              

        is.read (buffer,length);                                        

        is.close();                                                     
        ret.assign(buffer, length);                                     

        return ret;                                                     
    }                                                                       
    return NULL;                                                            
}

//-------------------------------------------------------------------------     
//-------------- Turns file into a map of hashes to blocks ----------------     
//-------------------------------------------------------------------------     

vector<string> Uploader::getBlocks(string fileName) {                    

    vector<string> blockList;                                               
    string file_string = getAllBytes(fileName);                             

    for (size_t start = 0; start <  file_string.length(); start += blocksize) {

        int end = start + blocksize;                                    

        // Remaining bytes case                                         
        if ( file_string.length() - start < (unsigned)blocksize) {      
            end = file_string.length();                             
        }                                                               

        string block = file_string.substr(start, end - start);      
        blockList.push_back(block);                                     
    }                                                                       
    return blockList;                                                       
}

//----------------------------------------
//--------- Main upload function ---------
//----------------------------------------

void Uploader::upload()
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

/*
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
*/

    //-----------------------------------
    //-- Classify Servers based on RTT --
    //-----------------------------------

    vector<double> avgRTT;
    
    log->info("Calculating average RTTs\n");

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
        log->info("Server {} avg RTT: {}", n, avg);
    }

    // Assign servers based on RTTs
    int localServer = getLocalServer(avgRTT);
    int closestServer = getClosestServer(avgRTT, localServer);
    int farthestServer = getFarthestServer(avgRTT, localServer);


    // Print Results
    for (int n = 0; n < num_servers; ++n){
        log->info("Average RTT for server {}: {}", n, avgRTT[n]);
    }
    log->info("Local Server: {}", localServer);
    log->info("Closest Server: {}", closestServer);
    log->info("Farthest Server: {}", farthestServer);

    //--------------------------------------------------------
    //-- Parse base directory and populate maps accordingly --
    //--------------------------------------------------------

    FileInfoMap localMap; // typedef map<string, FileInfo> FileInfoMap; 

    map<string, string> localBlockMap; // maps hash, block data 



    vector<string> file_names;                                              

    if (auto dir = opendir(base_dir.c_str())) {                             
        while (auto f = readdir(dir)) {                                 
            if (!f->d_name || f->d_name[0] == '.')                  
                continue; // Skip everything that starts with a dot
            file_names.push_back(f->d_name);                        
            // log->info("File: {}", f->d_name);                    
        }                                                               
        closedir(dir);                                                  
    }                                                                       

    // for each file, compute that file’s hash list | create mapping for blocks and FileInfos
    for (string s: file_names) {                                            

        // blocks b0, b1, b2, and b3                                                        
        vector<string> blocks = getBlocks(s);
        list<string> hash_list;                                 

        // iterates through list of blocks for particular file
        for (auto b: blocks) { 

            // for each block
            string tmpHash = picosha2::hash256_hex_string(b); 

            //{h0:b0, h1:b2}
            localBlockMap.insert({tmpHash, b}); 

            // [h0, h1, h2, h3]
            hash_list.push_back(tmpHash); 

        }                                                       

        // version number should nev
        FileInfo fileI = std::make_tuple(1, hash_list); 
        localMap.insert({s, fileI});                            
    }

    //-----------------------------
    //-- Set up upload to server --
    //-----------------------------

    log->info("Upload to server");

    // Timing purposes
    auto starttime = std::chrono::high_resolution_clock::now();

    // Iterator so we can loop                                              
    std::map<string, FileInfo>::iterator local_it = localMap.begin();       

    // Loop through local files                                             
    while( local_it != localMap.end()){                                     

        string local_filename = local_it->first;                        

        // Get file info from local file                           
        FileInfo local_info = localMap[local_filename];            

        for (int n = 0; n < num_servers; n++){ 

            //log->info("Updating file {} on server {}", local_filename, n);           

            // Update remote fileinfo                         
            (void)clients[n]->call("record_file", local_filename, local_info);
        }


        // list of hashes of local file                            
        list<string> local_hashlist = get<1>(local_info);          

        // For randomizing 
        srand (time(NULL));
	
	// Iterate through all hashes to write to server        
        std::list<string>::iterator it;                         
        for (it = local_hashlist.begin(); it != local_hashlist.end(); ++it){

            string hash = it->c_str();                      
            string block = localBlockMap[hash];             


            //-------------------                                                      
            //-- random Policy --                                                      
            //-------------------
            
            if( policy == "random"){ 

                int randomServer = getRandomServer(-1);
                (void)clients[randomServer]->call("store_block", hash, block);
		
		//log->info("Block stored in server {} with policy: \"{}\"", randomServer, policy);
            }

            //----------------------                                                
            //-- tworandom Policy --                                                
            //----------------------
            
            else if( policy == "tworandom"){ 

                int randomServer = getRandomServer(-1);
                int randomServer2 = getRandomServer(randomServer);

                (void)clients[randomServer]->call("store_block", hash, block);
                (void)clients[randomServer2]->call("store_block", hash, block);

		//log->info("Block stored in server {} with policy: \"{}\"", randomServer, policy);
		//log->info("Block stored in server {} with policy: \"{}\"", randomServer2, policy);
            }
            
            //------------------
            //-- local Policy --
            //------------------
            
            else if( policy == "local"){

                (void)clients[localServer]->call("store_block", hash, block);

		//log->info("Block stored in server {} with policy: \"{}\"", localServer, policy);
            }

            //-------------------------                                             
            //-- localclosest Policy --                                             
            //------------------------- 
            
            else if( policy == "localclosest"){

                (void)clients[localServer]->call("store_block", hash, block);
                (void)clients[closestServer]->call("store_block", hash, block);

		//log->info("Block stored in server {} with policy: \"{}\"", localServer, policy);
		//log->info("Block stored in server {} with policy: \"{}\"", closestServer, policy);
            }
    
            //--------------------------                                            
            //-- localfarthest Policy --                                            
            //--------------------------
            
            else if( policy == "localfarthest"){

                (void)clients[localServer]->call("store_block", hash, block);
                (void)clients[farthestServer]->call("store_block", hash, block);

		//log->info("Block stored in server {} with policy: \"{}\"", localServer, policy);
		//log->info("Block stored in server {} with policy: \"{}\"", farthestServer, policy);
            }

            // Policy not handled
            else{
            
                log->error("Policy {} not handled", policy);
            }
        }                                                       
        local_it++;                                                     
    }

	auto finishtime = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> elapsedtime = finishtime - starttime;
	double finaltime = elapsedtime.count();

	log->info("Upload time: {}", finaltime);


    // Delete the clients
    for (int i = 0; i < num_servers; ++i)
    {
        log->info("Tearing down client {}", i);
        delete clients[i];
    }
}
