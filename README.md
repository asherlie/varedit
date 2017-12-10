# varedit

- [x] varedit finds virtual memory addresses of variables in running processes

- [x] varedit edits these variables

### some examples of common usage
##### find the virtual memory address of a string containing "hello" in process 487, searching in all available memory locations:
  ```
  sudo ./v 487 -p hello -E -C
  ```
##### write the integer 236 to memory location 0x7ff82 of process 12692
  ```
  sudo ./v 12692 -w 0x7ff82 236
  ```
##### enter interactive mode on process 139 looking for strings in all available memory
  ```
  sudo ./v 139 -f -E -C
  ```
  or just
  ```
  sudo ./v 139 -E -C
  ```
