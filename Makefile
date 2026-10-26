# ECE 492/592 HW3 Track B -- build, run, plot, package.
#   make                     build bin/q4_q5_rob_prf and bin/q6_q7_tlb
#   make run MACHINE=crux    run every experiment on this machine
#   make plot                side-by-side plots from data/artemisia + data/crux
#   make zip                 submission archive

CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=gnu11
LDLIBS  := -lm
BINS    := bin/q4_q5_rob_prf bin/q6_q7_tlb
MACHINE ?=

all: $(BINS)

bin/%: src/%.c src/common.h | bin
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

bin:
	mkdir -p bin

run: all
	@test -n "$(MACHINE)" || { echo "usage: make run MACHINE=<name>"; exit 1; }
	./scripts/run_all.sh $(MACHINE)

plot:
	python3 scripts/plot.py

zip: all
	rm -f hw3_trackB.zip
	zip -r hw3_trackB.zip src scripts data plots report docs \
	    AI_LOG.md REPRODUCE.md README.md Makefile -x '*.o' -x '__pycache__/*'

clean:
	rm -rf bin

.PHONY: all run plot zip clean
