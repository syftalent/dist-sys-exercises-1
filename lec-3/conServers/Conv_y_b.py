#!/usr/bin/env python

#******************************************************************************
#
#  CS 6421 - Simple Conversation
#  Execution:    python Conv_y_b.py portnum
#
#******************************************************************************

import socket
import sys

## Function to process requests
def process(conn):
    #conn.send("Welcome, you are connected to a Python-based server\n")

    # read userInput from client
    userInput = conn.recv(BUFFER_SIZE)
    if not userInput:
        print "Error reading message"
        sys.exit(1)

    print "Received message: ", userInput
    # TODO: add convertion function here, reply = func(userInput)
    mylist = userInput.split(" ")
    if len(mylist) != 3:
        conn.send("Invalid Input!\n")
    #elif not mylist[2].isdigit():
    #    conn.send("Invalid Input!!\n")
    elif (mylist[0] == "y") and (mylist[1] == "b"):
            result = float(mylist[2]) / 60
            conn.send(str(result) + " bananas")
            conn.send("\n")
    elif (mylist[0] == "b") and (mylist[1] == "y"):
            result = float(mylist[2]) * 60
            conn.send(str(result) + " yen")
            conn.send("\n")
    else:
        conn.send("Invalid Input!\n")

    conn.close()


### Main code run when program is started
BUFFER_SIZE = 1024
interface = ""

# if input arguments are wrong, print out usage
if len(sys.argv) != 2:
    print >> sys.stderr, "usage: python {0} portnum\n".format(sys.argv[0])
    sys.exit(1)

portnum = int(sys.argv[1])

# create socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind((interface, portnum))
s.listen(5)

print("Start Listening")

while True:
    # accept connection and print out info of client
    conn, addr = s.accept()
    print 'Accepted connection from client', addr
    process(conn)
s.close()
