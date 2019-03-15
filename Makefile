.PHONY:all 
all:httpserver upload

httpserver:HttpServer.cpp
	g++ $^ -o $@ -pthread -std=c++11

upload:UpLoader.cpp 
	g++ $^ -o $@ -std=c++11

.PHONY:clean
clean:
	rm -rf httpserver upload
