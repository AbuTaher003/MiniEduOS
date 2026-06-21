## MiniEduOS Command Reference

This document lists all available commands in MiniEduOS based on implemented modules.

---

# 1. Memory Manager Commands

```
mem
meminfo
malloc <size>
free
memmap
```

Description:

* mem / meminfo → show memory usage and allocation status
* malloc <size> → allocate memory block (simulation)
* free → free allocated memory block
* memmap → display memory layout map

---

# 2. Process Manager Commands

```
ps
new <name>
run <pid>
kill <pid>
top
procinfo
```

Description:

* ps → list all processes
* new <name> → create new process
* run <pid> → run selected process
* kill <pid> → terminate process
* top → process table sorted by CPU usage
* procinfo → summary of process states

---

# 3. CPU Scheduling Commands

```
schedule
fcfs
sjf
priority
rr <quantum>
gantt
gantt fcfs
gantt sjf
gantt priority
gantt rr
```

Description:

* schedule → show scheduling menu or last result
* fcfs → First Come First Serve scheduling
* sjf → Shortest Job First scheduling
* priority → Priority scheduling
* rr <quantum> → Round Robin scheduling with time quantum
* gantt → display Gantt chart of last execution
* gantt <algo> → display Gantt chart for specific algorithm

---

# 4. File System Commands (RAM-based)

```
ls
mkdir <name>
touch <name>
cat <file>
write <file>
rm <file>
pwd
tree
cd <path>
open <file>
```

Description:

* ls → list files and directories
* mkdir → create directory
* touch → create file
* cat → display file content
* write → write/append content to file
* rm → delete file or directory
* pwd → show current path
* tree → display directory structure
* cd → change directory
* open → open file in GUI (if supported)

---

# 5. GUI / Desktop Commands

```
desktop
refresh
terminal
settings
about
sysinfo
top (GUI variant also supported)
```

Description:

* desktop → return to desktop view
* refresh → reload desktop UI
* terminal → open terminal window
* settings → open system settings panel
* about → show system information
* sysinfo → system summary

---

# 6. System Monitoring Commands

```
sysinfo
uptime
meminfo
procinfo
```

Description:

* sysinfo → full system status overview
* uptime → system running time
* meminfo → memory usage summary
* procinfo → process state summary

---

# 7. Utility Commands

```
help
clear
exit
```

Description:

* help → show available commands
* clear → clear terminal screen
* exit → exit shell or close session

---

# Notes

* All commands operate on simulated OS environment
* File system is RAM-based only
* Process and memory values are educational simulations
* GUI commands may open graphical windows if supported
* Scheduling commands modify CPU execution simulation state
