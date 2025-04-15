#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <unistd.h>
#include <cstring>

static const int SERVER_PORT = 12345;
static const int MAX_BUFFER  = 1024;

// Separate mutexes for different data
std::mutex clients_lock;
std::mutex groups_lock;

// Data structures: same functionality, different names
std::unordered_map<std::string, std::string> registered_users;     // username -> password
std::unordered_map<int, std::string> connected_clients;            // socket -> username
std::unordered_map<std::string, std::unordered_set<int>> chat_groups; // group_name -> sockets of group members

// Utility function to strip leading/trailing whitespace
std::string strip(const std::string &input) {
    auto begin = input.find_first_not_of(" \t\r\n");
    auto end   = input.find_last_not_of(" \t\r\n");
    if (begin == std::string::npos || end == std::string::npos) {
        return "";
    }
    return input.substr(begin, end - begin + 1);
}

// Read user credentials from file
void read_user_data(const std::string &filename) {
    std::ifstream file_in(filename);
    if (!file_in.is_open()) {
        std::cerr << "Could not open user credentials file: " << filename << std::endl;
        exit(EXIT_FAILURE);
    }
    std::string line;
    while (std::getline(file_in, line)) {
        std::size_t sep_pos = line.find(':');
        if (sep_pos != std::string::npos) {
            std::string uname = strip(line.substr(0, sep_pos));
            std::string pwd   = strip(line.substr(sep_pos + 1));
            registered_users[uname] = pwd;
        }
    }
    file_in.close();
}

// Send a string message to a particular client socket
void send_to_client(int client_socket, const std::string &msg) {
    if (send(client_socket, msg.c_str(), msg.size(), 0) < 0) {
        std::cerr << "Warning: Could not send to client socket " << client_socket << std::endl;
    }
}

// Notify all clients except the sender
void broadcast_all(const std::string &msg, int from_socket) {
    std::lock_guard<std::mutex> lock(clients_lock);
    std::string sender_name = connected_clients[from_socket];
    for (const auto &[sock, uname] : connected_clients) {
        if (sock != from_socket) {
            send_to_client(sock, "[Broadcast] " + sender_name + ": " + msg);
        }
    }
}

// Send a private (one-to-one) message
void send_private(int sender_socket, const std::string &recipient, const std::string &content) {
    std::lock_guard<std::mutex> lock(clients_lock);
    bool found_recipient = false;
    for (const auto &[sock, uname] : connected_clients) {
        if (uname == recipient) {
            found_recipient = true;
            send_to_client(sock, "[Whisper] " + connected_clients[sender_socket] + ": " + content);
            send_to_client(sender_socket, "[Sent -> " + recipient + "]: " + content);
            break;
        }
    }
    if (!found_recipient) {
        send_to_client(sender_socket, "Error: Could not find user '" + recipient + "' online.");
    }
}

// Send a message to all members of a specific group
void send_group(int sender_socket, const std::string &group_name, const std::string &content) {
    std::lock_guard<std::mutex> lock(groups_lock);

    auto it = chat_groups.find(group_name);
    if (it == chat_groups.end()) {
        send_to_client(sender_socket, "Error: Group '" + group_name + "' does not exist.");
        return;
    }

    // Check membership
    if (it->second.find(sender_socket) == it->second.end()) {
        send_to_client(sender_socket, "Error: You are not a member of '" + group_name + "'.");
        return;
    }

    std::string sender_name = connected_clients[sender_socket];
    for (int member_socket : it->second) {
        if (member_socket != sender_socket) {
            send_to_client(member_socket, "[Group: " + group_name + "] " + sender_name + ": " + content);
        }
    }
    send_to_client(sender_socket, "[Group: " + group_name + "] You: " + content);
}

// Safely disconnect a client from server
void disconnect_client(int client_socket) {
    std::string departing_user;
    
    {
        std::lock_guard<std::mutex> lock_clients(clients_lock);
        if (connected_clients.count(client_socket) > 0) {
            departing_user = connected_clients[client_socket];
            connected_clients.erase(client_socket);
        }
    }
    
    if (!departing_user.empty()) {
        std::lock_guard<std::mutex> lock_groups(groups_lock);
        // Remove user from all groups
        for (auto &group_pair : chat_groups) {
            group_pair.second.erase(client_socket);
        }
        // Notify others
        broadcast_all(departing_user + " left the chat.", client_socket);
    }
    close(client_socket);
}

