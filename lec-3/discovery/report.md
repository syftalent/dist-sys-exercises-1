Team members:  
Programmer :Yifei Shen, Huida Tao   
Protocol Disginer: Yu Ma, Luyao Ma  

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
