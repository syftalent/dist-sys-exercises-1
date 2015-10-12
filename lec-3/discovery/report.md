Team members:  
Yifei Shen, Huida Tao, Jizi Shangguan   

Breif summary:  
Our discovery server support such commands: ADD, REMOVE, LOOKUP, LOOKUP_MULTI  

Supported protocol:
ADD <unit1> <unit2> <hostip> <port>     //Add new conversion server with ip and port  
REMOVE <hostip> <port>                  //Remove exited conversion server with units (order is not sensitive)  
LOOKUP <unit1> <unit2>                  //Lookup exited conversion server with its ip and port (order is not sensitive)  

Extended protocol
LOOKUP_MULTI <unit1> <unit2> <hostip> <port> <isEnd>   //Search the conversion path of units (order is not sensitive)  

Suggest test commands:  
ADD lbs b 192.168.0.1 1234  
ADD ban b 192.168.0.2 1233  
LOOKUP lbs b   
LOOKUP_MULTI ban lbs  
REMOVE lbs b  
LOOKUP lbs b  

Load balancing: when multiple server with the same function applied, we will rotate the server list, if the first server got connected,
                if you try to use the same conversion again, the discovery server will automatically return the second server's IP and Port.

                Technique: In our discovery server, we maped conversion units to a set of server. So each pair of units will be maped to a 
                set of servers which do the same conversion. we use linkedHashSet to store the server set. Every time a server from the list 
                get visited, we will delete it from the linkedHashSet and then add it back. So the recent visited server will go to the bottom 
                of the server set, the second server will become the first we can visit.

Fault tolerance: For faulty input by client, we have a Constants class that including all error message. Client will get explaination 
                 about the error. Also the discovery server can handle crashing. If any conversion server get off-line, the discovery
                 server will delete it from server list, so it won't be return to any user.( We also built a backup server to store a 
                 copy of server table in discovery server, and we finished the backup process, but we still can't successfully get it 
                 back from backup server when discovery reboots so it will not contains in the code we turned in, but we can show you 
                 some when we do the demo.)
                 
                 Technique: in the discovery server, every time it get request from client, the discovery server will send a check message 
                 to all the conversion servers in the server list. if some of them are off-line, the connection will rasie an exception, so 
                 we can know which server is off-line and delete it from the server list. For backup server, we create a new server for it, we 
                 first serialize all the class we use to create server list, then use ObjectInputStream and ObjectOutputStream to send the server 
                 list to the backup server. when discovery server crashed and reboots, all the conversion servers don't need to register again, 
                 the discovery server can get the backup from backup server.
