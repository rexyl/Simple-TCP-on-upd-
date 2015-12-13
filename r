This is a implementation of a simplified TCP­like transport layer protocol. 
The protocol should provide reliable, in order delivery of a stream of bytes. 
It recovers from network packet loss, packet corruption, packet duplication 
and packet reordering and should be able cope with dynamic network delays. 
However, it doesn’t support congestion or flow control.
(a)TCP segment structure:
	TCP is an array of bytes(unsigned char) which consists of 20 bytes header and "BUFSIZE"
	content which define as 512 bytes.
	The structure is as following:
	source			: 0 to 1
	destination		: 2 to 3
	sequence number	: 4 to 7
	ack number	   	: 8 to 12
	flag			: 12 to 13
	check sum		: 16 to 17
	length of buffer: 18 to 19
	content			: 20 to BUFSIZE-1

(b,c)States and mechanism of sender:
	1. Listening at TCP port, waiting receiver to connect.
	2. Establish tcp connection and move to send packets till reach window maximum.
	3. (Thread1)Receive tcp packets from receiver that contain ack information.
	3. (Thread2)For each send out message, start a timer to count the trip time, 
		resend if timeout.
	4. All the segments transfered and all the resend buffer is clear, send FIN to receiver.
(b,c)States and mechanism of receiver:
	1. Send a file transfer request to tcp port of sender.
	2. Establish tcp connection and receive packets from sender.
	3. For each received packets send ack = seq+1 to sender.
		i. if packets is in order, write it to file and greedily forward out of order buffer
		ii. if packets is out of order, store it at out of order buffer.
	4. Receive a FIN from sender, ready to exit.

Sample invoke:
./s README 127.0.0.1 41192 4120 stdout 5
./r R 41194 127.0.0.1 4120 stdout
Note that "s" must go first, because sender should wait for a file transfer request.
And I don't specify the port of UDP so the instream of proxy port should be *.

./newudpl -ilocalhost/* -olocalhost -L5 -s5000
