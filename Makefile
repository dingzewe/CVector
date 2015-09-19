test_vector: h2unit.o
	g++ h2unit.o test_vector.cpp -o test_vector
h2unit.o: h2unit.cpp
	g++ -c $< -o $@
clean:
	rm -rf vector.o h2unit.o test_vector
