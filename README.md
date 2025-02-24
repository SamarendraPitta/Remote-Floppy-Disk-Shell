
🖥️ Remote Floppy Disk Shell
📌 Project Overview
The Remote Floppy Disk Shell is a UDP-based client-server application that allows users to remotely access a floppy disk mounted on a server. The project simulates a shell environment where users can interact with the floppy disk as if it were locally mounted. It is implemented in C.

The application consists of:

A UDP server daemon that manages floppy disk access requests.
A client shell that enables users to send commands to the server.

⚙️ Features
Connection Management: Clients can connect and disconnect from the server using predefined commands.
Floppy Disk Access: Users can retrieve data from specific sectors on the floppy disk.
Shell Commands: Supports commands like fmount, fumount, structure, traverse, showfat, and showsector.
Concurrency: The server can handle up to 4 clients simultaneously.
Error Handling & Security: Ensures clients use valid handles and checks for consistency in port and IP addresses.
