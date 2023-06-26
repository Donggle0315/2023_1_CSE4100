# PHASE 1: Excute Shell

## how to compile and excute
- 'Makefile' : contains compilation rule and files for creating executable file.
- 'csapp.c', 'csapp.h', 'shellex.c' : source files and header file.
- typing "make" to the command line to make excutable file "shellex".
- typing "./shellex" to the command line to excute shell.

## how to use functions in the Shell
- cd : navigate the directories in your shell
  - cd "path" : navigate the path.
  - cd ../ : navigate the parent directory.
  - cd : navigate the HOME directory.
- history : tracks shell commands excuted since your shell started.
  - history : print all commands which you typed in command line.
  - !! : print the lastest excuted command. And then excute the command.
  - !# : print the command on the # line. And then excute the command.
- quit : terminate all the child processes and quit the shell.