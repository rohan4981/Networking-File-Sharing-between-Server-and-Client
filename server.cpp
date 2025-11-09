/*
 * C++ File Sharing Server
 *
 * This server listens for client connections, handles authentication,
 * and processes file sharing commands (LIST, DOWNLOAD, UPLOAD).
 * It is multi-threaded, spawning a new thread for each client.
 *
 * Supports Windows (Winsock) and POSIX (Linux/macOS) sockets.
 */

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <fstream>
#include <sstream>
#include <cstring>
#include <map>
#include <filesystem> // For directory creation


#ifdef _WIN32
    // Windows (Winsock)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib") // Link against the Winsock library
    typedef SOCKET SocketType;
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    // POSIX (Linux/macOS)
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int SocketType;
    #define CLOSE_SOCKET(s) close(s)
#endif
// --- End Platform-Specific ---

// --- Configuration ---
const int PORT = 9999;
const int BUFFER_SIZE = 4096;
const char* SERVER_FILES_DIR = "server_files";
const std::string ENCRYPTION_KEY = "mysecretkey";

// Simple user database
std::map<std::string, std::string> VALID_USERS = {
    {"user", "pass123"},
    {"admin", "adminpass"}
};
// --- End Configuration ---

/**
 * @brief Logs a message to the console with a [SERVER] prefix.
 */
void log(const std::string& message) {
    std::cout << "[SERVER] " << message << std::endl;
}

/**
 * @brief "Encrypts" or "Decrypts" data using a simple XOR cipher.
 * This is NOT secure and is for educational purposes only.
 */
std::string encryptDecrypt(const std::string& data) {
    std::string result = data;
    for (size_t i = 0; i < data.size(); ++i) {
        result[i] = data[i] ^ ENCRYPTION_KEY[i % ENCRYPTION_KEY.size()];
    }
    return result;
}

/**
 * @brief Sends a response (string) to the client, with encryption.
 */
bool sendResponse(SocketType clientSocket, const std::string& response) {
    std::string encryptedResponse = encryptDecrypt(response);
    int bytesSent = send(clientSocket, encryptedResponse.c_str(), encryptedResponse.length(), 0);
    return bytesSent > 0;
}

/**
 * @brief Receives a command from the client, with decryption.
 */
std::string receiveCommand(SocketType clientSocket) {
    char buffer[BUFFER_SIZE] = {0};
    int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (bytesReceived <= 0) {
        return ""; // Connection closed or error
    }
    return encryptDecrypt(std::string(buffer, bytesReceived));
}

/**
 * @brief Handles a single client connection.
 * @param clientSocket The socket for the connected client.
 */
