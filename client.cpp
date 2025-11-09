/*
 * C++ File Sharing Client
 *
 * This client connects to the server, authenticates, and provides
 * a command-line interface for:
 * 1. LIST: Listing remote files.
 * 2. DOWNLOAD: Downloading files from the server.
 * 3. UPLOAD: Uploading files to the server.
 *
 * Supports Windows (Winsock) and POSIX (Linux/macOS) sockets.
 */

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <filesystem> // For directory creation

// --- Platform-Specific Includes ---
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
const char* HOST = "127.0.0.1";
const int PORT = 9999;
const int BUFFER_SIZE = 4096;
const char* CLIENT_FILES_DIR = "client_files";
const std::string ENCRYPTION_KEY = "mysecretkey";
// --- End Configuration ---

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
 * @brief Sends a command (string) to the server, with encryption.
 */
bool sendCommand(SocketType sock, const std::string& cmd) {
    std::string encryptedCmd = encryptDecrypt(cmd);
    return send(sock, encryptedCmd.c_str(), encryptedCmd.length(), 0) > 0;
}

/**
 * @brief Receives a response from the server, with decryption.
 */
std::string receiveResponse(SocketType sock) {
    char buffer[BUFFER_SIZE] = {0};
    int bytesReceived = recv(sock, buffer, BUFFER_SIZE, 0);
    if (bytesReceived <= 0) {
        return ""; // Connection closed or error
    }
    return encryptDecrypt(std::string(buffer, bytesReceived));
}

/**
 * @brief Handles the LIST command response.
 */
void handleList(SocketType sock) {
    std::string response = receiveResponse(sock);
    std::cout << response << std::endl;
}

/**
 * @brief Handles the DOWNLOAD command logic.
 */
void handleDownload(SocketType sock, const std::string& filename) {
    std::string response = receiveResponse(sock);
    std::stringstream ss(response);
    std::string command;
    ss >> command;

    if (command == "OK_DOWNLOAD") {
        long long fileSize;
        ss >> fileSize;
        std::cout << "[+] Server OK. File size: " << fileSize << " bytes." << std::endl;
        std::string filepath = std::string(CLIENT_FILES_DIR) + "/" + filename;
        std::ofstream outFile(filepath, std::ios_base::binary); // <-- FIX: std::ios to std::ios_base

        if (!outFile.is_open()) {
            std::cerr << "[-] Error: Could not open file for writing: " << filepath << std::endl;
            sendCommand(sock, "CANCEL"); // Tell server to stop
            return;
        }

        // 2. Tell server we are ready
        sendCommand(sock, "START");

        // 3. Receive file data in chunks
        long long bytesReceived = 0;
        std::cout << "[+] Downloading " << filename << "..." << std::endl;
        while (bytesReceived < fileSize) {
            std::string chunk = receiveResponse(sock);
            if (chunk.empty()) {
                std::cerr << "[-] Error: Connection lost during download." << std::endl;
                break;
            }
            
            // FIX: Removed the logic that was incorrectly checking for DOWNLOAD_DONE here.
            // That signal comes after the loop.

            // Ensure we don't write more bytes than expected
            long long bytesToWrite = chunk.length();
            if (bytesReceived + bytesToWrite > fileSize) {
                bytesToWrite = fileSize - bytesReceived;
                chunk = chunk.substr(0, bytesToWrite);
            }

            outFile.write(chunk.c_str(), bytesToWrite);
            bytesReceived += bytesToWrite;
        }
        outFile.close();

        if (bytesReceived >= fileSize) {
            std::cout << "[+] Download complete: " << filepath << std::endl;
            // FIX: Wait for the final "DOWNLOAD_DONE" signal from the server
            std::string done_signal = receiveResponse(sock);
            if (done_signal != "DOWNLOAD_DONE") {
                std::cout << "[+] Warning: Did not receive final DONE signal. Got: " << done_signal << std::endl;
            }
        } else {
            std::cerr << "[-] Download failed. Incomplete file." << std::endl;
        }

    } else {
        std::cout << "[-] Server error: " << response << std::endl;
    }
}

/**
 * @brief Handles the UPLOAD command logic.
 */
