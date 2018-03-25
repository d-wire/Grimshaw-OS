%.o: %.cpp
	g++ -fPIC -std=gnu++0x -c $< -o $@

libFAT32.so: fat_32r.o
	g++ -shared $^ -o $@
	
clean:
	rm -f *.o *~ *.so *.out