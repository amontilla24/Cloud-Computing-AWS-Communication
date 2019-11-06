#include <sysexits.h>
#include <string>

#include "rpc/server.h"

#include "logger.hpp"
#include "SurfStoreTypes.hpp"
#include "SurfStoreServer.hpp"

SurfStoreServer::SurfStoreServer(INIReader& t_config, int t_servernum)
    : config(t_config), servernum(t_servernum)
{
    auto log = logger();

	// pull our address and port
	string serverid = "server" + std::to_string(servernum);
	string servconf = config.Get("ssd", serverid, "");
	if (servconf == "") {
		log->error("{} not found in config file", serverid);
		exit(EX_CONFIG);
	}
	size_t idx = servconf.find(":");
	if (idx == string::npos) {
		log->error("Config line {} is invalid", servconf);
		exit(EX_CONFIG);
	}
	port = (int) strtol(servconf.substr(idx+1).c_str(), nullptr, 0);
	if (port <= 0 || port > 65535) {
		log->error("The port provided is invalid: {}", servconf);
		exit(EX_CONFIG);
	}
}

void SurfStoreServer::launch()
{
    auto log = logger();

    log->info("Launching SurfStore server");
    log->info("My ID is: {}", servernum);
    log->info("Port: {}", port);

	map<string, string> blockMap;                                           
        FileInfoMap metaMap;

	rpc::server srv(port);

	srv.bind("ping", []() {
		auto log = logger();

		log->info("ping()");
		return;
	});


	// Get a block for a specific hash                                 
        srv.bind("get_block", [&](string hash) {                                
                                                                                
                auto log = logger();                                            
                log->info("get_block()");                                       
                                                                                
                for(auto it = blockMap.cbegin(); it != blockMap.cend(); ++it)   
                {                                                               
                                                                                
                        if(it->first == hash){                                  
                                return it->second;                              
                        }                                                       
                }                                                               
                                                                                
                log->error("No matching hash");                                 
                return blockMap[hash];                                          
        });

	// Store a block
        srv.bind("store_block", [&](string hash, string data) {


                auto log = logger();

                blockMap[hash] = data;

                return;
        });

        // Download a FileInfo Map from the server
        srv.bind("get_fileinfo_map", [&]() {

                auto log = logger();
                log->info("get_fileinfo_map()");

                //return fmap;
                return metaMap;
        });

        // Record the file exists on the server metaMap                         
        srv.bind("record_file", [&](string filename, FileInfo finfo) {             
		
		auto log = logger();
               
		log->info("File {} created", filename); 
		
		metaMap[filename] = finfo;
	       	
		/*
                // File doesn't exist then create                                  
                int remoteVersion = get<0>(metaMap[filename]);                     
                int localVersion = get<0>(finfo);                                  
                                                                                   
                if(localVersion == 0){
			log->info("File {} created", filename);
                        metaMap[filename] = finfo;                                 
                }                                                                  
                else{                                                              
            		log->info("File {} already exists", filename);                         
                } 
		*/
                                                                                   
        });

	// Get blocks stored at given server                            
        srv.bind("get_stored_blocks", [&]() {                                       
                                                                                   
                auto log = logger();                                               
                log->info("get_stored_blocks()");                                   
                list<string> blocks;

		map<string, string>::iterator it;
       		for (it = blockMap.begin(); it != blockMap.end(); it++)
       		{
        	    blocks.push_back(it->first);
       		}
                                                                   
                return blocks;
        }); 



	srv.run();
}