void handleUpload(SocketType sock, const std::string& filename) {
    std::string filepath = std::string(CLIENT_FILES_DIR) + "/" + filename;
    std::ifstream file(filepath, std::ios_base::binary | std::ios_base::ate); // <-- FIX: std::ios to std::ios_base

    if (!file.is_open()) {
        std::cerr << "[-] Error: File not found in 'client_files' directory: " << filename << std::endl;
        return;
    }

    long long fileSize = file.tellg();
    file.seekg(0, std::ios_base::beg); // <-- FIX: std::ios to std::ios_base

    // 1. Send UPLOAD command with filename and size
    sendCommand(sock, "UPLOAD " + filename + " " + std::to_string(fileSize));

    // 2. Wait for server OK
    std::string response = receiveResponse(sock);
    if (response != "OK_UPLOAD") {
        std::cerr << "[-] Server error: " << response << std::endl;
        return;
    }

    // 3. Send file data in chunks
    std::cout << "[+] Uploading " << filename << " (" << fileSize << " bytes)..." << std::endl;
    char fileBuffer[BUFFER_SIZE / 2]; // Smaller chunks for sending
    while (file.read(fileBuffer, sizeof(fileBuffer)) || file.gcount() > 0) {
        std::string chunk(fileBuffer, file.gcount());
        if (!sendCommand(sock, chunk)) {
            std::cerr << "[-] Error: Connection lost during upload." << std::endl;
            return;
        }
    }
    file.close();

    // 4. Wait for final confirmation
    response = receiveResponse(sock);
    std::cout << "[+] Server response: " << response << std::endl;
}

/**
 * @brief Initializes platform-specific networking (e.g., Winsock).
 * @return 0 on success, -1 on failure.
 */
int initialize_networking() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
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

/**
 * @brief Main function to run the client.
 */
int main() {
    if (initialize_networking() != 0) {
        return 1;
    }

    SocketType sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { // Or INVALID_SOCKET
        std::cerr << "[-] Failed to create socket." << std::endl;
        cleanup_networking();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[-] Connection failed. Is the server running?" << std::endl;
        CLOSE_SOCKET(sock);
        cleanup_networking();
        return 1;
    }

    std::cout << "[+] Connected to server at " << HOST << ":" << PORT << std::endl;

    // --- Authentication ---
    bool isAuthenticated = false;
    while (!isAuthenticated) {
        std::string user, pass;
        std::cout << "Username: ";
        std::getline(std::cin, user);
        std::cout << "Password: ";
        std::getline(std::cin, pass);

        sendCommand(sock, "AUTH " + user + " " + pass);
        std::string response = receiveResponse(sock);

        if (response == "AUTH_SUCCESS") {
            isAuthenticated = true;
            std::cout << "[+] Authentication successful!" << std::endl;
        } else {
            std::cout << "[-] Authentication failed. Please try again." << std::endl;
        }
    }

    // Ensure client files directory exists
    if (!std::filesystem::exists(CLIENT_FILES_DIR)) {
        std::filesystem::create_directory(CLIENT_FILES_DIR);
        std::cout << "[+] Created directory: " << CLIENT_FILES_DIR << std::endl;
    }

    // --- Command Loop ---
    std::string line;
    while (true) {
        std::cout << "\n(list, upload [file], download [file], quit)\n> ";
        std::getline(std::cin, line);
        
        std::stringstream ss(line);
        std::string command;
        ss >> command;

        if (command.empty()) continue;

        if (command == "list") {
            sendCommand(sock, "LIST");
            handleList(sock);
        } else if (command == "download") {
            std::string filename;
            ss >> filename;
            if (filename.empty()) {
                std::cout << "Usage: download [filename]" << std::endl;
                continue;
            }
            sendCommand(sock, "DOWNLOAD " + filename);
            handleDownload(sock, filename);
        } else if (command == "upload") {
            std::string filename;
            ss >> filename;
            if (filename.empty()) {
                std::cout << "Usage: upload [filename]" << std::endl;
                continue;
            }
            handleUpload(sock, filename);
        } else if (command == "quit") {
            sendCommand(sock, "QUIT");
            break;
        } else {
            std::cout << "[-] Unknown command." << std::endl;
        }
    }

    CLOSE_SOCKET(sock);
    cleanup_networking();
    return 0;
}