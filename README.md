# Cloud Computing | Amazon Web Services (AWS) Communication

## Project Overview

Amazon EC2 servers are set up with VMs in four different locations:
•	Seoul, Korea
•	Dublin, Ireland
•	Sao Paulo, Brazil
•	Mumbai, India

The servers are aware of each other’s’ addresses and have software to upload and download data to designated directories in each VM. 
Files are separated into blocks, and such blocks are stored differently depending in what block replacement policy is used to do so.
Below are the 5 block replacement policies used across the servers.
•	Random policy: when a file is upload, it chooses, for each block, a random datacenter and stores the block there.
•	Tworandom policy: client chooses two datacenters at random for each block (must be two different ones).
•	Local policy: store the blocks on the block store running on the same datacenter as the client (localhost). 
•	Localclosest policy: client stores in localhost and on the closest remaining datacenter (lowest RTT: round trip time).
•	Localfarthest policy: client stores in localhost and on the farthest remaining datacenter (farthest RTT: round trip time).
