# PHASE 2: Pipeline Command

## how to compile and excute
- 'Makefile' : contains compilation rule and files for creating executable file.
- 'csapp.c', 'csapp.h', 'shellex.c' : source files and header file.
- typing "make" to the command line to make excutable file "shellex".
- typing "./shellex" to the command line to excute shell.

## how to use functions in the Shell
- All commands implemented in Phase 1 can be used.
- "command1" | "command2" | ... | "commandN" : 
   - Execute incoming commands tied to the pipeline.
   - The output of one data processing step leads to the input of the next step.
   - ex) ls -al | grep "abc" | sort