CC=g++
CFLAGS=-c -Wall -Wextra -Werror -std=c++17 -ltbb
LDFLAGS= -ltbb
SOURCES=document.cpp main.cpp process_queries.cpp  read_input_functions.cpp\
		remove_duplicates.cpp request_queue.cpp search_server.cpp string_processing.cpp
HEDEAR=search_server.h concurrent_map.h document.h paginator.h process_queries.h  read_input_functions.h\
		remove_duplicates.h  request_queue.h string_processing.h
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=main

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) $(HEDEAR)
	$(CC)  $(OBJECTS) $(LDFLAGS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf *.o
