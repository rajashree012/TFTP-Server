ECEN602 HW2 Programming Assignment (TFTP protocol implementation)
-----------------------------------------------------------------

Team Number: 12
Member 1 # Zhou, Shenmin (UIN: 823008644)
Member 2 # Polsani, Rajashree Rao (UIN: 223001584)
---------------------------------------

Description/Comments:
--------------------
1. server.c contains tftp server code
2. We have used re-transmission timeout = 100ms and connection timeout = 5 sec
3. Detailed comments are written in the code
4. Brief description of the code : 
   Client sends RRQ to serverIp and serverPort. After verifying the request server creates a new socket and binds it to a port (we have incremented the port number successively from the one specified on the command line and checked whether it is a fee port or not. Server sends first block of data from the new port. tftp clients responds with an ack through this new socket. Sockets are specific for a request). Packets are retransmitted when there is timout waiting for the ack or when the client does not receive the packet. Data is transmitted until the end of the file.

Notes from your TA:
-------------------
1. You need to submit TFTP server file along with makefile and readme.txt to Google Drive folder shared to each team.
2. Don't create new folder for submission. Upload your files to teamxx/HW2.
3. I have shared 'temp' folder for your personal (or team) use. You can do compilation or code share among team members using this folder.
4. While submitting, delete all these files and upload only server file, makefile and readme.txt
5. Grading is automated for this programming assignment. You can run "python grading_hw2.py <server_ip> <server_port>" to check your score. 
6. Make sure to check file transfer from different machines. While grading, I'll use different machines for your server and tftp client.

All the best. 

Unix command for starting server:
------------------------------------------
./server SERVER_IP SERVER_PORT

