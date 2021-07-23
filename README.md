# windows-drivers

### Thread Priority Booster Driver
This driver is part of the book "Windows Kernel Programming" and it's goal is to raise a thread priority level.
From user mode you can't reach any priority level you want because of the way that the priority is being calculated, so you can only achive it with code running at kernel space
for direct access to the `KTHREAD` structure.

### ProcessGuard Driver
This is my solution to exercise 1 chapter 9.
The exercise is to write a driver that recieves proccess paths from user mode and prevent their execution.
