# PHASE 2: Background Process

## how to compile and excute
- 'Makefile' : contains compilation rule and files for creating executable file.
- 'csapp.c', 'csapp.h', 'shellex.c' : source files and header file.
- typing "make" to the command line to make excutable file "shellex".
- typing "./shellex" to the command line to excute shell.

## how to use functions in the Shell
- All commands implemented in Phase 1 and Phase2 can be used.
- "command" & : excute command in background.
  - command runs in background so that new command line can typed. 
- jobs : print all running and stopped jobs.
  - print "job num, state(RUNNING|SUSPENDED), cmdline" on the terminal. 
- bg \<job> : change a stopped background job to a running background job.
  - bg %"job_num"
- fg \<job> : change a stopped or running background job to a running in the foreground.
  - fg %"job_num"
- kill \<job> : terminate a job.
  - kill %"job_num" 
- Ctrl + C : terminate a process running in foreground.
- Ctrl + Z : stop a process running in foreground.
- Ctrl + \ : terminate shellex program.