hellomake: sender.cpp receiver.cpp
	g++-4.6 -pthread -std=c++0x sender.cpp -o s -w
	g++-4.6 -pthread -std=c++0x receiver.cpp -o r -w
