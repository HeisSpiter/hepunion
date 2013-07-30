all: tests

hepunion_tests: hepunion_tests.c
	$(CC) -Wextra -Wall -c -o hepunion_tests_tests.o hepunion_tests_tests.c
	$(CC) -Wextra -Wall -c -o hepunion_tests_main.o hepunion_tests_main.c
	$(CC) -Wextra -Wall -o hepunion_tests hepunion_tests_tests.o hepunion_tests_main.o

tests: hepunion_tests
	sudo ./call_tests.sh

clean:
	rm -f *~ *.o
