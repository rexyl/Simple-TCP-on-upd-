# Simple-TCP-on-upd-
make: sender.cpp receiver.cpp
	g++-4.6 -pthread -std=c++0x sender.cpp -o s -w
	g++-4.6 -pthread -std=c++0x receiver.cpp -o r -w


a. 	Simple chat room with server-client mode
b. 	Version(c++/4.2.1
	Apple LLVM version 6.1.0 (clang-602.0.53) (based on LLVM 3.6.0svn)
	Target: x86_64-apple-darwin14.5.0
	Thread model: posix)
	or Xcode 6.4 default compiler
c.	"make"
d.	./s [portnumber]
	./c [server ip] [portnumber]
