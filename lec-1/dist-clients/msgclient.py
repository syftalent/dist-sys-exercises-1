# CS 6421 - Simple Message Board Client in Python
# Yifei Shen G49720084
# Run with:     python msgclient.py

import socket
import sys

#check the number of arguments
if len(sys.argv) != 4:
    print("The number of arguments is invalid")
else:
    host = sys.argv[1]
    portnum = 5555;
    
    #make the connection to server
    clientsocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    clientsocket.connect((host,portnum))
    #send message
    clientsocket.send(sys.argv[2]+"\r\n")
    clientsocket.send(sys.argv[3]+"\r\n")
    
    #quit
    clientsocket.close()
    print("Done!")