void handle_client(SocketType clientSocket) {
    std::string clientAddr = "Unknown"; // In a real app, get this from accept()
    log("New client connected.");

    bool isAuthenticated = false;

    try {
        while (true) {
            std::string cmd = receiveCommand(clientSocket);
            if (cmd.empty()) {
                log("Client disconnected abruptly.");
                break;
            }

            log("Received command: " + cmd);
            std::stringstream ss(cmd);
            std::string command;
            ss >> command;

            if (!isAuthenticated) {
                if (command == "AUTH") {
                    std::string user, pass;
                    ss >> user >> pass;
                    if (VALID_USERS.count(user) && VALID_USERS[user] == pass) {
                        isAuthenticated = true;
                        sendResponse(clientSocket, "AUTH_SUCCESS");
                        log("User '" + user + "' authenticated.");
                    } else {
                        sendResponse(clientSocket, "AUTH_FAIL");
                        log("Failed auth attempt for user '" + user + "'.");
                    }
                } else {
                    sendResponse(clientSocket, "ERROR Authentication required.");
                }
                continue;
            }

            // --- Authenticated Commands ---

            if (command == "LIST") {
                std::string fileList = "Files on server:\n";
                for (const auto& entry : std::filesystem::directory_iterator(SERVER_FILES_DIR)) {
                    fileList += entry.path().filename().string() + "\n";
                }
                sendResponse(clientSocket, fileList);

            } else if (command == "DOWNLOAD") {
                std::string filename;
                ss >> filename;
                std::string filepath = std::string(SERVER_FILES_DIR) + "/" + filename;

                std::ifstream file(filepath, std::ios::binary | std::ios::ate);
                if (file.is_open()) {
                    std::streamsize size = file.tellg();
                    file.seekg(0, std::ios::beg);
                    
                    // 1. Send OK and file size
                    sendResponse(clientSocket, "OK_DOWNLOAD " + std::to_string(size));

                    // 2. Wait for client readiness (expect "START")
                    if (receiveCommand(clientSocket) != "START") {
                        log("Client did not start transfer.");
                        continue;
                    }

                    // 3. Send file data in chunks
                    char fileBuffer[BUFFER_SIZE];
                    while (file.read(fileBuffer, sizeof(fileBuffer)) || file.gcount() > 0) {
                        std::string chunk(fileBuffer, file.gcount());
                        sendResponse(clientSocket, chunk); // Send encrypted chunk
                    }
                    file.close();
                    log("Finished sending " + filename);
                    sendResponse(clientSocket, "DOWNLOAD_DONE"); // Send final chunk

                } else {
                    sendResponse(clientSocket, "ERROR File not found.");
                }

            } else if (command == "UPLOAD") {
                std::string filename;
                long long fileSize;
                ss >> filename >> fileSize;
                
                std::string filepath = std::string(SERVER_FILES_DIR) + "/" + filename;
                std::ofstream outFile(filepath, std::ios::binary);
                
                if (!outFile.is_open()) {
                    sendResponse(clientSocket, "ERROR Cannot create file.");
                    continue;
                }
                
                // 1. Send OK to start transfer
                sendResponse(clientSocket, "OK_UPLOAD");
                
                // 2. Receive file data
                long long bytesReceived = 0;
                while (bytesReceived < fileSize) {
                    std::string chunk = receiveCommand(clientSocket);
                    if (chunk.empty()) {
                        log("Upload failed: Client disconnected.");
                        break;
                    }
                    outFile.write(chunk.c_str(), chunk.length());
                    bytesReceived += chunk.length();
                }
                outFile.close();
                
                if (bytesReceived == fileSize) {
                    log("Successfully received " + filename);
                    sendResponse(clientSocket, "UPLOAD_SUCCESS");
                } else {
                    log("Upload failed for " + filename + ". Incomplete data.");
                    sendResponse(clientSocket, "ERROR Upload incomplete.");
                }

            } else if (command == "QUIT") {
                log("Client sent QUIT. Disconnecting.");
                break;
            } else {
                sendResponse(clientSocket, "ERROR Unknown command.");
            }
        }
    } catch (const std::exception& e) {
        log(std::string("Error handling client: ") + e.what());
    }

    CLOSE_SOCKET(clientSocket);
    log("Client connection closed.");
}

/**
 * @brief Initializes platform-specific networking (e.g., Winsock).
 * @return 0 on success, -1 on failure.
 */
int initialize_networking() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log("WSAStartup failed.");
        return -1;
    }
#endif
    return 0;
}

/**
 * @brief Cleans up platform-specific networking.
 */
void cleanup_networking() {
#ifdef _WIN32
    WSACleanup();
#endif
}

int main() {
    if (initialize_networking() != 0) {
        return 1;
    }

    // Ensure server files directory exists
    if (!std::filesystem::exists(SERVER_FILES_DIR)) {
        std::filesystem::create_directory(SERVER_FILES_DIR);
        log("Created directory: " + std::string(SERVER_FILES_DIR));
    }

    SocketType serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) { // Or INVALID_SOCKET for Winsock
        log("Failed to create socket.");
        cleanup_networking();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        log("Bind failed.");
        CLOSE_SOCKET(serverSocket);
        cleanup_networking();
        return 1;
    }

    if (listen(serverSocket, 5) < 0) {
        log("Listen failed.");
        CLOSE_SOCKET(serverSocket);
        cleanup_networking();
        return 1;
    }

    log("Server listening on port " + std::to_string(PORT) + "...");

    while (true) {
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        SocketType clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, (socklen_t*)&clientAddrSize);

        if (clientSocket < 0) { // Or INVALID_SOCKET
            log("Accept failed.");
            continue;
        }

        // Create a new thread to handle this client
        std::thread clientThread(handle_client, clientSocket);
        clientThread.detach(); // Detach the thread to run independently
    }

    CLOSE_SOCKET(serverSocket);
    cleanup_networking();
    return 0;
}