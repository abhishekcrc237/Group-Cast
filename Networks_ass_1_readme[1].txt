README - Group Chat Application

CS425 Programming Assignment 1
Group Members:
- Ankur Kumar (210152)
- Abhishek Choudhary (210037)
- Ashish Kumar (210212)


Assignment Features

Implemented:

	-Allows multiple clients to connect concurrently via a TCP connection.

	-Authenticates users using a username-password combination stored in users.txt.

	-Supports multiple messaging features:

		-Broadcast messages

		-Private messages between users

		-Group services (creation, joining, messaging, and leaving groups)

	-Listing all groups and their members

	-Informs all connected users when a new user joins or leaves the chat.

	-Thread-safe execution using separate std::mutex locks for clients and groups to prevent race conditions.

	-Handles client disconnections gracefully by updating the shared data structures and notifying other users.

Not Implemented:

	-Persistent message storage (server does not store messages after shutdown).

	-Encrypted communication (messages are sent as plain text).

	-Advanced error handling for network failures or corrupted messages.

Design Decisions:

1.Multi-threaded Server

	-A new thread is spawned for each client connection using std::thread.

	-This allows multiple clients to interact with the server simultaneously.

	-Processes were avoided as they are more resource-intensive compared to threads.

2.Synchronization

	-std::mutex is used to lock shared resources such as connected_clients and chat_groups.

	-Ensures that concurrent access to shared data does not cause inconsistencies.

	-Separate locks are used for clients and groups to improve efficiency and reduce contention.

3.Data Structures

	-std::unordered_map<std::string, std::string> registered_users: Stores registered usernames and passwords.

	-std::unordered_map<int, std::string> connected_clients: Maps socket descriptors to usernames.

	-std::unordered_map<std::string, std::unordered_set<int>> chat_groups: Manages group membership by storing group names and 	corresponding client sockets.

4.User Authentication

	-User credentials are loaded from a users.txt file.

	-Text file authentication was chosen for simplicity instead of using a database.

	-Passwords are stored in plaintext, which is insecure but kept for basic implementation.

	-Prevents multiple logins from the same username simultaneously.

Implementation:

1.Important Functions:

	-read_user_data(): Parses users.txt and stores user credentials in an unordered map.

	-send_to_client(): Sends a message to a specific client socket.

	-broadcast_all(): Sends a message to all connected users except the sender.

	-send_private(): Sends a private message to a specific user.

	-send_group(): Sends a message to all members of a specific group.

	-disconnect_client(): Handles client disconnections, updates data structures, and notifies others.

	-client_session(): Handles client authentication and processes incoming commands.

	-main(): Initializes the server, binds to a port, listens for connections, and creates new threads for each client.

2.Code Flow:

	-The server starts and loads users from users.txt.

	-It binds to PORT 12345 and listens for client connections.

	-When a client connects, a new thread is spawned to handle communication.

	-The client authenticates using a username and password.

	-Once authenticated, the client can:

		-Send messages (broadcast, private, or group messages)

		-Create, join, and leave groups

		-List all groups and group members

	-Server continuously listens for new client connections while processing existing clients in parallel.

Testing:

1.Correctness Testing

	-Verified authentication with valid and invalid credentials.

	-Ensured correct message delivery for all messaging features.

	-Checked proper handling of joining and leaving groups.

	-Tested listing of groups and group members.

2.Stress Testing

	-Connected multiple clients to test concurrency and synchronization.

	-Sent large messages to test buffer limits and handling.

	-Simulated random client disconnections to check server stability.

Server Restrictions:

	-Maximum Clients: Limited by system and network resources.

	-Maximum Groups: No hard-coded limit, but constrained by memory.

	-Maximum Group Members: No explicit limit, but performance may degrade with too many users in a group.

	-Maximum Message Size: MAX_BUFFER is set to 1024 bytes per message.

Challenges Faced:

	-Ensuring thread-safe operations while handling multiple clients simultaneously.

	-Managing client disconnections without causing dangling entries in data structures.

	-Debugging message formatting issues due to newline character mismatches.

	-Preventing multiple logins for the same user.

	-Handling incorrect or malformed commands from clients.

Contribution of Each Member
1.Ankur Kumar  (33.33%) : Designed authentication, private and broadcast messaging
2.Abhishek Choudhary (33.33%) : Implemented group functionality and synchronization, Readme file, Debugging
3.Ashish Kumar (33.33%) : Stress testing, correctness testing, Debugging


Sources Referred

Beej’s Guide to Network Programming

C++ documentation (cppreference.com)

Stack Overflow discussions on socket programming

Linux man pages for socket(), bind(), listen(), and accept()


