MAKEFILE COMMANDS:
make (or make all):
compiles both server.cpp and client.cpp

make client
compiles and runs client

make server
compiles and runs server

make clean
removes all executables, as well as db file folder

TO USE THIS PROGRAM:
running on the zone server using (at least) two different instances, make sure 
the src file folder is in both destinations. On the first destination use 
"make server" command to start the server. Once the server is started, on the 
other destination(s), use the "make client" command to start the client. Once
both the client and server are started, you can start sending messages from the
client to the server.

IF MAKE COMMANDS ARE NOT WORKING:
to compile server: 
g++ server.cpp -o server && ./server

to compile client:
g++ client.cpp -o client && ./client

NOTE: Once both client and server are running, the server will output the correct
hostname to use for the HELO <server-hostname> to function properly which will allow
the client to enter the email sequence.
