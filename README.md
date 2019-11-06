# Cloud Computing | Amazon Web Services (AWS) Communication

### Repository Contents

src/Dowloader.cc: Main downloader client, downloads data from the different servers and measure timing properties of the connections.
src/Uploader.cc: Main uploader client, uploads data based on the implemented block replication policy.
src/SurfStoreServer.cc: Handles direct block access functions to store and upload data.

Project_Report.pdf: Report summarizing experiment results.

Collected_Experiment_Data.pdf: Raw data collected later used for analysis.

dependencies folder: Contains dependencies needed to build the project, as well as libraries used for diffferent functionlity (rpc calls, logging, sha encryption)


### Project Overview

Amazon EC2 servers are set up with VMs in four different locations:
* Seoul, Korea
* Dublin, Ireland
* Sao Paulo, Brazil
* Mumbai, India

The servers are aware of each other’s’ addresses and have software to upload and download data to designated directories in each VM. 
Files are separated into blocks, and such blocks are stored differently depending in what block replacement policy is used to do so.
Below are the 5 block replacement policies used across the servers.
* Random policy: when a file is upload, it chooses, for each block, a random datacenter and stores the block there.
* Tworandom policy: client chooses two datacenters at random for each block (must be two different ones).
* Local policy: store the blocks on the block store running on the same datacenter as the client (localhost). 
* Localclosest policy: client stores in localhost and on the closest remaining datacenter (lowest RTT: round trip time).
* Localfarthest policy: client stores in localhost and on the farthest remaining datacenter (farthest RTT: round trip time).