// Thread function: handle client connection and commands
void client_session(int client_socket) {
    char buffer[MAX_BUFFER];
    
    // Step 1: Prompt for username
    send_to_client(client_socket, "Please enter your username: ");
    memset(buffer, 0, MAX_BUFFER);
    int received = recv(client_socket, buffer, MAX_BUFFER, 0);
    if (received <= 0) {
        close(client_socket);
        return;
    }
    std::string username = strip(buffer);
    
    // Step 2: Prompt for password
    send_to_client(client_socket, "Enter your password: ");
    memset(buffer, 0, MAX_BUFFER);
    received = recv(client_socket, buffer, MAX_BUFFER, 0);
    if (received <= 0) {
        close(client_socket);
        return;
    }
    std::string password = strip(buffer);

    // Step 3: Authentication
    if (!registered_users.count(username) || registered_users[username] != password) {
        send_to_client(client_socket, "Login failed. Disconnecting.\n");
        close(client_socket);
        return;
    }

    // Step 4: Check if user is already logged in
    {
        std::lock_guard<std::mutex> lock(clients_lock);
        for (const auto &item : connected_clients) {
            if (item.second == username) {
                send_to_client(client_socket, "Error: This user is already active.\n");
                close(client_socket);
                return;
            }
        }
        connected_clients[client_socket] = username;
    }

    // Step 5: Welcome messages
    send_to_client(client_socket, "Hello " + username + ", welcome to the server!\n");
    send_to_client(client_socket,
        "Commands available:\n"
        "  /broadcast <message>    - Send a public message\n"
        "  /msg <user> <message>   - Send a private message\n"
        "  /create_group <name>    - Create a new group\n"
        "  /join_group <name>      - Join an existing group\n"
        "  /leave_group <name>     - Leave a group\n"
        "  /group_msg <group> <m>  - Group message\n"
        "  /list_groups            - List all existing groups\n"
        "  /list_members <group>   - List members of a group\n"
        "  /exit                   - Disconnect\n"
    );
    broadcast_all(username + " joined the chat.", client_socket);

    // Step 6: Command loop
    while (true) {
        memset(buffer, 0, MAX_BUFFER);
        received = recv(client_socket, buffer, MAX_BUFFER, 0);
        if (received <= 0) {
            disconnect_client(client_socket);
            return;
        }

        std::string command_line = strip(buffer);
        if (command_line.empty()) {
            continue;
        }

        // Handle special commands
        if (command_line == "/exit") {
            disconnect_client(client_socket);
            return;
        }
        else if (command_line.rfind("/broadcast ", 0) == 0) {
            // /broadcast <message>
            std::string msg_content = strip(command_line.substr(11));
            broadcast_all(msg_content, client_socket);
        }
        else if (command_line.rfind("/msg ", 0) == 0) {
            // /msg <username> <message>
            size_t space_loc = command_line.find(' ', 5);
            if (space_loc != std::string::npos) {
                std::string recp = strip(command_line.substr(5, space_loc - 5));
                std::string msg  = strip(command_line.substr(space_loc + 1));
                send_private(client_socket, recp, msg);
            } else {
                send_to_client(client_socket, "Error: Invalid format. Usage: /msg <user> <message>");
            }
        }
        else if (command_line.rfind("/create_group ", 0) == 0) {
            // /create_group <group_name>
            std::string gname = strip(command_line.substr(14));
            std::lock_guard<std::mutex> grp_lock(groups_lock);
            if (chat_groups.count(gname)) {
                send_to_client(client_socket, "Error: Group '" + gname + "' already exists.");
            } else {
                chat_groups[gname].insert(client_socket);
                send_to_client(client_socket, "Group '" + gname + "' was successfully created.");
            }
        }
        else if (command_line.rfind("/join_group ", 0) == 0) {
            // /join_group <group_name>
            std::string gname = strip(command_line.substr(12));
            std::lock_guard<std::mutex> grp_lock(groups_lock);
            if (!chat_groups.count(gname)) {
                send_to_client(client_socket, "Error: No group named '" + gname + "' found.");
            } else {
                chat_groups[gname].insert(client_socket);
                send_to_client(client_socket, "You joined the group '" + gname + "'.");
            }
        }
        else if (command_line.rfind("/leave_group ", 0) == 0) {
            // /leave_group <group_name>
            std::string gname = strip(command_line.substr(13));
            std::lock_guard<std::mutex> grp_lock(groups_lock);
            auto iter = chat_groups.find(gname);
            if (iter != chat_groups.end()) {
                if (iter->second.erase(client_socket) > 0) {
                    send_to_client(client_socket, "You left the group '" + gname + "'.");
                } else {
                    send_to_client(client_socket, "Error: You were not part of '" + gname + "'.");
                }
            } else {
                send_to_client(client_socket, "Error: Group '" + gname + "' does not exist.");
            }
        }
        else if (command_line.rfind("/group_msg ", 0) == 0) {
            // /group_msg <group_name> <message>
            size_t space_pos = command_line.find(' ', 11);
            if (space_pos != std::string::npos) {
                std::string group_name = strip(command_line.substr(11, space_pos - 11));
                std::string group_msg  = strip(command_line.substr(space_pos + 1));
                send_group(client_socket, group_name, group_msg);
            } else {
                send_to_client(client_socket, "Error: Invalid format. Usage: /group_msg <group> <message>");
            }
        }
        // Additional commands: list all groups and group members
        else if (command_line == "/list_groups") {
            std::lock_guard<std::mutex> grp_lock(groups_lock);
            if (chat_groups.empty()) {
                send_to_client(client_socket, "No groups currently exist.");
            } else {
                std::ostringstream oss;
                oss << "Existing groups:\n";
                for (const auto &kv : chat_groups) {
                    oss << "  - " << kv.first << " (" << kv.second.size() << " members)\n";
                }
                send_to_client(client_socket, oss.str());
            }
        }
        else if (command_line.rfind("/list_members ", 0) == 0) {
            // /list_members <group_name>
            std::string gname = strip(command_line.substr(13));
            std::lock_guard<std::mutex> grp_lock(groups_lock);
            auto iter = chat_groups.find(gname);
            if (iter == chat_groups.end()) {
                send_to_client(client_socket, "Error: Group '" + gname + "' does not exist.");
            } else {
                std::ostringstream oss;
                oss << "Members of [" << gname << "]:\n";
                for (auto sock : iter->second) {
                    oss << "  - " << connected_clients[sock] << "\n";
                }
                send_to_client(client_socket, oss.str());
            }
        }
        else {
            // Unknown or unrecognized command
            send_to_client(client_socket, "Error: Unrecognized command. Type /help for assistance.");
        }
    }
}

int main() {
    // Step A: Read user credentials
    read_user_data("users.txt");

    // Step B: Create server socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        std::cerr << "Error: Unable to create socket." << std::endl;
        return EXIT_FAILURE;
    }

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Error: Failed setsockopt()." << std::endl;
        return EXIT_FAILURE;
    }

    // Step C: Bind the socket
    sockaddr_in server_addr{};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(SERVER_PORT);

    if (bind(server_sock, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "Error: Could not bind to port." << std::endl;
        return EXIT_FAILURE;
    }

    // Step D: Begin listening
    if (listen(server_sock, 10) < 0) {
        std::cerr << "Error: Listen call failed." << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Server started on port " << SERVER_PORT << std::endl;
    std::cout << "Awaiting incoming connections...\n";

    // Step E: Accept client connections continuously
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int new_socket = accept(server_sock, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (new_socket < 0) {
            std::cerr << "Warning: Failed to accept a new client." << std::endl;
            continue;
        }

        std::cout << "Client connected from " << inet_ntoa(client_addr.sin_addr) 
                  << ":" << ntohs(client_addr.sin_port) << std::endl;

        // Launch a thread to handle this client
        std::thread(client_session, new_socket).detach();
    }

    close(server_sock);
    return 0;
}